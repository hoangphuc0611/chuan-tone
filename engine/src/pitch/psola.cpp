#include "pitch/psola.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace tonecore {

namespace {

constexpr int kHannSize = 2048;   // số điểm bảng cho t ∈ [−1, 1]
constexpr int kCorrMaxLen = 256;  // độ dài tối đa đoạn so dạng sóng khi tinh chỉnh mốc

std::size_t nextPow2(std::size_t n) {
  std::size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}

}  // namespace

Psola::Psola(const PsolaConfig& cfg)
    : cfg_(cfg), maxPeriod_(static_cast<int>(std::ceil(cfg.sampleRate / cfg.minF0))) {
  // Cần giữ: trễ + vài chu kỳ dài nhất (hạt đọc lùi tới a − H, so dạng sóng lùi thêm T).
  const std::size_t need = static_cast<std::size_t>(cfg.latency + 6 * maxPeriod_ + kCorrMaxLen);
  ring_.assign(nextPow2(need), 0.0f);
  mask_ = ring_.size() - 1;
  hannTable_.resize(kHannSize + 1);
  for (int i = 0; i <= kHannSize; ++i) {
    const double t = -1.0 + 2.0 * i / kHannSize;
    hannTable_[static_cast<std::size_t>(i)] =
        static_cast<float>(0.5 * (1.0 + std::cos(3.14159265358979323846 * t)));
  }
}

void Psola::reset() noexcept {
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  n_ = 0;
  voiced_ = false;
  period_ = 0.0f;
  ratio_ = 1.0f;
  markHead_ = markCount_ = 0;
  chain_ = false;
  lastMark_ = 0;
  grainCount_ = 0;
  synthActive_ = false;
  wetMix_ = 0.0f;
}

// [RT]
void Psola::setAnalysis(bool voiced, float periodSamples) noexcept {
  const bool ok =
      voiced && periodSamples >= 8.0f && periodSamples <= static_cast<float>(maxPeriod_) * 1.05f;
  voiced_ = ok;
  if (ok) period_ = periodSamples;
  if (!ok) {
    // Hết đoạn hữu thanh: bỏ chuỗi mốc (các hạt đang phát đã giữ bản sao vị trí của chúng).
    chain_ = false;
    markCount_ = 0;
  }
}

// [RT]
void Psola::setRatio(float ratio) noexcept {
  ratio_ = std::clamp(ratio, 0.5f, 2.0f);
}

// [RT]
float Psola::hann(float t) const noexcept {
  const float pos = (t + 1.0f) * 0.5f * static_cast<float>(kHannSize);
  if (pos <= 0.0f || pos >= static_cast<float>(kHannSize)) return 0.0f;
  const auto i = static_cast<int>(pos);
  const float f = pos - static_cast<float>(i);
  const float* h = hannTable_.data();
  return h[i] + f * (h[i + 1] - h[i]);
}

// [RT] Tìm vị trí trong pred ± T/4 mà đoạn kết thúc tại đó giống nhất đoạn kết thúc tại mốc
// trước (tương quan chuẩn hoá). Giữ các mốc cùng pha dạng sóng, chống trôi.
int64_t Psola::refineMark(int64_t pred, float period) const noexcept {
  const int radius = std::max(1, static_cast<int>(period * 0.25f));
  const int len = std::min(kCorrMaxLen, std::max(8, static_cast<int>(period)));
  double ePrev = 0.0;
  for (int k = 0; k < len; ++k) {
    const auto v = static_cast<double>(at(lastMark_ - k));
    ePrev += v * v;
  }
  if (ePrev < 1e-12) return pred;
  int64_t best = pred;
  double bestScore = -2.0;
  for (int64_t c = pred - radius; c <= pred + radius; ++c) {
    double num = 0.0;
    double e = 0.0;
    for (int k = 0; k < len; ++k) {
      const auto x = static_cast<double>(at(c - k));
      num += x * static_cast<double>(at(lastMark_ - k));
      e += x * x;
    }
    const double score = num / std::sqrt(e * ePrev + 1e-20);
    if (score > bestScore) {
      bestScore = score;
      best = c;
    }
  }
  return bestScore > 0.3 ? best : pred;
}

