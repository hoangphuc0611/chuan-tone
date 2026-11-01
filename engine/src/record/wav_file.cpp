#include "record/wav_file.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace tonecore {

namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

void putU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void putU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t getU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

tc_result WavWriter::open(const std::string& path, uint32_t channels, uint32_t sampleRate,
                          WavFormat format) {
  abort();
  if (path.empty() || channels == 0 || channels > 8 || sampleRate == 0) return TC_ERR_INVALID_ARG;
  path_ = path;
  channels_ = channels;
  sampleRate_ = sampleRate;
  format_ = format;
  frames_ = 0;
  failed_ = false;
  file_ = std::fopen((path_ + ".part").c_str(), "wb");
  if (file_ == nullptr) return TC_ERR_INVALID_ARG;
  std::setvbuf(file_, nullptr, _IOFBF, 1 << 16);
  if (!writeHeader()) {
    abort();
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

// Header cố định: float 58 byte (fmt 18 + fact), PCM 44 byte. Gọi lúc mở (độ dài 0) và lúc
// finish (độ dài thật).
bool WavWriter::writeHeader() {
  const bool isFloat = format_ == WavFormat::Float32;
  const uint32_t bytesPerSample = isFloat ? 4 : 2;
  const uint64_t data64 = frames_ * channels_ * bytesPerSample;
  const auto dataBytes = static_cast<uint32_t>(std::min<uint64_t>(data64, 0xFFFFFFFFull - 64));
  uint8_t h[58];
  std::size_t n = 0;
  std::memcpy(h, "RIFF", 4);
  std::memcpy(h + 8, "WAVEfmt ", 8);
  const uint32_t fmtSize = isFloat ? 18 : 16;
  putU32(h + 16, fmtSize);
  putU16(h + 20, isFloat ? kFormatFloat : kFormatPcm);
  putU16(h + 22, static_cast<uint16_t>(channels_));
  putU32(h + 24, sampleRate_);
  putU32(h + 28, sampleRate_ * channels_ * bytesPerSample);
  putU16(h + 32, static_cast<uint16_t>(channels_ * bytesPerSample));
  putU16(h + 34, static_cast<uint16_t>(bytesPerSample * 8));
  n = 36;
  if (isFloat) {
    putU16(h + 36, 0);  // cbSize
    std::memcpy(h + 38, "fact", 4);
    putU32(h + 42, 4);
    putU32(h + 46, static_cast<uint32_t>(std::min<uint64_t>(frames_, 0xFFFFFFFFull)));
    n = 50;
  }
  std::memcpy(h + n, "data", 4);
  putU32(h + n + 4, dataBytes);
  n += 8;
  putU32(h + 4, static_cast<uint32_t>(n - 8 + dataBytes));
  return std::fseek(file_, 0, SEEK_SET) == 0 && std::fwrite(h, 1, n, file_) == n;
}

bool WavWriter::write(const float* interleaved, std::size_t frames) {
  if (file_ == nullptr || failed_) return false;
  const std::size_t count = frames * channels_;
  if (format_ == WavFormat::Float32) {
    // Máy đích (ARM, x86) đều little-endian như WAV.
    if (std::fwrite(interleaved, sizeof(float), count, file_) != count) failed_ = true;
  } else {
    scratch_.resize(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
      const float x = std::clamp(interleaved[i], -1.0f, 1.0f);
      const auto v = static_cast<int16_t>(std::lrint(x * 32767.0f));
      putU16(scratch_.data() + 2 * i, static_cast<uint16_t>(v));
    }
    if (std::fwrite(scratch_.data(), 1, scratch_.size(), file_) != scratch_.size()) {
      failed_ = true;
    }
  }
  if (!failed_) frames_ += frames;
  return !failed_;
}

bool WavWriter::finish() {
  if (file_ == nullptr) return false;
  bool ok = !failed_ && std::fflush(file_) == 0 && writeHeader();
  ok = std::fclose(file_) == 0 && ok;
  file_ = nullptr;
  std::error_code ec;
  if (ok) {
    std::filesystem::rename(path_ + ".part", path_, ec);
    ok = !ec;
  }
  if (!ok) std::filesystem::remove(path_ + ".part", ec);
  return ok;
}

void WavWriter::abort() {
  if (file_ == nullptr) return;
  std::fclose(file_);
  file_ = nullptr;
  std::error_code ec;
  std::filesystem::remove(path_ + ".part", ec);
}

tc_result readWav(const std::string& path, WavData& out) {
  out = WavData{};
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return TC_ERR_INVALID_ARG;
  std::vector<uint8_t> bytes;
  {
    uint8_t buf[1 << 16];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
  }
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    return TC_ERR_NOT_SUPPORTED;
  }
  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t rate = 0;
  const uint8_t* data = nullptr;
  std::size_t dataBytes = 0;
  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const uint8_t* c = bytes.data() + pos;
    const std::size_t size = getU32(c + 4);
    const std::size_t avail = std::min(size, bytes.size() - pos - 8);
    if (std::memcmp(c, "fmt ", 4) == 0 && avail >= 16) {
      format = getU16(c + 8);
      channels = getU16(c + 10);
      rate = getU32(c + 12);
      bits = getU16(c + 22);
      if (format == kFormatExtensible && avail >= 26) format = getU16(c + 32);
    } else if (std::memcmp(c, "data", 4) == 0) {
      data = c + 8;
      dataBytes = avail;  // file đang ghi dở / header chưa sửa: lấy phần có thật
      break;
    }
    pos += 8 + size + (size & 1);
  }
  if (data == nullptr || channels == 0 || rate == 0) return TC_ERR_NOT_SUPPORTED;
  const bool isFloat = format == kFormatFloat && bits == 32;
  const bool isPcm = format == kFormatPcm && (bits == 16 || bits == 24 || bits == 32);
  if (!isFloat && !isPcm) return TC_ERR_NOT_SUPPORTED;
  const std::size_t bytesPerSample = bits / 8;
  const std::size_t count = dataBytes / bytesPerSample / channels * channels;
  out.samples.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const uint8_t* p = data + i * bytesPerSample;
    float v;
    if (isFloat) {
      std::memcpy(&v, p, 4);
    } else if (bits == 16) {
      v = static_cast<float>(static_cast<int16_t>(getU16(p))) / 32768.0f;
    } else if (bits == 24) {
      const int32_t s = static_cast<int32_t>((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8;
      v = static_cast<float>(s) / 8388608.0f;
    } else {
      v = static_cast<float>(static_cast<double>(static_cast<int32_t>(getU32(p))) / 2147483648.0);
    }
    out.samples[i] = v;
  }
  out.sampleRate = rate;
  out.channels = channels;
  out.frames = count / channels;
  return TC_OK;
}

}  // namespace tonecore
