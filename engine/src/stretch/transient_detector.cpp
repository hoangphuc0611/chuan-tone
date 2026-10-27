#include "stretch/transient_detector.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr float kLogGamma = 1000.0f;  // độ nén log biên độ
constexpr float kThresholdRatio = 1.5f;
constexpr float kThresholdDelta = 0.02f;
constexpr double kMinGapSec = 0.03;  // hai onset cách nhau ít nhất 30 ms
constexpr double kPrerollSec = 0.2;  // dò từ trước điểm bắt đầu 200 ms (đủ ngữ cảnh trung vị)
constexpr double kGuardSec = 0.003;  // cửa sổ "trước onset" kết thúc trước onset 3 ms
constexpr float kRiseLowDb = 3.0f;
constexpr float kRiseHighDb = 9.0f;
constexpr std::size_t kMaxOnsets = 32;
// Onset phải mang đủ năng lượng MỚI: Σ k·max(0, sau − trước) / Σ k·sau (nhấn cao tần theo k) ≥
// ngưỡng. Loại các đỉnh flux giả trong đuôi một tiếng đánh (vd. thân kick quét 150 → 50 Hz làm năng
// lượng chuyển dần xuống bin thấp hơn: flux nhô lên nhưng không có năng lượng mới ở cao tần).
constexpr float kMinNewEnergy = 0.7f;

int nextPow2(double v) {
  int n = 1;
  while (n < v) n <<= 1;
  return n;
}

std::vector<float> hann(int n) {
  std::vector<float> w(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    w[static_cast<std::size_t>(i)] = static_cast<float>(
        0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(n)));
  }
  return w;
}

// Cửa sổ cho phổ "sau onset": dốc lên nửa Hann chỉ `rise` mẫu (tiếng đánh nằm ngay đầu cửa sổ —
// Hann thường gần 0 ở đó sẽ xoá mất tiếng click < 1 ms), phẳng, dốc xuống nửa Hann ở 25% cuối.
// Phổ "trước onset" dùng bản lật ngược (dốc nhanh ở phía onset).
std::vector<float> tukeyFromOnset(int n, int rise) {
  std::vector<float> w(static_cast<std::size_t>(n), 1.0f);
  const int fall = n / 4;
  for (int i = 0; i < rise; ++i) {
    w[static_cast<std::size_t>(i)] =
        static_cast<float>(0.5 - 0.5 * std::cos(dsp::kPi * (static_cast<double>(i) + 0.5) /
                                                static_cast<double>(rise)));
  }
  for (int i = 0; i < fall; ++i) {
    w[static_cast<std::size_t>(n - 1 - i)] =
        static_cast<float>(0.5 - 0.5 * std::cos(dsp::kPi * (static_cast<double>(i) + 0.5) /
                                                static_cast<double>(fall)));
  }
  return w;
}

}  // namespace

TransientDetector::TransientDetector(int channels, double sampleRate, int binsFftSize)
    : channels_(std::max(1, channels)),
      rate_(sampleRate),
      frame_(nextPow2(0.021 * sampleRate)),
      hop_(frame_ / 4),
      bins_(frame_ / 2 + 1),
      kMin_(static_cast<int>(std::ceil(30.0 * frame_ / sampleRate))),
      median_(std::max(1, static_cast<int>(std::lround(0.1 * sampleRate / (frame_ / 4))))),
      peak_(std::max(1, static_cast<int>(std::lround(0.03 * sampleRate / (frame_ / 4))))),
      fft_(static_cast<std::size_t>(frame_)),
      window_(hann(frame_)),
      buf_(static_cast<std::size_t>(frame_)),
      spec_(static_cast<std::size_t>(frame_ / 2 + 1)),
      prevLog_(static_cast<std::size_t>(frame_ / 2 + 1)),
      clsSize_(std::min(binsFftSize, nextPow2(2048.0 * sampleRate / 48000.0))),
      clsFft_(static_cast<std::size_t>(binsFftSize)),
      clsWindow_(tukeyFromOnset(clsSize_, static_cast<int>(0.001 * sampleRate))),
      clsBuf_(static_cast<std::size_t>(binsFftSize)),
      clsSpec_(static_cast<std::size_t>(binsFftSize / 2 + 1)),
      prePow_(static_cast<std::size_t>(binsFftSize / 2 + 1)),
      postPow_(static_cast<std::size_t>(binsFftSize / 2 + 1)) {
  flux_.resize(static_cast<std::size_t>(nextPow2(2.0 * (median_ + peak_) + 8.0)), 0.0f);
  scratch_.resize(static_cast<std::size_t>(2 * median_ + 1));
  onsets_.resize(kMaxOnsets);
  for (auto& o : onsets_) o.g.assign(static_cast<std::size_t>(binsFftSize / 2 + 1), 0.0f);
}

