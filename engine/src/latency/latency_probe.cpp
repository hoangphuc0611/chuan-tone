#include "latency/latency_probe.h"

#include <algorithm>
#include <cmath>

#include "latency/mls.h"

namespace tonecore {

namespace {

// Ngưỡng độ tin cậy để coi một lần đo là hợp lệ.
constexpr float kMinConfidence = 0.25f;
// Vùng loại trừ quanh đỉnh chính khi tìm đỉnh phụ (giây) — tránh lấy chính sườn của đỉnh.
constexpr double kPeakExclusionSec = 0.002;
// Các lần đo hợp lệ phải lệch nhau không quá 1 ms.
constexpr double kMaxSpreadSec = 0.001;

// Tích vô hướng với 8 bộ cộng song song để CPU/trình biên dịch tận dụng SIMD.
float dot(const float* a, const float* b, uint32_t n) {
  float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t i = 0;
  for (; i + 8 <= n; i += 8) {
    for (int j = 0; j < 8; ++j) acc[j] += a[i + j] * b[i + j];
  }
  float sum = 0.0f;
  for (float v : acc) sum += v;
  for (; i < n; ++i) sum += a[i] * b[i];
  return sum;
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

LatencyProbe::LatencyProbe(uint32_t sampleRate, const Config& config)
    : sampleRate_(sampleRate),
      config_(config),
      mls_(generateMls(config.mlsOrder)),
      maxLatencyFrames_(static_cast<uint32_t>(config.maxLatencySec * sampleRate)),
      prerollFrames_(static_cast<uint32_t>(config.prerollSec * sampleRate)),
      periodFrames_(static_cast<uint32_t>(mls_.size()) + maxLatencyFrames_),
      totalFrames_(prerollFrames_ + static_cast<uint32_t>(config.repeats) * periodFrames_),
      capture_(totalFrames_, 0.0f) {}

void LatencyProbe::arm() noexcept {
  state_.store(kArmed, std::memory_order_release);
}

void LatencyProbe::cancel() noexcept {
  state_.store(kIdle, std::memory_order_release);
}

// [RT] Không cấp phát, không khóa.
bool LatencyProbe::processBlock(const float* micIn, float* stimulusOut, uint32_t frames) noexcept {
  int s = state_.load(std::memory_order_acquire);
  if (s == kArmed) {
    // Chỉ audio thread reset con trỏ ghi → không tranh chấp với luồng điều khiển.
    cursor_ = 0;
    int expected = kArmed;
    if (state_.compare_exchange_strong(expected, kRunning, std::memory_order_acq_rel)) {
      s = kRunning;
    } else {
      s = expected;  // vừa bị cancel()
    }
  }

  if (s != kRunning) {
    std::fill(stimulusOut, stimulusOut + frames, 0.0f);
    return false;
  }

  const auto mlsLen = static_cast<uint32_t>(mls_.size());
  const auto repeats = static_cast<uint32_t>(config_.repeats);
  for (uint32_t i = 0; i < frames; ++i) {
    const uint32_t pos = cursor_ + i;
    float stim = 0.0f;
    if (pos < totalFrames_) {
      capture_[pos] = micIn != nullptr ? micIn[i] : 0.0f;
      if (pos >= prerollFrames_) {
        const uint32_t rel = pos - prerollFrames_;
        const uint32_t burst = rel / periodFrames_;
        const uint32_t k = rel % periodFrames_;
        if (burst < repeats && k < mlsLen) stim = config_.level * mls_[k];
      }
    }
    stimulusOut[i] = stim;
  }

  cursor_ += frames;
  if (cursor_ >= totalFrames_) {
    int expected = kRunning;
    state_.compare_exchange_strong(expected, kComplete, std::memory_order_acq_rel);
  }
  return true;
}

LatencyProbe::BurstResult LatencyProbe::analyzeBurst(int index) const {
  BurstResult r;
  const auto n = static_cast<uint32_t>(mls_.size());
  const uint32_t start = prerollFrames_ + static_cast<uint32_t>(index) * periodFrames_;

  // Tương quan chéo trong cửa sổ độ trễ [0, maxLatency).
  std::vector<float> corr(maxLatencyFrames_, 0.0f);
  for (uint32_t lag = 0; lag < maxLatencyFrames_; ++lag) {
    if (start + lag + n > totalFrames_) break;
    corr[lag] = std::fabs(dot(mls_.data(), capture_.data() + start + lag, n));
  }

  const auto peakIt = std::max_element(corr.begin(), corr.end());
  const float peak = *peakIt;
  if (!(peak > 0.0f)) return r;
  const auto peakLag = static_cast<uint32_t>(peakIt - corr.begin());

  // Đỉnh phụ: lớn nhất ngoài vùng ±2 ms quanh đỉnh chính.
  const auto excl = static_cast<uint32_t>(kPeakExclusionSec * sampleRate_);
  float second = 0.0f;
  for (uint32_t lag = 0; lag < corr.size(); ++lag) {
    const uint32_t d = lag > peakLag ? lag - peakLag : peakLag - lag;
    if (d > excl) second = std::max(second, corr[lag]);
  }

  // Nội suy parabol qua 3 điểm quanh đỉnh → vị trí đỉnh dưới một mẫu.
  double frac = 0.0;
  if (peakLag > 0 && peakLag + 1 < corr.size()) {
    const double y0 = static_cast<double>(corr[peakLag - 1]);
    const double y1 = static_cast<double>(corr[peakLag]);
    const double y2 = static_cast<double>(corr[peakLag + 1]);
    const double denom = y0 - 2.0 * y1 + y2;
    if (denom < 0.0) frac = 0.5 * (y0 - y2) / denom;
  }

  r.latencyFrames = static_cast<double>(peakLag) + frac;
  r.confidence = 1.0f - second / peak;
  r.valid = r.confidence >= kMinConfidence;
  return r;
}

LatencyProbe::Result LatencyProbe::analyze() const {
  Result result;
  if (!isComplete() || mls_.empty()) return result;

  std::vector<double> lags;
  std::vector<double> confidences;
  for (int i = 0; i < config_.repeats; ++i) {
    BurstResult b = analyzeBurst(i);
    if (b.valid) {
      lags.push_back(b.latencyFrames);
      confidences.push_back(static_cast<double>(b.confidence));
    }
    result.bursts.push_back(b);
  }

  result.validBursts = static_cast<int>(lags.size());
  if (lags.empty()) return result;

  const auto [mn, mx] = std::minmax_element(lags.begin(), lags.end());
  result.spreadFrames = *mx - *mn;
  result.latencyFrames = median(lags);
  result.latencyMs = 1000.0 * result.latencyFrames / sampleRate_;
  result.confidence = static_cast<float>(median(confidences));

  // Hợp lệ khi quá nửa số lần đo hợp lệ và chúng khớp nhau.
  result.valid = result.validBursts * 2 > config_.repeats &&
                 result.spreadFrames <= kMaxSpreadSec * sampleRate_;
  return result;
}

}  // namespace tonecore
