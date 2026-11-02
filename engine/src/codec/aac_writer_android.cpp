// Ghi M4A (AAC-LC) trên Android bằng NDK AMediaCodec (bộ nén của hệ điều hành) + AMediaMuxer.
// Build trong CI; chưa thử trên máy Android thật (M7 nghiệm thu trên iPhone — docs/m7-plan.md).

#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include "codec/aac_writer.h"

namespace tonecore {

namespace {

constexpr int64_t kTimeoutUs = 10000;
constexpr int kAacProfileLc = 2;

class AndroidAacWriter final : public AacWriter {
 public:
  ~AndroidAacWriter() override { abort(); }

  tc_result open(const std::string& path, uint32_t rate, uint32_t channels, int32_t bitrate) {
    path_ = path;
    rate_ = rate;
    channels_ = channels;
    fd_ = ::open((path_ + ".part").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd_ < 0) return TC_ERR_INVALID_ARG;
    muxer_ = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    codec_ = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (muxer_ == nullptr || codec_ == nullptr) {
      abort();
      return TC_ERR_NOT_SUPPORTED;
    }
    AMediaFormat* f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, static_cast<int32_t>(rate));
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, static_cast<int32_t>(channels));
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_AAC_PROFILE, kAacProfileLc);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
    const media_status_t st =
        AMediaCodec_configure(codec_, f, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(f);
    if (st != AMEDIA_OK || AMediaCodec_start(codec_) != AMEDIA_OK) {
      abort();
      return TC_ERR_NOT_SUPPORTED;
    }
    codecStarted_ = true;
    return TC_OK;
  }

  bool write(const float* x, std::size_t frames) override {
    if (codec_ == nullptr || failed_) return false;
    pcm_.resize(frames * channels_);
    for (std::size_t i = 0; i < pcm_.size(); ++i) {
      const float v = std::clamp(x[i], -1.0f, 1.0f);
      pcm_[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
    }
    return feed(pcm_.data(), frames, false);
  }

  bool finish() override {
    if (codec_ == nullptr) return false;
    bool ok = !failed_ && feed(nullptr, 0, true) && drain(true);
    ok = closeAll() && ok && muxerStarted_;
    std::error_code ec;
    if (ok) {
      std::filesystem::rename(path_ + ".part", path_, ec);
      if (!ec) return true;
    }
    std::filesystem::remove(path_ + ".part", ec);
    return false;
  }

  void abort() override {
    if (codec_ == nullptr && muxer_ == nullptr && fd_ < 0) return;
    closeAll();
    std::error_code ec;
    std::filesystem::remove(path_ + ".part", ec);
  }

 private:
  // Đưa PCM vào bộ nén (eos = báo hết dữ liệu), rút dữ liệu đã nén ra muxer xen kẽ.
  bool feed(const int16_t* pcm, std::size_t frames, bool eos) {
    std::size_t off = 0;
    int idle = 0;
    while (off < frames || eos) {
      const ssize_t idx = AMediaCodec_dequeueInputBuffer(codec_, kTimeoutUs);
      if (idx >= 0) {
        std::size_t cap = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(idx), &cap);
        if (buf == nullptr) return fail();
        const std::size_t bytesPerFrame = 2 * channels_;
        const std::size_t n = std::min(frames - off, cap / bytesPerFrame);
        if (n > 0) std::memcpy(buf, pcm + off * channels_, n * bytesPerFrame);
        const auto pts = static_cast<uint64_t>(framesIn_ * 1000000 / rate_);
        framesIn_ += n;
        off += n;
        const bool last = eos && off >= frames;
        if (AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(idx), 0, n * bytesPerFrame,
                                         pts, last ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0) !=
            AMEDIA_OK) {
          return fail();
        }
        if (last) return true;
        idle = 0;
      } else if (++idle > 500) {
        return fail();  // bộ nén treo (~5 s không nhận dữ liệu)
      }
      if (!drain(false)) return false;
    }
    return true;
  }

  // Rút dữ liệu đã nén; untilEos = chờ tới gói cuối.
  bool drain(bool untilEos) {
    int idle = 0;
    for (;;) {
      AMediaCodecBufferInfo info{};
      const ssize_t o = AMediaCodec_dequeueOutputBuffer(codec_, &info, untilEos ? kTimeoutUs : 0);
      if (o >= 0) {
        idle = 0;
        std::size_t cap = 0;
        uint8_t* buf = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(o), &cap);
        const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
        if (buf != nullptr && info.size > 0 && !config) {
          if (!muxerStarted_) return fail();
          if (AMediaMuxer_writeSampleData(muxer_, track_, buf, &info) != AMEDIA_OK) return fail();
        }
        AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(o), false);
        if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) return true;
      } else if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* f = AMediaCodec_getOutputFormat(codec_);
        const ssize_t t = AMediaMuxer_addTrack(muxer_, f);
        AMediaFormat_delete(f);
        if (t < 0 || AMediaMuxer_start(muxer_) != AMEDIA_OK) return fail();
        track_ = static_cast<size_t>(t);
        muxerStarted_ = true;
      } else if (o == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        if (!untilEos) return true;
        if (++idle > 500) return fail();
      }
      // AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED: hỏi lại.
    }
  }

  bool fail() {
    failed_ = true;
    return false;
  }

  bool closeAll() {
    bool ok = true;
    if (codec_ != nullptr) {
      if (codecStarted_) AMediaCodec_stop(codec_);
      AMediaCodec_delete(codec_);
      codec_ = nullptr;
    }
    if (muxer_ != nullptr) {
      if (muxerStarted_ && AMediaMuxer_stop(muxer_) != AMEDIA_OK) ok = false;
      AMediaMuxer_delete(muxer_);
      muxer_ = nullptr;
    }
    if (fd_ >= 0) {
      if (::close(fd_) != 0) ok = false;
      fd_ = -1;
    }
    return ok;
  }

  std::string path_;
  uint32_t rate_ = 48000;
  uint32_t channels_ = 2;
  int fd_ = -1;
  AMediaMuxer* muxer_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  bool codecStarted_ = false;
  bool muxerStarted_ = false;
  size_t track_ = 0;
  uint64_t framesIn_ = 0;
  bool failed_ = false;
  std::vector<int16_t> pcm_;
};

}  // namespace

std::unique_ptr<AacWriter> createAacWriter(const std::string& path, uint32_t sampleRate,
                                           uint32_t channels, int32_t bitrate, tc_result& err) {
  auto w = std::make_unique<AndroidAacWriter>();
  err = w->open(path, sampleRate, channels, bitrate);
  if (err != TC_OK) return nullptr;
  return w;
}

}  // namespace tonecore
