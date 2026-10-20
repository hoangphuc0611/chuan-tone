// Giải mã file trên Android bằng NDK Media (AMediaExtractor + AMediaCodec) – codec của hệ điều
// hành, hỗ trợ MP3, AAC/M4A, WAV, FLAC, OGG/Opus tùy máy.

#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "decode/decoder.h"

namespace tonecore {

namespace {

constexpr int64_t kDequeueTimeoutUs = 10000;
constexpr int32_t kEncodingPcm16 = 2;  // AudioFormat.ENCODING_PCM_16BIT
constexpr int32_t kEncodingFloat = 4;  // AudioFormat.ENCODING_PCM_FLOAT

struct Resources {
  int fd = -1;
  AMediaExtractor* extractor = nullptr;
  AMediaCodec* codec = nullptr;
  AMediaFormat* format = nullptr;
  ~Resources() {
    if (codec != nullptr) {
      AMediaCodec_stop(codec);
      AMediaCodec_delete(codec);
    }
    if (format != nullptr) AMediaFormat_delete(format);
    if (extractor != nullptr) AMediaExtractor_delete(extractor);
    if (fd >= 0) close(fd);
  }
};

}  // namespace

tc_result decodeFile(const std::string& path, DecodedAudio& out) {
  out = DecodedAudio{};
  Resources r;

  r.fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (r.fd < 0) return TC_ERR_INVALID_ARG;
  struct stat st{};
  if (fstat(r.fd, &st) != 0) return TC_ERR_INVALID_ARG;

  r.extractor = AMediaExtractor_new();
  if (AMediaExtractor_setDataSourceFd(r.extractor, r.fd, 0, st.st_size) != AMEDIA_OK) {
    return TC_ERR_NOT_SUPPORTED;
  }

  // Chọn track âm thanh đầu tiên.
  const size_t tracks = AMediaExtractor_getTrackCount(r.extractor);
  const char* mime = nullptr;
  for (size_t i = 0; i < tracks; ++i) {
    AMediaFormat* f = AMediaExtractor_getTrackFormat(r.extractor, i);
    const char* m = nullptr;
    if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && m != nullptr &&
        std::strncmp(m, "audio/", 6) == 0) {
      AMediaExtractor_selectTrack(r.extractor, i);
      r.format = f;
      mime = m;
      break;
    }
    AMediaFormat_delete(f);
  }
  if (r.format == nullptr) return TC_ERR_NOT_SUPPORTED;

  int32_t sampleRate = 0;
  int32_t channels = 0;
  AMediaFormat_getInt32(r.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
  AMediaFormat_getInt32(r.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
  if (sampleRate <= 0 || channels <= 0) return TC_ERR_NOT_SUPPORTED;

  int64_t durationUs = 0;
  if (AMediaFormat_getInt64(r.format, AMEDIAFORMAT_KEY_DURATION, &durationUs) &&
      static_cast<double>(durationUs) / 1e6 > kMaxBeatSeconds) {
    return TC_ERR_NOT_SUPPORTED;
  }

  r.codec = AMediaCodec_createDecoderByType(mime);
  if (r.codec == nullptr) return TC_ERR_NOT_SUPPORTED;
  if (AMediaCodec_configure(r.codec, r.format, nullptr, nullptr, 0) != AMEDIA_OK ||
      AMediaCodec_start(r.codec) != AMEDIA_OK) {
    return TC_ERR_NOT_SUPPORTED;
  }

  int32_t encoding = kEncodingPcm16;
  auto outChannels = static_cast<uint32_t>(std::min(channels, 2));
  out.channels = outChannels;
  out.sampleRate = static_cast<uint32_t>(sampleRate);
  if (durationUs > 0) {
    out.samples.reserve(static_cast<size_t>(durationUs / 1e6 * sampleRate) * outChannels);
  }
  const auto maxFrames = static_cast<uint64_t>(kMaxBeatSeconds * sampleRate);

  bool inputDone = false;
  bool outputDone = false;
  int idleLoops = 0;
  while (!outputDone) {
    if (!inputDone) {
      const ssize_t inIdx = AMediaCodec_dequeueInputBuffer(r.codec, kDequeueTimeoutUs);
      if (inIdx >= 0) {
        size_t capacity = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(r.codec, static_cast<size_t>(inIdx), &capacity);
        const ssize_t n = AMediaExtractor_readSampleData(r.extractor, buf, capacity);
        if (n < 0) {
          AMediaCodec_queueInputBuffer(r.codec, static_cast<size_t>(inIdx), 0, 0, 0,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          inputDone = true;
        } else {
          const int64_t pts = AMediaExtractor_getSampleTime(r.extractor);
          AMediaCodec_queueInputBuffer(r.codec, static_cast<size_t>(inIdx), 0,
                                       static_cast<size_t>(n), static_cast<uint64_t>(pts), 0);
          AMediaExtractor_advance(r.extractor);
        }
      }
    }

    AMediaCodecBufferInfo info{};
    const ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(r.codec, &info, kDequeueTimeoutUs);
    if (outIdx >= 0) {
      idleLoops = 0;
      size_t size = 0;
      const uint8_t* data =
          AMediaCodec_getOutputBuffer(r.codec, static_cast<size_t>(outIdx), &size);
      if (data != nullptr && info.size > 0) {
        const uint8_t* p = data + info.offset;
        const auto cch = static_cast<size_t>(channels);
        if (encoding == kEncodingFloat) {
          const size_t frames = static_cast<size_t>(info.size) / (sizeof(float) * cch);
          const auto* f = reinterpret_cast<const float*>(p);
          for (size_t i = 0; i < frames; ++i) {
            for (uint32_t c = 0; c < outChannels; ++c) {
              const float v = std::clamp(f[i * cch + c], -1.0f, 1.0f);
              out.samples.push_back(static_cast<int16_t>(std::lrint(v * 32767.0f)));
            }
          }
          out.frames += frames;
        } else {
          const size_t frames = static_cast<size_t>(info.size) / (sizeof(int16_t) * cch);
          const auto* s = reinterpret_cast<const int16_t*>(p);
          for (size_t i = 0; i < frames; ++i) {
            for (uint32_t c = 0; c < outChannels; ++c) out.samples.push_back(s[i * cch + c]);
          }
          out.frames += frames;
        }
      }
      AMediaCodec_releaseOutputBuffer(r.codec, static_cast<size_t>(outIdx), false);
      if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) outputDone = true;
      if (out.frames > maxFrames) return TC_ERR_NOT_SUPPORTED;
    } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      // Codec báo định dạng thật của PCM đầu ra (tần số/kênh có thể khác metadata).
      AMediaFormat* f = AMediaCodec_getOutputFormat(r.codec);
      int32_t v = 0;
      if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &v) && v > 0) {
        out.sampleRate = static_cast<uint32_t>(v);
      }
      if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v) && v > 0 &&
          out.frames == 0) {
        channels = v;
        outChannels = static_cast<uint32_t>(std::min(v, 2));
        out.channels = outChannels;
      }
      if (AMediaFormat_getInt32(f, "pcm-encoding", &v)) encoding = v;
      AMediaFormat_delete(f);
    } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER && inputDone) {
      // Phòng codec lỗi không bao giờ trả EOS.
      if (++idleLoops > 500) break;
    }
  }

  return out.frames > 0 ? TC_OK : TC_ERR_NOT_SUPPORTED;
}

}  // namespace tonecore
