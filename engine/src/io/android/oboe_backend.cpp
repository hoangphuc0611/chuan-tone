// Backend âm thanh Android dùng Oboe (Apache-2.0): AAudio khi có (Android 8.1+), tự lùi về
// OpenSL ES trên máy cũ.
//
// Cấu hình theo CLAUDE.md mục 3.1 và ADR-002:
//  - Full-duplex bằng oboe::FullDuplexStream: callback của stream ra (loa/tai nghe) đọc
//    non-blocking từ stream vào (micro) → một callback xử lý cả hai, độ trễ thấp nhất.
//  - PerformanceMode::LowLatency + SharingMode::Exclusive (đường MMAP nếu máy hỗ trợ).
//  - 48 kHz; nếu phần cứng khác, Oboe tự đổi tần số (chất lượng Medium).
//  - Micro mono float, input preset VoicePerformance (Android 10+: tắt AGC/khử ồn, đường nhanh);
//    máy cũ hơn dùng VoiceRecognition (cũng ít xử lý).
//  - Buffer ra = 2 × burst; mỗi khi xảy ra xrun thì tăng thêm 1 burst (tối đa 8 burst).
//  - Bật ADPF performance hint để hệ điều hành không dồn luồng audio sang nhân yếu.
//  - Rút/cắm thiết bị → stream bị ngắt → tự mở lại ở luồng lỗi của Oboe.

#include <android/api-level.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "core/engine.h"
#include "io/audio_backend.h"

namespace tonecore {

namespace {

constexpr int32_t kInitialBursts = 2;
constexpr int32_t kMaxBursts = 8;
constexpr int32_t kOutputChannels = 2;

tc_result toResult(oboe::Result r) {
  switch (r) {
    case oboe::Result::OK:
      return TC_OK;
    case oboe::Result::ErrorNoMemory:
      return TC_ERR_OUT_OF_MEMORY;
    case oboe::Result::ErrorInvalidState:
      return TC_ERR_INVALID_STATE;
    case oboe::Result::ErrorUnimplemented:
      return TC_ERR_NOT_SUPPORTED;
    default:
      return TC_ERR_DEVICE;
  }
}

class OboeBackend;

// FullDuplexStream có start()/stop() riêng trùng tên với AudioBackend → tách thành lớp nhỏ,
// chỉ chuyển callback về OboeBackend.
class DuplexStream final : public oboe::FullDuplexStream {
 public:
  explicit DuplexStream(OboeBackend& owner) : owner_(owner) {}
  oboe::DataCallbackResult onBothStreamsReady(const void* inputData, int numInputFrames,
                                              void* outputData, int numOutputFrames) override;

 private:
  OboeBackend& owner_;
};

class OboeBackend final : public AudioBackend, public oboe::AudioStreamErrorCallback {
 public:
  OboeBackend(BackendHost& host, const EngineConfig& config)
      : host_(host), sampleRate_(static_cast<int32_t>(config.sampleRate)) {}

  ~OboeBackend() override { stop(); }

  tc_result start() override {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = false;
    return openAndStartLocked();
  }

  void stop() override {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    closeLocked();
  }

  BackendInfo info() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    BackendInfo i;
    if (!output_ || !input_) return i;
    i.sampleRate = static_cast<uint32_t>(output_->getSampleRate());
    i.bufferFrames = static_cast<uint32_t>(output_->getBufferSizeInFrames());
    i.burstFrames = static_cast<uint32_t>(output_->getFramesPerBurst());
    i.inputChannels = static_cast<uint32_t>(input_->getChannelCount());
    i.outputChannels = static_cast<uint32_t>(output_->getChannelCount());
    // calculateLatencyMillis dựa trên timestamp – chỉ là ước lượng của hệ điều hành.
    if (auto l = output_->calculateLatencyMillis(); l) i.outputLatencyMs = l.value();
    if (auto l = input_->calculateLatencyMillis(); l) i.inputLatencyMs = l.value();
    i.api = output_->getAudioApi() == oboe::AudioApi::AAudio ? TC_AUDIO_API_AAUDIO
                                                             : TC_AUDIO_API_OPENSLES;
    i.route = TC_ROUTE_UNKNOWN;  // Kotlin báo qua tc_set_route_hint
    return i;
  }

  uint32_t xruns() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return xrunsBase_ + currentXruns_.load(std::memory_order_relaxed);
  }

  // [RT] Callback chính – cả micro và loa đã sẵn sàng.
  oboe::DataCallbackResult onAudio(const void* inputData, int numInputFrames, void* outputData,
                                   int numOutputFrames) noexcept {
    const auto frames = static_cast<uint32_t>(numOutputFrames);
    float* in = inBuf_.data();
    if (frames > inBuf_.size()) {
      // Không bao giờ xảy ra (buffer cấp theo dung lượng stream); phòng hờ thì xuất im lặng.
      return oboe::DataCallbackResult::Continue;
    }
    // Micro có thể trả ít frame hơn (vd. lúc mới khởi động) → đệm 0.
    const auto got = static_cast<uint32_t>(std::max(0, std::min(numInputFrames, numOutputFrames)));
    const auto* src = static_cast<const float*>(inputData);
    std::copy(src, src + got, in);
    std::fill(in + got, in + frames, 0.0f);

    host_.processAudio(in, 1, static_cast<float*>(outputData), kOutputChannels, frames);
    tuneBufferOnXrun();
    return oboe::DataCallbackResult::Continue;
  }

