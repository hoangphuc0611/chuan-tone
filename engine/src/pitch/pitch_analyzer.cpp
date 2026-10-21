#include "pitch/pitch_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tonecore {

namespace {

int decimationFactor(double inputRate, double target) {
  return std::max(1, static_cast<int>(std::lround(inputRate / target)));
}

}  // namespace

PitchAnalyzer::PitchAnalyzer(const PitchAnalyzerConfig& cfg)
    : cfg_(cfg),
      factor_(decimationFactor(cfg.inputRate, cfg.targetAnalysisRate)),
      analysisRate_(cfg.inputRate / factor_),
      window_(static_cast<std::size_t>(std::lround(cfg.windowMs * 1e-3 * analysisRate_))),
      hop_(std::max<std::size_t>(
          1, static_cast<std::size_t>(std::lround(cfg.hopMs * 1e-3 * analysisRate_)))),
      decimator_(factor_),
      tracker_(cfg.tracker),
      dec_(kChunk / static_cast<std::size_t>(factor_) + 2),
      buf_(window_ + 8 * hop_, 0.0f) {
  PitchDetectorConfig dc;
  dc.sampleRate = analysisRate_;
  dc.window = window_;
  dc.minF0 = cfg.minF0;
  dc.maxF0 = cfg.maxF0;
  if (cfg.algorithm == PitchAlgorithm::Yin) {
    yin_ = std::make_unique<Yin>(dc);
  } else {
    mpm_ = std::make_unique<Mpm>(dc);
  }
}

void PitchAnalyzer::reset() noexcept {
  decimator_.reset();
  tracker_.reset();
  fill_ = 0;
  sinceHop_ = 0;
  produced_ = 0;
}

// [RT]
bool PitchAnalyzer::pushSample(float s) noexcept {
  if (fill_ == buf_.size()) {
    // Dồn về đầu, giữ lại đúng một cửa sổ.
    std::memmove(buf_.data(), buf_.data() + (fill_ - window_), window_ * sizeof(float));
    fill_ = window_;
  }
  buf_[fill_++] = s;
  ++produced_;
  ++sinceHop_;
  if (fill_ < window_ || sinceHop_ < hop_) return false;
  sinceHop_ = 0;
  analyzeFrame(buf_.data() + (fill_ - window_));
  return true;
}

// [RT]
void PitchAnalyzer::analyzeFrame(const float* x) noexcept {
  double energy = 0.0;
  std::size_t crossings = 0;
  for (std::size_t i = 0; i < window_; ++i) {
    energy += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    if (i > 0 && ((x[i] >= 0.0f) != (x[i - 1] >= 0.0f))) ++crossings;
  }
  const double rms = std::sqrt(energy / static_cast<double>(window_));
  frame_.rmsDb = static_cast<float>(20.0 * std::log10(std::max(rms, 1e-6)));
  frame_.zcr = static_cast<float>(crossings) / static_cast<float>(window_);
  frame_.raw = mpm_ ? mpm_->analyze(x) : yin_->analyze(x);
  frame_.tracked = tracker_.update(frame_.raw, frame_.rmsDb, frame_.zcr);

  // Thời điểm giữa cửa sổ: mẫu phân tích j ứng với mẫu gốc factor·j + (factor−1) − trễ bộ lọc.
  const double centerJ = static_cast<double>(produced_) - static_cast<double>(window_) / 2.0 - 0.5;
  const double inputIndex =
      static_cast<double>(factor_) * centerJ + (factor_ - 1) - decimator_.groupDelayInput();
  frame_.timeSec = inputIndex / cfg_.inputRate;
}

}  // namespace tonecore