float TransientDetector::sample(int64_t t) const noexcept {
  if (t < 0 || static_cast<uint64_t>(t) >= in_.frames) return 0.0f;
  float sum = 0.0f;
  for (int c = 0; c < channels_; ++c) sum += in_.at(t, c);
  return sum / static_cast<float>(channels_);
}

void TransientDetector::start(const InputView& in, int64_t from) noexcept {
  in_ = in;
  count_ = 0;
  head_ = 0;
  const auto first = static_cast<int64_t>(
      std::floor((static_cast<double>(from) - kPrerollSec * rate_) / static_cast<double>(hop_)));
  // Khung first − 1 chỉ để có log phổ "trước" cho flux của khung first.
  nextFrame_ = first - 1;
  computeFlux();
  firstFrame_ = first;
  nextFrame_ = first;
  decidedUpTo_ = first + peak_;
}

void TransientDetector::computeFlux() noexcept {
  const int64_t t = nextFrame_++;
  const int64_t base = t * hop_;
  for (int i = 0; i < frame_; ++i) {
    buf_[static_cast<std::size_t>(i)] = sample(base + i) * window_[static_cast<std::size_t>(i)];
  }
  fft_.forward(buf_.data(), spec_.data());
  float sum = 0.0f;
  for (int k = 0; k < bins_; ++k) {
    const auto kk = static_cast<std::size_t>(k);
    const float l = std::log1p(kLogGamma * std::abs(spec_[kk]));
    if (k >= kMin_) sum += std::max(0.0f, l - prevLog_[kk]);
    prevLog_[kk] = l;
  }
  flux_[static_cast<std::size_t>(t) & (flux_.size() - 1)] = sum / static_cast<float>(bins_ - kMin_);
}

void TransientDetector::ensure(int64_t pos) noexcept {
  // Onset của khung t nằm trong [t·hop − hop, t·hop + frame] → quyết định đủ khung có đầu < pos.
  const int64_t target = pos / hop_ + 2;
  while (decidedUpTo_ <= target) {
    while (nextFrame_ <= decidedUpTo_ + median_) computeFlux();
    decide(decidedUpTo_++);
  }
}

void TransientDetector::decide(int64_t t) noexcept {
  const auto mask = flux_.size() - 1;
  const auto at = [&](int64_t f) { return flux_[static_cast<std::size_t>(f) & mask]; };
  const float v = at(t);
  for (int i = 1; i <= peak_; ++i) {
    if (t - i >= firstFrame_ && at(t - i) >= v) return;
    if (at(t + i) > v) return;
  }
  std::size_t n = 0;
  for (int64_t f = std::max(firstFrame_, t - median_); f <= t + median_; ++f) scratch_[n++] = at(f);
  const auto mid = scratch_.begin() + static_cast<std::ptrdiff_t>(n / 2);
  std::nth_element(scratch_.begin(), mid, scratch_.begin() + static_cast<std::ptrdiff_t>(n));
  if (v <= kThresholdRatio * *mid + kThresholdDelta) return;

  const double time = refine(t);
  if (count_ > 0 && time - onset(count_ - 1).time < kMinGapSec * rate_) return;
  if (count_ == onsets_.size()) {  // đầy: bỏ onset cũ nhất
    head_ = (head_ + 1) % onsets_.size();
    --count_;
  }
  Onset& o = onsets_[(head_ + count_) % onsets_.size()];
  o.time = time;
  classify(o);
  if (o.strength < kMinNewEnergy) return;
  ++count_;
}