  // Luồng riêng của Oboe gọi sau khi stream lỗi đã bị đóng (vd. rút tai nghe).
  void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    // Cả stream vào và ra đều có thể báo lỗi cho cùng một lần ngắt → chỉ xử lý lỗi của stream
    // hiện hành, bỏ qua báo lỗi muộn của stream đã được thay.
    if (stream != output_.get() && stream != input_.get()) return;
    if (stopping_ || error != oboe::Result::ErrorDisconnected) {
      if (!stopping_) host_.onStreamRestarted(toResult(error));
      return;
    }
    closeLocked();
    const tc_result r = openAndStartLocked();
    host_.onStreamRestarted(r);
  }

 private:
  // [RT] Gặp xrun mới → tăng buffer 1 burst (đánh đổi thêm ~1 burst độ trễ lấy sự ổn định).
  void tuneBufferOnXrun() noexcept {
    if (!output_) return;
    auto x = output_->getXRunCount();
    if (!x) return;
    const auto count = static_cast<uint32_t>(x.value());
    if (count > lastXruns_) {
      lastXruns_ = count;
      const int32_t burst = output_->getFramesPerBurst();
      const int32_t current = output_->getBufferSizeInFrames();
      const int32_t target = std::min(current + burst, burst * kMaxBursts);
      if (target > current) output_->setBufferSizeInFrames(target);
    }
    currentXruns_.store(count, std::memory_order_relaxed);
  }

  tc_result openAndStartLocked() {
    // ---- Stream ra (điều khiển callback) ----
    oboe::AudioStreamBuilder out;
    out.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(kOutputChannels)
        ->setSampleRate(sampleRate_)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setFormatConversionAllowed(true)
        ->setChannelConversionAllowed(true)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(&duplex_)
        ->setErrorCallback(this);
    oboe::Result r = out.openStream(output_);
    if (r != oboe::Result::OK) return toResult(r);

    // ---- Stream vào (micro): đọc non-blocking trong callback của stream ra ----
    const oboe::InputPreset preset = android_get_device_api_level() >= 29
                                         ? oboe::InputPreset::VoicePerformance
                                         : oboe::InputPreset::VoiceRecognition;
    oboe::AudioStreamBuilder in;
    in.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(1)  // BẮT BUỘC mono: buffer của FullDuplexStream không nhân số kênh
        ->setSampleRate(output_->getSampleRate())
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setFormatConversionAllowed(true)
        ->setChannelConversionAllowed(true)
        ->setInputPreset(preset)
        ->setBufferCapacityInFrames(output_->getBufferCapacityInFrames() * 2)
        ->setErrorCallback(this);
    r = in.openStream(input_);
    if (r != oboe::Result::OK) {
      closeLocked();
      return toResult(r);
    }

    const int32_t burst = output_->getFramesPerBurst();
    output_->setBufferSizeInFrames(burst * kInitialBursts);
    output_->setPerformanceHintEnabled(true);

    // Buffer micro cấp phát trước, đủ cho callback lớn nhất có thể.
    inBuf_.assign(static_cast<std::size_t>(output_->getBufferCapacityInFrames()), 0.0f);
    lastXruns_ = 0;
    currentXruns_.store(0, std::memory_order_relaxed);

    duplex_.setSharedInputStream(input_);
    duplex_.setSharedOutputStream(output_);
    r = duplex_.start();
    if (r != oboe::Result::OK) {
      closeLocked();
      return toResult(r);
    }
    return TC_OK;
  }

  void closeLocked() {
    if (output_ || input_) {
      xrunsBase_ += currentXruns_.load(std::memory_order_relaxed);
      currentXruns_.store(0, std::memory_order_relaxed);
    }
    if (output_) {
      output_->requestStop();
      output_->close();
    }
    if (input_) {
      input_->requestStop();
      input_->close();
    }
    output_.reset();
    input_.reset();
  }

  BackendHost& host_;
  const int32_t sampleRate_;
  DuplexStream duplex_{*this};
  mutable std::mutex mutex_;  // bảo vệ mở/đóng stream (không dùng trong callback)
  bool stopping_ = false;
  std::shared_ptr<oboe::AudioStream> output_;
  std::shared_ptr<oboe::AudioStream> input_;
  std::vector<float> inBuf_;
  uint32_t lastXruns_ = 0;  // chỉ audio thread
  std::atomic<uint32_t> currentXruns_{0};
  uint32_t xrunsBase_ = 0;  // xrun của các stream đã đóng (khi mở lại)
};

oboe::DataCallbackResult DuplexStream::onBothStreamsReady(const void* inputData, int numInputFrames,
                                                          void* outputData, int numOutputFrames) {
  return owner_.onAudio(inputData, numInputFrames, outputData, numOutputFrames);
}

}  // namespace

std::unique_ptr<AudioBackend> createPlatformBackend(BackendHost& host, const EngineConfig& config) {
  return std::make_unique<OboeBackend>(host, config);
}

}  // namespace tonecore
