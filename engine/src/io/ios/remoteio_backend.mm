// Backend âm thanh iOS: AVAudioSession + AudioUnit RemoteIO (CLAUDE.md mục 3.1, ADR-002).
//
//  - Session .playAndRecord + mode .measurement (tắt xử lý giọng của hệ thống → ít trễ, tiếng
//    trung thực), ra loa ngoài mặc định, cho phép tai nghe Bluetooth A2DP (chỉ nghe nhạc).
//  - 48 kHz, IO buffer 128 frame (≈ 2,7 ms).
//  - Một render callback trên bus 0 (loa/tai nghe): gọi AudioUnitRender lấy micro từ bus 1 vào
//    buffer cấp phát sẵn, rồi gọi engine xử lý → full-duplex trong cùng một callback.
//  - Theo dõi đổi route (cắm/rút tai nghe, Bluetooth) và bị ngắt (cuộc gọi đến).
//  - RemoteIO không báo xrun → tự phát hiện qua bước nhảy của mSampleTime.
//
// Biên dịch với ARC (-fobjc-arc).

#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "core/engine.h"
#include "io/audio_backend.h"

namespace tonecore {

namespace {

constexpr UInt32 kMaxFramesPerSlice = 4096;
constexpr double kPreferredBufferFrames = 128.0;

int32_t routeFromSession(AVAudioSession* session) {
  AVAudioSessionRouteDescription* route = session.currentRoute;
  if (route.outputs.count == 0) return TC_ROUTE_UNKNOWN;
  NSString* port = route.outputs.firstObject.portType;
  if ([port isEqualToString:AVAudioSessionPortBuiltInSpeaker]) return TC_ROUTE_SPEAKER;
  if ([port isEqualToString:AVAudioSessionPortBuiltInReceiver]) return TC_ROUTE_RECEIVER;
  if ([port isEqualToString:AVAudioSessionPortHeadphones] ||
      [port isEqualToString:AVAudioSessionPortLineOut]) {
    return TC_ROUTE_WIRED;
  }
  if ([port isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
      [port isEqualToString:AVAudioSessionPortBluetoothHFP] ||
      [port isEqualToString:AVAudioSessionPortBluetoothLE] ||
      [port isEqualToString:AVAudioSessionPortAirPlay]) {
    return TC_ROUTE_BLUETOOTH;  // AirPlay cũng trễ lớn → xử lý như Bluetooth
  }
  if ([port isEqualToString:AVAudioSessionPortUSBAudio]) return TC_ROUTE_USB;
  return TC_ROUTE_UNKNOWN;
}

AudioStreamBasicDescription floatFormat(double rate, UInt32 channels) {
  AudioStreamBasicDescription f{};
  f.mSampleRate = rate;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  f.mBitsPerChannel = 32;
  f.mChannelsPerFrame = channels;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = 4 * channels;
  f.mBytesPerPacket = f.mBytesPerFrame;
  return f;
}

class RemoteIOBackend final : public AudioBackend {
 public:
  RemoteIOBackend(BackendHost& host, const EngineConfig& config)
      : host_(host), sampleRate_(config.sampleRate), inBuf_(kMaxFramesPerSlice, 0.0f) {}

  ~RemoteIOBackend() override { stop(); }

  tc_result start() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (unit_ != nullptr) return TC_ERR_INVALID_STATE;
    tc_result r = configureSession();
    if (r != TC_OK) return r;
    r = createUnit();
    if (r != TC_OK) {
      disposeUnit();
      return r;
    }
    addObservers();
    return TC_OK;
  }

  void stop() override {
    std::lock_guard<std::mutex> lock(mutex_);
    removeObservers();
    disposeUnit();
    [[AVAudioSession sharedInstance]
          setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
              error:nil];
  }

  BackendInfo info() const override {
    AVAudioSession* s = [AVAudioSession sharedInstance];
    BackendInfo i;
    i.sampleRate = sampleRate_;
    i.burstFrames = static_cast<uint32_t>(s.IOBufferDuration * s.sampleRate + 0.5);
    i.bufferFrames = i.burstFrames;
    i.inputChannels = 1;
    i.outputChannels = 2;
    i.inputLatencyMs = s.inputLatency * 1000.0;
    i.outputLatencyMs = s.outputLatency * 1000.0;
    i.route = routeFromSession(s);
    i.api = TC_AUDIO_API_REMOTEIO;
    return i;
  }

  uint32_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }

