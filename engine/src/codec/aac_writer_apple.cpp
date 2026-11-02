// Ghi M4A (AAC-LC) trên iOS / macOS bằng ExtAudioFile — bộ nén của hệ điều hành, API C thuần.

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "codec/aac_writer.h"

namespace tonecore {

namespace {

class AppleAacWriter final : public AacWriter {
 public:
  ~AppleAacWriter() override { abort(); }

  tc_result open(const std::string& path, uint32_t rate, uint32_t channels, int32_t bitrate) {
    path_ = path;
    channels_ = channels;
    const std::string tmp = path_ + ".part";
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(tmp.c_str()),
        static_cast<CFIndex>(tmp.size()), false);
    if (url == nullptr) return TC_ERR_INVALID_ARG;
    AudioStreamBasicDescription dst{};
    dst.mFormatID = kAudioFormatMPEG4AAC;
    dst.mSampleRate = rate;
    dst.mChannelsPerFrame = channels;
    UInt32 size = sizeof(dst);
    AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, 0, nullptr, &size, &dst);
    const OSStatus st = ExtAudioFileCreateWithURL(url, kAudioFileM4AType, &dst, nullptr,
                                                  kAudioFileFlags_EraseFile, &file_);
    CFRelease(url);
    if (st != noErr) {
      file_ = nullptr;
      return TC_ERR_INVALID_ARG;
    }
    // Dữ liệu đưa vào: float interleaved.
    AudioStreamBasicDescription client{};
    client.mSampleRate = rate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    client.mBitsPerChannel = 32;
    client.mChannelsPerFrame = channels;
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = 4 * channels;
    client.mBytesPerPacket = client.mBytesPerFrame;
    if (ExtAudioFileSetProperty(file_, kExtAudioFileProperty_ClientDataFormat, sizeof(client),
                                &client) != noErr) {
      abort();
      return TC_ERR_NOT_SUPPORTED;
    }
    // Tốc độ bit: đặt trên bộ chuyển đổi bên trong rồi báo ExtAudioFile cấu hình lại.
    AudioConverterRef conv = nullptr;
    size = sizeof(conv);
    if (ExtAudioFileGetProperty(file_, kExtAudioFileProperty_AudioConverter, &size, &conv) ==
            noErr &&
        conv != nullptr) {
      const auto br = static_cast<UInt32>(bitrate);
      if (AudioConverterSetProperty(conv, kAudioConverterEncodeBitRate, sizeof(br), &br) == noErr) {
        CFArrayRef config = nullptr;
        ExtAudioFileSetProperty(file_, kExtAudioFileProperty_ConverterConfig, sizeof(config),
                                &config);
      }
    }
    return TC_OK;
  }

  bool write(const float* x, std::size_t frames) override {
    if (file_ == nullptr || failed_) return false;
    constexpr std::size_t kChunk = 4096;
    for (std::size_t at = 0; at < frames; at += kChunk) {
      const std::size_t n = std::min(kChunk, frames - at);
      buf_.assign(x + at * channels_, x + (at + n) * channels_);
      for (float& v : buf_) v = std::clamp(v, -1.0f, 1.0f);
      AudioBufferList abl;
      abl.mNumberBuffers = 1;
      abl.mBuffers[0].mNumberChannels = channels_;
      abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(buf_.size() * sizeof(float));
      abl.mBuffers[0].mData = buf_.data();
      if (ExtAudioFileWrite(file_, static_cast<UInt32>(n), &abl) != noErr) {
        failed_ = true;
        return false;
      }
    }
    return true;
  }

  bool finish() override {
    if (file_ == nullptr) return false;
    const bool ok = ExtAudioFileDispose(file_) == noErr && !failed_;
    file_ = nullptr;
    std::error_code ec;
    if (ok) {
      std::filesystem::rename(path_ + ".part", path_, ec);
      if (!ec) return true;
    }
    std::filesystem::remove(path_ + ".part", ec);
    return false;
  }

  void abort() override {
    if (file_ == nullptr) return;
    ExtAudioFileDispose(file_);
    file_ = nullptr;
    std::error_code ec;
    std::filesystem::remove(path_ + ".part", ec);
  }

 private:
  ExtAudioFileRef file_ = nullptr;
  std::string path_;
  uint32_t channels_ = 2;
  bool failed_ = false;
  std::vector<float> buf_;
};

}  // namespace

std::unique_ptr<AacWriter> createAacWriter(const std::string& path, uint32_t sampleRate,
                                           uint32_t channels, int32_t bitrate, tc_result& err) {
  auto w = std::make_unique<AppleAacWriter>();
  err = w->open(path, sampleRate, channels, bitrate);
  if (err != TC_OK) return nullptr;
  return w;
}

}  // namespace tonecore
