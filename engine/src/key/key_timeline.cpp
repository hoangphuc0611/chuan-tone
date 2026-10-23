#include "key/key_timeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace tonecore {

namespace {

constexpr int kKeys = 24;

double logSumExp(const double* v, int n) {
  double m = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; ++i) m = std::max(m, v[i]);
  if (!std::isfinite(m)) return m;
  double s = 0.0;
  for (int i = 0; i < n; ++i) s += std::exp(v[i] - m);
  return m + std::log(s);
}

// Đoạn theo chỉ số quan sát [t0, t1).
struct Run {
  std::size_t t0;
  std::size_t t1;
  int key;
};

}  // namespace

bool relatedKeys(const MusicalKey& a, const MusicalKey& b) {
  if (a == b) return false;
  const int d = (b.root - a.root + 12) % 12;
  if (a.mode == b.mode) return d == 7 || d == 5 || d == 1 || d == 2;  // quãng 5, nâng tone
  if (d == 0) return true;                                            // song song
  return a.mode == KeyMode::Major ? d == 9 : d == 3;                  // tương đối
}

std::vector<KeySegment> estimateKeyTimeline(const Chromagram& ch, double durationSec,
                                            const KeyDetectConfig& keyCfg,
                                            const KeyTimelineConfig& cfg,
                                            const KeyEstimate& globalKey) {
  const KeyProfile* profile = resolveProfile(keyCfg);
  const std::size_t F = ch.frames();
  if (profile == nullptr || F == 0 || !(ch.frameSec > 0.0)) return {};
  if (!(durationSec > 0.0)) durationSec = static_cast<double>(F) * ch.frameSec;

  // --- 1. Quan sát: điểm 24 tone của từng cửa sổ trượt ---
  const auto w = static_cast<std::size_t>(
      std::max(1L, std::lround(static_cast<double>(cfg.windowSec) / ch.frameSec)));
  const auto h = static_cast<std::size_t>(
      std::max(1L, std::lround(static_cast<double>(cfg.hopSec) / ch.frameSec)));
  const std::vector<uint8_t> voiced = voicedFrames(ch, keyCfg.silenceDb);
  const double tau = std::max(1e-6, static_cast<double>(cfg.emissionTemperature));
  std::vector<double> centers;
  std::vector<double> logE;  // T × 24
  for (std::size_t start = 0; start < F; start += h) {
    const std::size_t end = std::min(F, start + w);
    ChromaVector bass{};
    ChromaVector treble{};
    const std::size_t used =
        accumulateChroma(ch, voiced, start, end, keyCfg.frameNorm, bass, treble);
    const bool informative =
        static_cast<double>(used) * ch.frameSec >= static_cast<double>(cfg.minWindowVoicedSec);
    const KeyScores s = scoreKeys(bass, treble, *profile, keyCfg.bassWeight);
    for (int k = 0; k < kKeys; ++k) {
      logE.push_back(informative ? static_cast<double>(s[static_cast<std::size_t>(k)]) / tau : 0.0);
    }
    centers.push_back(0.5 * static_cast<double>(start + end - 1) * ch.frameSec);
    if (end == F) break;
  }
  const std::size_t T = centers.size();

  // --- 2. Ma trận chuyển (log) ---
  const double eps = std::clamp(static_cast<double>(cfg.switchProb), 1e-9, 0.5);
  const auto boost = static_cast<double>(cfg.relatedBoost);
  std::vector<double> logA(kKeys * kKeys);
  for (int i = 0; i < kKeys; ++i) {
    double z = 0.0;
    for (int j = 0; j < kKeys; ++j) {
      if (j != i) z += relatedKeys(keyFromIndex(i), keyFromIndex(j)) ? boost : 1.0;
    }
    for (int j = 0; j < kKeys; ++j) {
      const double p =
          j == i ? 1.0 - eps
                 : eps * (relatedKeys(keyFromIndex(i), keyFromIndex(j)) ? boost : 1.0) / z;
      logA[static_cast<std::size_t>(i * kKeys + j)] = std::log(p);
    }
  }
  auto E = [&](std::size_t t, int k) { return logE[t * kKeys + static_cast<std::size_t>(k)]; };
  auto A = [&](int i, int j) { return logA[static_cast<std::size_t>(i * kKeys + j)]; };

  // --- 3a. Viterbi (phân bố đầu đều) ---
  std::vector<double> delta(T * kKeys);
  std::vector<int> psi(T * kKeys, 0);
  for (int k = 0; k < kKeys; ++k) delta[static_cast<std::size_t>(k)] = E(0, k);
  for (std::size_t t = 1; t < T; ++t) {
    for (int j = 0; j < kKeys; ++j) {
      double best = -std::numeric_limits<double>::infinity();
      int arg = 0;
      for (int i = 0; i < kKeys; ++i) {
        const double v = delta[(t - 1) * kKeys + static_cast<std::size_t>(i)] + A(i, j);
        if (v > best) {
          best = v;
          arg = i;
        }
      }
      delta[t * kKeys + static_cast<std::size_t>(j)] = best + E(t, j);
      psi[t * kKeys + static_cast<std::size_t>(j)] = arg;
    }
  }
  std::vector<int> path(T);
  {
    const double* last = &delta[(T - 1) * kKeys];
    path[T - 1] = static_cast<int>(std::max_element(last, last + kKeys) - last);
    for (std::size_t t = T - 1; t > 0; --t) {
      path[t - 1] = psi[t * kKeys + static_cast<std::size_t>(path[t])];
    }
  }

  // --- 3b. Forward–backward (log) → hậu nghiệm gamma ---
  std::vector<double> alpha(T * kKeys);
  std::vector<double> beta(T * kKeys, 0.0);
  std::vector<double> tmp(kKeys);
  for (int k = 0; k < kKeys; ++k) alpha[static_cast<std::size_t>(k)] = E(0, k);
  for (std::size_t t = 1; t < T; ++t) {
    for (int j = 0; j < kKeys; ++j) {
      for (int i = 0; i < kKeys; ++i) {
        tmp[static_cast<std::size_t>(i)] =
            alpha[(t - 1) * kKeys + static_cast<std::size_t>(i)] + A(i, j);
      }
      alpha[t * kKeys + static_cast<std::size_t>(j)] = logSumExp(tmp.data(), kKeys) + E(t, j);
    }
  }
  for (std::size_t t = T - 1; t > 0; --t) {
    for (int i = 0; i < kKeys; ++i) {
      for (int j = 0; j < kKeys; ++j) {
        tmp[static_cast<std::size_t>(j)] =
            A(i, j) + E(t, j) + beta[t * kKeys + static_cast<std::size_t>(j)];
      }
      beta[(t - 1) * kKeys + static_cast<std::size_t>(i)] = logSumExp(tmp.data(), kKeys);
    }
  }
  std::vector<double> gamma(T * kKeys);
  for (std::size_t t = 0; t < T; ++t) {
    for (int k = 0; k < kKeys; ++k) {
      tmp[static_cast<std::size_t>(k)] = alpha[t * kKeys + static_cast<std::size_t>(k)] +
                                         beta[t * kKeys + static_cast<std::size_t>(k)];
    }
    const double z = logSumExp(tmp.data(), kKeys);
    for (int k = 0; k < kKeys; ++k) {
      gamma[t * kKeys + static_cast<std::size_t>(k)] =
          std::exp(tmp[static_cast<std::size_t>(k)] - z);
    }
  }

  // --- 4. Đoạn liên tiếp cùng tone, gộp đoạn ngắn ---
  // Biên giữa quan sát t − 1 và t nằm ở trung điểm hai tâm cửa sổ.
  auto boundary = [&](std::size_t t) {
    if (t == 0) return 0.0;
    if (t >= T) return durationSec;
    return 0.5 * (centers[t - 1] + centers[t]);
  };
  std::vector<Run> runs;
  for (std::size_t t = 0; t < T; ++t) {
    if (runs.empty() || runs.back().key != path[t]) {
      runs.push_back({t, t + 1, path[t]});
    } else {
      runs.back().t1 = t + 1;
    }
  }
  auto length = [&](const Run& r) { return boundary(r.t1) - boundary(r.t0); };
  while (runs.size() > 1) {
    std::size_t shortest = 0;
    for (std::size_t i = 1; i < runs.size(); ++i) {
      if (length(runs[i]) < length(runs[shortest])) shortest = i;
    }
    if (length(runs[shortest]) >= static_cast<double>(cfg.minSegmentSec)) break;
    // Gộp vào đoạn kề dài hơn, rồi nối các đoạn kề cùng tone.
    const bool hasPrev = shortest > 0;
    const bool hasNext = shortest + 1 < runs.size();
    const bool toPrev =
        hasPrev && (!hasNext || length(runs[shortest - 1]) >= length(runs[shortest + 1]));
    if (toPrev) {
      runs[shortest - 1].t1 = runs[shortest].t1;
    } else {
      runs[shortest + 1].t0 = runs[shortest].t0;
    }
    runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(shortest));
    for (std::size_t i = 1; i < runs.size();) {
      if (runs[i].key == runs[i - 1].key) {
        runs[i - 1].t1 = runs[i].t1;
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  }

  std::vector<KeySegment> out;
  if (runs.size() == 1) {
    out.push_back({0.0, durationSec, globalKey.key, globalKey.confidence});
    return out;
  }
  for (const Run& r : runs) {
    double conf = 0.0;
    for (std::size_t t = r.t0; t < r.t1; ++t)
      conf += gamma[t * kKeys + static_cast<std::size_t>(r.key)];
    conf /= static_cast<double>(r.t1 - r.t0);
    out.push_back({boundary(r.t0), boundary(r.t1), keyFromIndex(r.key), static_cast<float>(conf)});
  }
  return out;
}

}  // namespace tonecore