// [RT] Đặt các mốc phân tích mới khi đã đủ tín hiệu.
void Psola::placeMarks() noexcept {
  if (!voiced_) return;
  const float t = period_;
  const auto quarter = static_cast<int64_t>(t * 0.25f) + 1;
  const int64_t newest = n_ - 1;
  if (!chain_) {
    // Mốc đầu tiên: đỉnh dương lớn nhất trong một chu kỳ gần nhất đã đủ dữ liệu.
    const int64_t end = newest - quarter;
    const int64_t begin = end - static_cast<int64_t>(t);
    if (begin < 0) return;
    int64_t best = end;
    float bestVal = -1e30f;
    for (int64_t i = begin; i <= end; ++i) {
      if (at(i) > bestVal) {
        bestVal = at(i);
        best = i;
      }
    }
    lastMark_ = best;
    chain_ = true;
  } else {
    const int64_t pred = lastMark_ + static_cast<int64_t>(std::lround(t));
    if (pred + quarter > newest) return;
    lastMark_ = refineMark(pred, t);
  }
  marks_[markHead_] = {lastMark_, t};
  markHead_ = (markHead_ + 1) % kMaxMarks;
  markCount_ = std::min(markCount_ + 1, kMaxMarks);
  ++marksPlaced_;
}

// [RT] Bắt đầu các hạt mới có đầu hạt ≤ u.
void Psola::startGrains(int64_t u) noexcept {
  if (!voiced_ || markCount_ == 0) {
    synthActive_ = false;
    return;
  }
  if (!synthActive_) {
    // Bắt đầu chuỗi tổng hợp: hạt đầu tiên tâm ở u + nửa chu kỳ.
    nextSynth_ = static_cast<double>(u) + 0.5 * static_cast<double>(period_);
    synthActive_ = true;
  }
  const int latency = cfg_.latency;
  for (int guard = 0; guard < 4; ++guard) {
    const auto s = static_cast<int64_t>(std::llround(nextSynth_));
    // Chọn mốc gần s nhất.
    int bestIdx = -1;
    int64_t bestDist = INT64_MAX;
    for (int i = 0; i < markCount_; ++i) {
      const int64_t d = std::llabs(marks_[i].pos - s);
      if (d < bestDist) {
        bestDist = d;
        bestIdx = i;
      }
    }
    const Mark& m = marks_[bestIdx];
    const float t = m.period;
    // Nửa độ dài hạt: đủ 1 chu kỳ nếu được, cắt ngắn với giọng trầm để giữ độ trễ cố định.
    const float hFull = t;
    const float hLimit = static_cast<float>(latency) - 0.25f * t;
    const int h = static_cast<int>(std::max(0.6f * t, std::min(hFull, hLimit)));
    if (s - h > u) return;  // chưa tới đầu hạt kế tiếp
    // Mốc quá xa (vd. vừa bắt đầu hát, chưa có mốc gần) → bỏ hạt này, đi tiếp.
    const bool usable = bestDist <= static_cast<int64_t>(2.0f * t) + latency;
    // Hạt đọc tới a + (u' − s) ≤ u' + latency ⇒ cần a − s ≤ latency.
    if (usable && m.pos - s <= latency && grainCount_ < kMaxGrains) {
      grains_[grainCount_++] = {m.pos, s, h};
    }
    nextSynth_ += static_cast<double>(t) / static_cast<double>(ratio_);
  }
}

// [RT]
void Psola::process(const float* in, float* out, std::size_t n) noexcept {
  const int64_t latency = cfg_.latency;
  const float fadeStep = 1.0f / static_cast<float>(std::max(1, cfg_.crossfade));
  for (std::size_t i = 0; i < n; ++i) {
    ring_[static_cast<std::size_t>(n_) & mask_] = in[i];
    ++n_;
    placeMarks();

    const int64_t u = n_ - 1 - latency;
    const float dry = u >= 0 ? at(u) : 0.0f;

    startGrains(u);
    float acc = 0.0f;
    float wsum = 0.0f;
    int keep = 0;
    for (int g = 0; g < grainCount_; ++g) {
      const Grain gr = grains_[g];
      const int64_t off = u - gr.s;
      if (off > gr.h) continue;  // hạt đã xong → bỏ
      grains_[keep++] = gr;
      const float w = hann(static_cast<float>(off) / static_cast<float>(gr.h));
      acc += w * at(gr.a + off);
      wsum += w;
    }
    grainCount_ = keep;

    const bool haveWet = wsum > 1e-3f;
    const float target = (voiced_ && haveWet) ? 1.0f : 0.0f;
    if (wetMix_ < target) {
      wetMix_ = std::min(target, wetMix_ + fadeStep);
    } else if (wetMix_ > target) {
      wetMix_ = std::max(target, wetMix_ - fadeStep);
    }
    const float wet = haveWet ? acc / wsum : dry;
    out[i] = dry + wetMix_ * (wet - dry);
  }
}

}  // namespace tonecore
