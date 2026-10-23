#include "key/key_detect.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

float pearson(const ChromaVector& a, const std::array<float, 12>& b) {
  double ma = 0.0;
  double mb = 0.0;
  for (std::size_t i = 0; i < 12; ++i) {
    ma += static_cast<double>(a[i]);
    mb += static_cast<double>(b[i]);
  }
  ma /= 12.0;
  mb /= 12.0;
  double num = 0.0;
  double va = 0.0;
  double vb = 0.0;
  for (std::size_t i = 0; i < 12; ++i) {
    const double da = static_cast<double>(a[i]) - ma;
    const double db = static_cast<double>(b[i]) - mb;
    num += da * db;
    va += da * da;
    vb += db * db;
  }
  if (!(va > 1e-20) || !(vb > 1e-20)) return 0.0f;
  return static_cast<float>(num / std::sqrt(va * vb));
}

namespace {

// Xoay profile: lớp cao độ pc nhận trọng số của bậc (pc − root).
std::array<float, 12> rotate(const std::array<float, 12>& base, int root) {
  std::array<float, 12> rotated{};
  for (int pc = 0; pc < 12; ++pc) {
    rotated[static_cast<std::size_t>(pc)] = base[static_cast<std::size_t>((pc - root + 12) % 12)];
  }
  return rotated;
}

}  // namespace

KeyScores scoreKeys(const ChromaVector& bass, const ChromaVector& treble, const KeyProfile& profile,
                    float bassWeight) {
  KeyScores scores{};
  for (int mode = 0; mode < 2; ++mode) {
    const std::array<float, 12>& base = mode == 0 ? profile.major : profile.minor;
    const auto& bassOpt = mode == 0 ? profile.bassMajor : profile.bassMinor;
    const std::array<float, 12>& bassBase = bassOpt ? *bassOpt : base;
    for (int root = 0; root < 12; ++root) {
      const float rt = pearson(treble, rotate(base, root));
      const float rb = pearson(bass, rotate(bassBase, root));
      scores[static_cast<std::size_t>(mode * 12 + root)] =
          (1.0f - bassWeight) * rt + bassWeight * rb;
    }
  }
  return scores;
}

std::vector<uint8_t> voicedFrames(const Chromagram& ch, float silenceDb) {
  const std::size_t F = ch.frames();
  std::vector<uint8_t> voiced(F, 0);
  if (F == 0) return voiced;
  std::vector<float> sorted = ch.energyDb;
  const auto k = static_cast<std::ptrdiff_t>((F - 1) * 95 / 100);
  std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
  // Ngưỡng tương đối theo bài, nhưng không thấp hơn sàn tuyệt đối (bài im lặng hoàn toàn thì
  // mức phân vị 95 cũng là "im lặng").
  constexpr float kAbsoluteFloorDb = -120.0f;
  const float thr = std::max(sorted[static_cast<std::size_t>(k)] - silenceDb, kAbsoluteFloorDb);
  for (std::size_t i = 0; i < F; ++i) voiced[i] = ch.energyDb[i] >= thr ? 1 : 0;
  return voiced;
}

std::size_t accumulateChroma(const Chromagram& ch, const std::vector<uint8_t>& voiced,
                             std::size_t from, std::size_t to, ChromaFrameNorm norm,
                             ChromaVector& bass, ChromaVector& treble) {
  bass.fill(0.0f);
  treble.fill(0.0f);
  to = std::min(to, ch.frames());
  std::size_t used = 0;
  for (std::size_t i = from; i < to; ++i) {
    if (!voiced[i]) continue;
    // Chuẩn hoá chung cho cả bass lẫn treble của khung (giữ tỉ lệ giữa hai dải).
    float scale = 1.0f;
    if (norm != ChromaFrameNorm::None) {
      float acc = 0.0f;
      for (std::size_t pc = 0; pc < 12; ++pc) {
        const float v = ch.bass[i][pc] + ch.treble[i][pc];
        acc = norm == ChromaFrameNorm::L1 ? acc + v : std::max(acc, v);
      }
      if (!(acc > 0.0f)) continue;
      scale = 1.0f / acc;
    }
    for (std::size_t pc = 0; pc < 12; ++pc) {
      bass[pc] += scale * ch.bass[i][pc];
      treble[pc] += scale * ch.treble[i][pc];
    }
    ++used;
  }
  return used;
}

KeyEstimate pickKey(const KeyScores& scores, const KeyDetectConfig& cfg) {
  KeyEstimate est;
  est.scores = scores;
  const auto best =
      static_cast<std::size_t>(std::max_element(scores.begin(), scores.end()) - scores.begin());
  est.key = keyFromIndex(static_cast<int>(best));
  const double top = static_cast<double>(scores[best]);
  const double t = std::max(1e-6, static_cast<double>(cfg.temperature));
  double sum = 0.0;
  for (const float s : scores) sum += std::exp((static_cast<double>(s) - top) / t);
  const double span = std::max(1e-6, static_cast<double>(cfg.clarityFull - cfg.clarityMin));
  const double clarity = std::clamp((top - static_cast<double>(cfg.clarityMin)) / span, 0.0, 1.0);
  est.confidence = static_cast<float>(clarity / sum);
  return est;
}

const KeyProfile* resolveProfile(const KeyDetectConfig& cfg) {
  return cfg.customProfile ? &*cfg.customProfile : findKeyProfile(cfg.profile);
}

int endingBassPitchClass(const Chromagram& ch, const std::vector<uint8_t>& voiced, float endSec) {
  std::size_t last = ch.frames();
  while (last > 0 && !voiced[last - 1]) --last;
  if (last == 0) return -1;
  const auto span = static_cast<std::size_t>(
      std::max(1.0, std::round(static_cast<double>(endSec) / ch.frameSec)));
  ChromaVector sum{};
  for (std::size_t i = last > span ? last - span : 0; i < last; ++i) {
    if (!voiced[i]) continue;
    for (std::size_t pc = 0; pc < 12; ++pc) sum[pc] += ch.bass[i][pc];
  }
  const auto best = std::max_element(sum.begin(), sum.end());
  return *best > 0.0f ? static_cast<int>(best - sum.begin()) : -1;
}

std::optional<KeyEstimate> estimateGlobalKey(const Chromagram& ch, const KeyDetectConfig& cfg) {
  const KeyProfile* profile = resolveProfile(cfg);
  if (profile == nullptr || ch.frames() == 0) return std::nullopt;
  const std::vector<uint8_t> voiced = voicedFrames(ch, cfg.silenceDb);
  ChromaVector bass{};
  ChromaVector treble{};
  const std::size_t used =
      accumulateChroma(ch, voiced, 0, ch.frames(), cfg.frameNorm, bass, treble);
  if (static_cast<double>(used) * ch.frameSec < static_cast<double>(cfg.minVoicedSec)) {
    return std::nullopt;
  }
  KeyScores scores = scoreKeys(bass, treble, *profile, cfg.bassWeight);
  for (std::size_t i = 12; i < 24; ++i) scores[i] += cfg.minorBias;
  if (cfg.endWeight > 0.0f) {
    const int tonic = endingBassPitchClass(ch, voiced, cfg.endSec);
    if (tonic >= 0) {
      scores[static_cast<std::size_t>(tonic)] += cfg.endWeight;       // trưởng
      scores[static_cast<std::size_t>(12 + tonic)] += cfg.endWeight;  // thứ
    }
  }
  return pickKey(scores, cfg);
}

}  // namespace tonecore
