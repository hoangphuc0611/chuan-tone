// Giải mã file trên iOS/macOS bằng ExtAudioFile (AudioToolbox – codec của hệ điều hành,
// hỗ trợ MP3, AAC/M4A, WAV, AIFF, FLAC, ALAC...). API C thuần nên không cần Objective-C.

#include <AudioToolbox/ExtendedAudioFile.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>

#include "decode/decoder.h"

namespace tonecore {

namespace {

struct FileGuard {
  ExtAudioFileRef file = nullptr;
  ~FileGuard() {
    if (file != nullptr) ExtAudioFileDispose(file);
  }
};

}  // namespace

tc_result decodeFile(const std::string& path, DecodedAudio& out) {
  out = DecodedAudio{};

  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.c_str()),
      static_cast<CFIndex>(path.size()), false);
  if (url == nullptr) return TC_ERR_INVALID_ARG;

  FileGuard guard;
  const OSStatus openStatus = ExtAudioFileOpenURL(url, &guard.file);
  CFRelease(url);
  if (openStatus != noErr) return TC_ERR_INVALID_ARG;

  AudioStreamBasicDescription fileFormat{};
  UInt32 size = sizeof(fileFormat);
  if (ExtAudioFileGetProperty(guard.file, kExtAudioFileProperty_FileDataFormat, &size,
                              &fileFormat) != noErr ||
      fileFormat.mSampleRate <= 0.0 || fileFormat.mChannelsPerFrame == 0) {
    return TC_ERR_NOT_SUPPORTED;
  }

  SInt64 fileFrames = 0;
  size = sizeof(fileFrames);
  ExtAudioFileGetProperty(guard.file, kExtAudioFileProperty_FileLengthFrames, &size, &fileFrames);
  if (static_cast<double>(fileFrames) / fileFormat.mSampleRate > kMaxBeatSeconds) {
    return TC_ERR_NOT_SUPPORTED;
  }

  // Định dạng ra: PCM int16 interleaved, giữ tần số gốc (resample bằng Resampler của engine
  // để chất lượng đồng nhất mọi nền tảng), tối đa 2 kênh.
  const UInt32 channels = std::min<UInt32>(fileFormat.mChannelsPerFrame, 2);
  AudioStreamBasicDescription client{};
  client.mSampleRate = fileFormat.mSampleRate;
  client.mFormatID = kAudioFormatLinearPCM;
  client.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  client.mBitsPerChannel = 16;
  client.mChannelsPerFrame = channels;
  client.mFramesPerPacket = 1;
  client.mBytesPerFrame = 2 * channels;
  client.mBytesPerPacket = client.mBytesPerFrame;
  if (ExtAudioFileSetProperty(guard.file, kExtAudioFileProperty_ClientDataFormat, sizeof(client),
                              &client) != noErr) {
    return TC_ERR_NOT_SUPPORTED;
  }

  out.sampleRate = static_cast<uint32_t>(fileFormat.mSampleRate);
  out.channels = channels;
  if (fileFrames > 0) out.samples.reserve(static_cast<std::size_t>(fileFrames) * channels);

  const auto maxFrames = static_cast<uint64_t>(kMaxBeatSeconds * fileFormat.mSampleRate);
  constexpr UInt32 kChunkFrames = 32768;
  std::vector<int16_t> chunk(kChunkFrames * channels);
  for (;;) {
    AudioBufferList abl;
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = channels;
    abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(chunk.size() * sizeof(int16_t));
    abl.mBuffers[0].mData = chunk.data();
    UInt32 frames = kChunkFrames;
    if (ExtAudioFileRead(guard.file, &frames, &abl) != noErr) return TC_ERR_NOT_SUPPORTED;
    if (frames == 0) break;
    out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + frames * channels);
    out.frames += frames;
    if (out.frames > maxFrames) return TC_ERR_NOT_SUPPORTED;
  }
  return out.frames > 0 ? TC_OK : TC_ERR_NOT_SUPPORTED;
}

}  // namespace tonecore
