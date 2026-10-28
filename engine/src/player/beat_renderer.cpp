#include "player/beat_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "dsp/kaiser.h"
#include "fx/peak_limiter.h"
#include "stretch/pitch_shifter.h"

namespace tonecore {

namespace {

constexpr uint32_t kBlock = TransposedTrack::kBlockFrames;

// Trạng thái một lần render — sống trên luồng nền, mỗi bước giữ chính nó qua shared_ptr.
class RenderJob : public std::enable_shared_from_this<RenderJob> {
 public:
  RenderJob(Worker& worker, std::shared_ptr<TransposedTrack> track, uint64_t fromFrame,
            double sampleRate, BeatRenderer::ProgressFn onProgress, BeatRenderer::DoneFn onDone)
      : worker_(worker),
        tt_(std::move(track)),
        shifter_(2, tt_->semitones, configFor(sampleRate)),
        limiter_(2, sampleRate),
        raw_(2 * static_cast<std::size_t>(std::max(kBlock, BeatRenderer::kLeadInFrames))),
        block_(2 * static_cast<std::size_t>(kBlock)),
        lead_(2 * static_cast<std::size_t>(BeatRenderer::kLeadInFrames)),
        onProgress_(std::move(onProgress)),
        onDone_(std::move(onDone)),
        t0_(std::chrono::steady_clock::now()) {
    startPass(static_cast<uint32_t>(std::min<uint64_t>(fromFrame / kBlock, tt_->blocks - 1)));
  }

  void post() {
    auto self = shared_from_this();
    worker_.post([self] { self->step(); });
  }

 private:
  struct LeadIn {
    uint32_t block;
    std::vector<float> samples;  // kLeadInFrames frame stereo ngay trước khối `block`
  };

  static PitchShiftConfig configFor(double sampleRate) {
    PitchShiftConfig c;
    c.sampleRate = sampleRate;
    return c;
  }

  bool ready(uint32_t b) const noexcept {
    return tt_->ready[b].load(std::memory_order_acquire) != 0;
  }

  void startPass(uint32_t b) {
    inPass_ = true;
    next_ = b;
    const Track& src = *tt_->source;
    const uint64_t begin =
        b == 0 ? 0 : static_cast<uint64_t>(b) * kBlock - BeatRenderer::kLeadInFrames;
    shifter_.start(src.pcm.data(), src.frames, begin);
    // Limiter trễ L frame: mồi L frame đầu → đầu ra sau đó khớp đúng vị trí từ `begin`.
    limiter_.reset();
    const auto lookahead = static_cast<uint32_t>(limiter_.lookahead());
    shifter_.pull(raw_.data(), lookahead);
    limiter_.process(raw_.data(), block_.data(), lookahead);
    if (b == 0) return;
    pullLimited(lead_.data(), BeatRenderer::kLeadInFrames);
    leadIns_.push_back({b, lead_});
  }

  void step() {
    if (tt_->cancelled.load(std::memory_order_relaxed)) return;
    // Tua tới vùng chưa có → ưu tiên chỗ đó.
    const int64_t want = tt_->wantFrame.exchange(-1, std::memory_order_relaxed);
    if (want >= 0) {
      const auto wb = static_cast<uint32_t>(
          std::min<uint64_t>(static_cast<uint64_t>(want) / kBlock, tt_->blocks - 1));
      if (!ready(wb) && !(inPass_ && wb == next_)) startPass(wb);
    }
    if (!inPass_) {
      uint32_t b = 0;
      while (b < tt_->blocks && ready(b)) ++b;
      if (b == tt_->blocks) {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_)
                .count();
        if (onDone_) onDone_(tt_->semitones, ms);
        return;
      }
      startPass(b);
    }
    for (uint32_t i = 0; i < BeatRenderer::kBlocksPerStep && inPass_; ++i) renderBlock();
    if (onProgress_) onProgress_(tt_->progress());
    post();
  }

  // Render khối next_, nối với lượt sau nếu khối kế tiếp đã có, rồi công bố.
  void renderBlock() {
    const uint32_t b = next_;
    if (ready(b)) {  // gặp vùng của lượt trước (không xảy ra: đã kiểm trước khi công bố khối trước)
      inPass_ = false;
      return;
    }
    const uint64_t start = static_cast<uint64_t>(b) * kBlock;
    const auto n = static_cast<uint32_t>(std::min<uint64_t>(kBlock, tt_->frames - start));
    pullLimited(block_.data(), n);
    const bool meets = b + 1 < tt_->blocks && ready(b + 1);
    if (meets) blendInto(b + 1, n);
    int16_t* dst = tt_->pcm.get() + 2 * static_cast<std::size_t>(start);
    for (std::size_t i = 0; i < 2 * static_cast<std::size_t>(n); ++i) {
      const float v = std::clamp(block_[i] * TransposedTrack::kStoreGain, -1.0f, 1.0f);
      dst[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
    }
    tt_->ready[b].store(1, std::memory_order_release);
    tt_->readyCount.fetch_add(1, std::memory_order_relaxed);
    next_ = b + 1;
    if (meets || next_ >= tt_->blocks) inPass_ = false;
  }

  // n frame tiếp theo (đã qua limiter) vào `out`.
  void pullLimited(float* out, uint32_t n) {
    shifter_.pull(raw_.data(), n);
    limiter_.process(raw_.data(), out, n);
  }

  // Trộn đuôi khối vừa render (n frame) sang phần render trước của lượt bắt đầu ở `nextBlock`.
  void blendInto(uint32_t nextBlock, uint32_t n) {
    const LeadIn* lead = nullptr;
    for (const auto& l : leadIns_) {
      if (l.block == nextBlock) lead = &l;
    }
    if (lead == nullptr) return;
    const uint32_t len = std::min(n, BeatRenderer::kLeadInFrames);
    const uint32_t off = n - len;
    const std::size_t leadOff = BeatRenderer::kLeadInFrames - len;
    for (uint32_t i = 0; i < len; ++i) {
      const auto w =
          static_cast<float>(0.5 - 0.5 * std::cos(dsp::kPi * (static_cast<double>(i) + 0.5) /
                                                  static_cast<double>(len)));
      for (std::size_t c = 0; c < 2; ++c) {
        float& mine = block_[2 * static_cast<std::size_t>(off + i) + c];
        mine += w * (lead->samples[2 * (leadOff + i) + c] - mine);
      }
    }
  }

  Worker& worker_;
  std::shared_ptr<TransposedTrack> tt_;
  PitchShifter shifter_;
  PeakLimiter limiter_;     // đỉnh sau đổi pha có thể vượt 0 dBFS → giới hạn ngay khi render
  std::vector<float> raw_;  // đầu ra PitchShifter trước limiter
  std::vector<float> block_;
  std::vector<float> lead_;
  std::vector<LeadIn> leadIns_;
  bool inPass_ = false;
  uint32_t next_ = 0;
  BeatRenderer::ProgressFn onProgress_;
  BeatRenderer::DoneFn onDone_;
  std::chrono::steady_clock::time_point t0_;
};

}  // namespace

void BeatRenderer::start(Worker& worker, std::shared_ptr<TransposedTrack> track, uint64_t fromFrame,
                         double sampleRate, ProgressFn onProgress, DoneFn onDone) {
  if (!track || track->blocks == 0) return;
  auto job = std::make_shared<RenderJob>(worker, std::move(track), fromFrame, sampleRate,
                                         std::move(onProgress), std::move(onDone));
  // Khối đầu tiên render ngay (người dùng đang chờ nghe tone mới), phần còn lại theo bước.
  job->post();
}

}  // namespace tonecore
