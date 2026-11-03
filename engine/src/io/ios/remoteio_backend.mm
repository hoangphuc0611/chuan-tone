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
#include <mach/mach_time.h>

#include <algorithm>
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

// Micro đang dùng (cổng vào đầu tiên của route hiện tại).
int32_t inputSourceFromSession(AVAudioSession* session) {
  AVAudioSessionRouteDescription* route = session.currentRoute;
  if (route.inputs.count == 0) return TC_INPUT_AUTO;
  NSString* port = route.inputs.firstObject.portType;
  if ([port isEqualToString:AVAudioSessionPortBuiltInMic]) return TC_INPUT_BUILTIN;
  if ([port isEqualToString:AVAudioSessionPortHeadsetMic] ||
      [port isEqualToString:AVAudioSessionPortLineIn]) {
    return TC_INPUT_HEADSET;
  }
  if ([port isEqualToString:AVAudioSessionPortUSBAudio]) return TC_INPUT_USB;
  if ([port isEqualToString:AVAudioSessionPortBluetoothHFP] ||
      [port isEqualToString:AVAudioSessionPortBluetoothLE]) {
    return TC_INPUT_BLUETOOTH;
  }
  return TC_INPUT_AUTO;
}

// Loại cổng iOS ứng với lựa chọn micro; nil = để hệ điều hành chọn.
NSString* portTypeForPreference(int32_t preference) {
  switch (preference) {
    case TC_INPUT_BUILTIN:
      return AVAudioSessionPortBuiltInMic;
    case TC_INPUT_HEADSET:
      return AVAudioSessionPortHeadsetMic;
    case TC_INPUT_USB:
      return AVAudioSessionPortUSBAudio;
    default:
      return nil;
  }
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
      : host_(host), sampleRate_(config.sampleRate), inBuf_(kMaxFramesPerSlice, 0.0f) {
    mach_timebase_info_data_t tb{};
    if (mach_timebase_info(&tb) == KERN_SUCCESS) {
      timebaseNumer_ = tb.numer;
      timebaseDenom_ = tb.denom;
    }
  }

  ~RemoteIOBackend() override { stop(); }

  tc_result start() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (unit_ != nullptr) return TC_ERR_INVALID_STATE;
    tc_result r = configureSession();
    if (r != TC_OK) return r;
    applyInputPreference();
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
    i.inputSource = inputSourceFromSession(s);
    return i;
  }

  uint32_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }

  // Không giữ mutex_ khi đặt cổng: setPreferredInput sinh thông báo đổi route, khối xử lý thông
  // báo cũng gọi applyInputPreference.
  void setInputPreference(int32_t preference, int32_t /*deviceId*/) override {
    inputPref_.store(preference, std::memory_order_relaxed);
    if (running_.load(std::memory_order_acquire)) applyInputPreference();
  }

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
    switch (s.recordPermission) {
      case AVAudioSessionRecordPermissionGranted:
        micAllowed_ = true;
        break;
      case AVAudioSessionRecordPermissionUndetermined: {
        // Lần đầu: iOS hiện hộp thoại xin quyền; micro bật ngay khi người dùng đồng ý.
        micAllowed_ = false;
        std::atomic<bool>* flag = &micAllowed_;
        [s requestRecordPermission:^(BOOL granted) {
          flag->store(granted == YES, std::memory_order_relaxed);
        }];
        break;
      }
      default:
        // Bị từ chối: vẫn chạy (phát beat được) nhưng micro im lặng.
        micAllowed_ = false;
        break;
    }
    return TC_OK;
  }

  // Chọn cổng micro theo lựa chọn (session phải đang bật). Cổng đó không có (chưa cắm) hoặc chọn
  // "Tự động" → để hệ điều hành chọn. Gọi lại mỗi lần đổi route: cắm soundcard / tai nghe vào sau
  // vẫn được chọn. Đã đúng thì không đặt lại (đặt lại cũng sinh thông báo đổi route).
  void applyInputPreference() {
    AVAudioSession* s = [AVAudioSession sharedInstance];
    NSString* want = portTypeForPreference(inputPref_.load(std::memory_order_relaxed));
    AVAudioSessionPortDescription* port = nil;
    if (want != nil) {
      for (AVAudioSessionPortDescription* p in s.availableInputs) {
        if ([p.portType isEqualToString:want]) {
          port = p;
          break;
        }
      }
    }
    AVAudioSessionPortDescription* current = s.preferredInput;
    if (port == nil && current == nil) return;
    if (port != nil && current != nil && [port.UID isEqualToString:current.UID]) return;
    [s setPreferredInput:port error:nil];
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
    running_.store(true, std::memory_order_release);
    return TC_OK;
  }

  void disposeUnit() {
    running_.store(false, std::memory_order_release);
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

    // Đồng hồ máy (mach_absolute_time) lúc buffer này ra thiết bị → ns, để căn video camera.
    int64_t hostNs = 0;
    if ((ts->mFlags & kAudioTimeStampHostTimeValid) != 0 && self->timebaseDenom_ != 0) {
      const uint64_t t = ts->mHostTime;
      const uint64_t n = self->timebaseNumer_;
      const uint64_t d = self->timebaseDenom_;
      hostNs = static_cast<int64_t>(t / d * n + t % d * n / d);
    }
    self->host_.processAudio(in, 1, out, 2, frames, hostNs);
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
                                   if (self->running_.load(std::memory_order_acquire)) {
                                     self->applyInputPreference();
                                   }
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
  std::atomic<int32_t> inputPref_{TC_INPUT_AUTO};
  std::atomic<bool> running_{false};  // unit đang chạy (đọc được ngoài mutex_)
  double lastSampleTime_ = -1.0;      // chỉ audio thread
  uint32_t timebaseNumer_ = 0;        // mach_absolute_time → ns (đặt một lần lúc tạo)
  uint32_t timebaseDenom_ = 0;
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