// Thời điểm onset trong miền thời gian quanh khung t: điểm n có E(sau 2 ms) / E(trước 10 ms) lớn
// nhất, E = năng lượng sai phân bậc 1 (nhấn cao tần, bỏ thành phần trầm ngân sẵn).
double TransientDetector::refine(int64_t t) const noexcept {
  const int64_t lo = t * hop_ - hop_;
  const int64_t hi = t * hop_ + frame_;
  const auto after = static_cast<int64_t>(0.002 * rate_);
  const auto before = static_cast<int64_t>(0.010 * rate_);
  // Cửa sổ trượt: "trước" = [n − before, n), "sau" = [n, n + after).
  const auto energy = [&](int64_t n) {
    const auto d = static_cast<double>(sample(n) - sample(n - 1));
    return d * d;
  };
  double eBefore = 0.0;
  double eAfter = 0.0;
  for (int64_t n = lo - before; n < lo; ++n) eBefore += energy(n);
  for (int64_t n = lo; n < lo + after; ++n) eAfter += energy(n);
  const double floor = 1e-12 * static_cast<double>(before);
  double best = -1.0;
  int64_t bestN = t * hop_;
  for (int64_t n = lo; n <= hi; ++n) {
    const double score = eAfter / (eBefore + floor);
    if (score > best) {
      best = score;
      bestN = n;
    }
    eBefore += energy(n) - energy(n - before);
    eAfter += energy(n + after) - energy(n);
  }
  return static_cast<double>(bestN);
}

// Mức trội từng bin: công suất ngay sau onset so với ngay trước (đệm 0 lên lưới bin của PV).
void TransientDetector::classify(Onset& o) noexcept {
  const auto bins = clsSpec_.size();
  // reversed = true: cửa sổ lật ngược (cho đoạn trước onset, dốc nhanh ở cuối).
  const auto powerAt = [&](int64_t start, bool reversed, std::vector<float>& out) {
    std::fill(clsBuf_.begin(), clsBuf_.end(), 0.0f);
    for (int i = 0; i < clsSize_; ++i) {
      const auto wi = static_cast<std::size_t>(reversed ? clsSize_ - 1 - i : i);
      clsBuf_[static_cast<std::size_t>(i)] = sample(start + i) * clsWindow_[wi];
    }
    clsFft_.forward(clsBuf_.data(), clsSpec_.data());
    for (std::size_t k = 0; k < bins; ++k) out[k] = std::norm(clsSpec_[k]);
  };
  const auto t0 = static_cast<int64_t>(std::llround(o.time));
  const auto guard = static_cast<int64_t>(kGuardSec * rate_);
  powerAt(t0 - guard - clsSize_, true, prePow_);
  powerAt(t0, false, postPow_);
  float maxPost = 0.0f;
  double newEnergy = 0.0;
  double postEnergy = 0.0;
  for (std::size_t k = 0; k < bins; ++k) {
    maxPost = std::max(maxPost, postPow_[k]);
    const auto kk = static_cast<double>(k);
    newEnergy += kk * std::max(0.0, static_cast<double>(postPow_[k] - prePow_[k]));
    postEnergy += kk * static_cast<double>(postPow_[k]);
  }
  o.strength = postEnergy > 0.0 ? static_cast<float>(newEnergy / postEnergy) : 0.0f;
  // Sàn chỉ để tránh chia cho 0 khi trước onset im lặng: phải rất thấp so với bin mạnh nhất (thân
  // kick trầm có thể mạnh hơn từng bin cao tần của tiếng click > 90 dB).
  const float floor = 1e-30f + 1e-14f * maxPost;
  for (std::size_t k = 0; k < bins; ++k) {
    // Làm mịn ±1 bin cho bớt nhiễu ước lượng.
    const std::size_t a = k > 0 ? k - 1 : k;
    const std::size_t b = k + 1 < bins ? k + 1 : k;
    const float post = postPow_[a] + postPow_[k] + postPow_[b];
    const float pre = prePow_[a] + prePow_[k] + prePow_[b];
    if (postPow_[k] < 1e-10f * maxPost) {
      o.g[k] = 0.0f;
      continue;
    }
    const float riseDb = 10.0f * std::log10((post + floor) / (pre + floor));
    o.g[k] = std::clamp((riseDb - kRiseLowDb) / (kRiseHighDb - kRiseLowDb), 0.0f, 1.0f);
  }
}

void TransientDetector::dropBefore(double before) noexcept {
  while (count_ > 0 && onsets_[head_].time < before) {
    head_ = (head_ + 1) % onsets_.size();
    --count_;
  }
}

}  // namespace tonecore