 private:
  tc_result configureSession() {
    AVAudioSession* s = [AVAudioSession sharedInstance];
    NSError* err = nil;
    const AVAudioSessionCategoryOptions opts = AVAudioSessionCategoryOptionDefaultToSpeaker |
                                               AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    if (![s setCategory:AVAudioSessionCategoryPlayAndRecord
                   mode:AVAudioSessionModeMeasurement
                options:opts
                  error:&err]) {
      return TC_ERR_DEVICE;
    }
    [s setPreferredSampleRate:sampleRate_ error:nil];
    [s setPreferredIOBufferDuration:kPreferredBufferFrames / sampleRate_ error:nil];
    if (![s setActive:YES error:&err]) return TC_ERR_DEVICE;
    if (s.recordPermission != AVAudioSessionRecordPermissionGranted) {
      // Không có quyền micro: vẫn chạy (phát beat được) nhưng micro im lặng.
      micAllowed_ = false;
    } else {
      micAllowed_ = true;
    }
    return TC_OK;
  }

  tc_result createUnit() {
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr || AudioComponentInstanceNew(comp, &unit_) != noErr) return TC_ERR_DEVICE;

    // Bật đầu vào (bus 1 = micro); bus 0 (loa) bật sẵn.
    UInt32 one = 1;
    if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
                             &one, sizeof(one)) != noErr) {
      return TC_ERR_DEVICE;
    }

    // Định dạng phía ứng dụng: micro mono float, loa stereo float interleaved, 48 kHz.
    // RemoteIO tự đổi tần số nếu phần cứng khác.
    AudioStreamBasicDescription inFmt = floatFormat(sampleRate_, 1);
    AudioStreamBasicDescription outFmt = floatFormat(sampleRate_, 2);
    if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1,
                             &inFmt, sizeof(inFmt)) != noErr ||
        AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                             &outFmt, sizeof(outFmt)) != noErr) {
      return TC_ERR_NOT_SUPPORTED;
    }

    UInt32 maxFrames = kMaxFramesPerSlice;
    AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                         &maxFrames, sizeof(maxFrames));

    AURenderCallbackStruct cb{};
    cb.inputProc = &RemoteIOBackend::renderCallback;
    cb.inputProcRefCon = this;
    if (AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                             &cb, sizeof(cb)) != noErr) {
      return TC_ERR_DEVICE;
    }

    lastSampleTime_ = -1.0;
    if (AudioUnitInitialize(unit_) != noErr) return TC_ERR_DEVICE;
    if (AudioOutputUnitStart(unit_) != noErr) return TC_ERR_DEVICE;
    return TC_OK;
  }

  void disposeUnit() {
    if (unit_ == nullptr) return;
    AudioOutputUnitStop(unit_);  // sau khi trả về, không còn callback nào
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
  }

  // [RT] Render callback của bus 0 (loa/tai nghe).
  static OSStatus renderCallback(void* refCon, AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp* ts, UInt32 /*bus*/, UInt32 frames,
                                 AudioBufferList* ioData) {
    auto* self = static_cast<RemoteIOBackend*>(refCon);
    float* out = static_cast<float*>(ioData->mBuffers[0].mData);
    if (frames > kMaxFramesPerSlice || out == nullptr) return noErr;

    // Phát hiện xrun: mSampleTime nhảy cóc nghĩa là đã bỏ lỡ dữ liệu.
    if ((ts->mFlags & kAudioTimeStampSampleTimeValid) != 0) {
      if (self->lastSampleTime_ >= 0.0 &&
          ts->mSampleTime - self->lastSampleTime_ > static_cast<double>(frames) + 1.0) {
        self->xruns_.fetch_add(1, std::memory_order_relaxed);
      }
      self->lastSampleTime_ = ts->mSampleTime;
    }

    // Lấy micro từ bus 1 vào buffer cấp phát sẵn.
    float* in = self->inBuf_.data();
    bool haveInput = false;
    if (self->micAllowed_) {
      AudioBufferList abl;
      abl.mNumberBuffers = 1;
      abl.mBuffers[0].mNumberChannels = 1;
      abl.mBuffers[0].mDataByteSize = frames * sizeof(float);
      abl.mBuffers[0].mData = in;
      haveInput = AudioUnitRender(self->unit_, flags, ts, 1, frames, &abl) == noErr;
    }
    if (!haveInput) std::fill(in, in + frames, 0.0f);

    self->host_.processAudio(in, 1, out, 2, frames);
    return noErr;
  }

  void addObservers() {
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    AVAudioSession* s = [AVAudioSession sharedInstance];
    BackendHost* host = &host_;
    RemoteIOBackend* self = this;

    routeObserver_ = [nc addObserverForName:AVAudioSessionRouteChangeNotification
                                     object:s
                                      queue:nil
                                 usingBlock:^(NSNotification* /*note*/) {
                                   host->onRouteChanged(routeFromSession(s));
                                 }];

    interruptObserver_ =
        [nc addObserverForName:AVAudioSessionInterruptionNotification
                        object:s
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                      const auto type = static_cast<AVAudioSessionInterruptionType>(
                          [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
                      if (type == AVAudioSessionInterruptionTypeBegan) {
                        host->onInterruption(true);
                      } else {
                        // Hết bị ngắt (cuộc gọi kết thúc) → bật lại session và unit.
                        std::lock_guard<std::mutex> lock(self->mutex_);
                        [s setActive:YES error:nil];
                        tc_result r = TC_ERR_DEVICE;
                        if (self->unit_ != nullptr && AudioOutputUnitStart(self->unit_) == noErr) {
                          r = TC_OK;
                        }
                        host->onInterruption(false);
                        host->onStreamRestarted(r);
                      }
                    }];

    resetObserver_ = [nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                                     object:s
                                      queue:nil
                                 usingBlock:^(NSNotification* /*note*/) {
                                   // Dịch vụ âm thanh của iOS khởi động lại: dựng lại toàn bộ.
                                   std::lock_guard<std::mutex> lock(self->mutex_);
                                   self->disposeUnit();
                                   tc_result r = self->configureSession();
                                   if (r == TC_OK) r = self->createUnit();
                                   host->onStreamRestarted(r);
                                 }];
  }

  void removeObservers() {
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    if (routeObserver_ != nil) [nc removeObserver:routeObserver_];
    if (interruptObserver_ != nil) [nc removeObserver:interruptObserver_];
    if (resetObserver_ != nil) [nc removeObserver:resetObserver_];
    routeObserver_ = nil;
    interruptObserver_ = nil;
    resetObserver_ = nil;
  }

  BackendHost& host_;
  const double sampleRate_;
  std::mutex mutex_;  // bảo vệ tạo/huỷ unit (không dùng trong callback)
  AudioComponentInstance unit_ = nullptr;
  std::vector<float> inBuf_;
  std::atomic<bool> micAllowed_{true};
  double lastSampleTime_ = -1.0;  // chỉ audio thread
  std::atomic<uint32_t> xruns_{0};
  id routeObserver_ = nil;
  id interruptObserver_ = nil;
  id resetObserver_ = nil;
};

}  // namespace

std::unique_ptr<AudioBackend> createPlatformBackend(BackendHost& host, const EngineConfig& config) {
  return std::make_unique<RemoteIOBackend>(host, config);
}

}  // namespace tonecore
