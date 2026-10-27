#include "stretch/transient_detector.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr double kBlockSec = 0.001;  // khối năng lượng 1 ms
constexpr int kBaseFrom = 10;        // nền = trung bình khối b−10 … b−3
constexpr int kBaseTo = 3;
constexpr int kPeakHalf = 5;            // đỉnh của R trong ±5 khối
constexpr float kRiseDb = 8.0f;         // tăng tối thiểu so với nền ngay trước
constexpr float kLevelGate = 1e-3f;     // E ≥ −30 dB so với đỉnh gần đây
constexpr double kPeakTau = 1.0;        // đỉnh gần đây tắt dần τ 1 s
constexpr double kMinGapSec = 0.010;    // hai onset cách nhau ít nhất 10 ms
constexpr double kPrerollSec = 0.05;    // dò từ trước điểm bắt đầu 50 ms (đủ nền + đỉnh gần đây)
constexpr double kGuardSec = 0.003;     // cửa sổ "trước onset" kết thúc trước onset 3 ms
constexpr double kClassifySec = 0.021;  // cửa sổ phổ trước / sau onset để tính mức trội
constexpr float kRiseLowDb = 3.0f;
constexpr float kRiseHighDb = 9.0f;
constexpr std::size_t kMaxOnsets = 32;
constexpr std::size_t kEnergyRing = 64;  // > kBaseFrom + 2·kPeakHalf + vài khối

int nextPow2(double v) {
  int n = 1;
  while (n < v) n <<= 1;
  return n;
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
      block_(std::max(1, static_cast<int>(std::lround(kBlockSec * sampleRate)))),
      energy_(kEnergyRing, 0.0f),
      peakDecay_(static_cast<float>(std::exp(-kBlockSec / kPeakTau))),
      clsSize_(std::min(binsFftSize, nextPow2(kClassifySec * sampleRate))),
      clsFft_(static_cast<std::size_t>(binsFftSize)),
      clsWindow_(tukeyFromOnset(clsSize_, static_cast<int>(0.001 * sampleRate))),
      clsBuf_(static_cast<std::size_t>(binsFftSize)),
      clsSpec_(static_cast<std::size_t>(binsFftSize / 2 + 1)),
      prePow_(static_cast<std::size_t>(binsFftSize / 2 + 1)),
      postPow_(static_cast<std::size_t>(binsFftSize / 2 + 1)) {
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
  peakTrack_ = 0.0f;
  const auto first = static_cast<int64_t>(
      std::floor((static_cast<double>(from) - kPrerollSec * rate_) / static_cast<double>(block_)));
  firstBlock_ = first;
  nextBlock_ = first;
  decidedUpTo_ = first + kBaseFrom;  // khối đầu chưa đủ nền
}

// Năng lượng trung bình của sai phân bậc 1 trong khối nextBlock_.
void TransientDetector::computeBlock() noexcept {
  const int64_t b = nextBlock_++;
  const int64_t t0 = b * block_;
  double e = 0.0;
  float prev = sample(t0 - 1);
  for (int i = 0; i < block_; ++i) {
    const float x = sample(t0 + i);
    const auto d = static_cast<double>(x - prev);
    e += d * d;
    prev = x;
  }
  energy_[static_cast<std::size_t>(b) & (kEnergyRing - 1)] = static_cast<float>(e / block_);
}

// R_b (dB): năng lượng 2 khối từ b so với nền b−10 … b−3.
float TransientDetector::rise(int64_t b) const noexcept {
  const auto at = [&](int64_t k) {
    return energy_[static_cast<std::size_t>(k) & (kEnergyRing - 1)];
  };
  float base = 0.0f;
  for (int64_t k = b - kBaseFrom; k <= b - kBaseTo; ++k) base += at(k);
  base /= static_cast<float>(kBaseFrom - kBaseTo + 1);
  const float now = 0.5f * (at(b) + at(b + 1));
  constexpr float kFloor = 1e-14f;
  return 10.0f * std::log10((now + kFloor) / (base + kFloor));
}

void TransientDetector::ensure(int64_t pos) noexcept {
  // Onset của khối b nằm trong khối b ± 3 ms → quyết định đủ khối có đầu < pos.
  const int64_t target = pos / block_ + 4;
  while (decidedUpTo_ <= target) {
    while (nextBlock_ <= decidedUpTo_ + kPeakHalf + 1) computeBlock();
    decide(decidedUpTo_++);
  }
}

void TransientDetector::decide(int64_t b) noexcept {
  const float e = energy_[static_cast<std::size_t>(b) & (kEnergyRing - 1)];
  peakTrack_ = std::max(e, peakTrack_ * peakDecay_);
  if (e < kLevelGate * peakTrack_) return;
  const float r = rise(b);
  if (r < kRiseDb) return;
  for (int i = 1; i <= kPeakHalf; ++i) {
    if (b - i - kBaseFrom >= firstBlock_ && rise(b - i) >= r) return;
    if (rise(b + i) > r) return;
  }
  const double time = refine(b);
  if (count_ > 0 && time - onset(count_ - 1).time < kMinGapSec * rate_) return;
  if (count_ == onsets_.size()) {  // đầy: bỏ onset cũ nhất
    head_ = (head_ + 1) % onsets_.size();
    --count_;
  }
  Onset& o = onsets_[(head_ + count_) % onsets_.size()];
  o.time = time;
  classify(o);
  ++count_;
}

// Thời điểm onset trong ±3 ms quanh khối b: điểm n có E(1 ms sau) / E(5 ms trước) lớn nhất
// (E = năng lượng sai phân bậc 1).
double TransientDetector::refine(int64_t b) const noexcept {
  const int64_t lo = b * block_ - 3 * block_;
  const int64_t hi = b * block_ + 3 * block_;
  const auto after = static_cast<int64_t>(block_);
  const auto before = static_cast<int64_t>(5 * block_);
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
  int64_t bestN = b * block_;
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
