#include "key/key_timeline.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "key/cqt.h"
#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 12000.0;

struct Part {
  double sec;  // bội của 6 s (một vòng hoà âm 4 × 1,5 s); root < 0 = im lặng
  int root;
  bool minor;
};

std::vector<float> song(const std::vector<Part>& parts, uint32_t seed) {
  double total = 0.0;
  for (const Part& p : parts) total += p.sec;
  std::vector<float> x(static_cast<std::size_t>(kRate * total), 0.0f);
  test::NoteSpec spec;
  spec.harmonics = 4;
  double t = 0.0;
  for (const Part& p : parts) {
    if (p.root >= 0) {
      test::addCadence(x, kRate, t, p.root, p.minor, static_cast<int>(std::lround(p.sec / 6.0)),
                       1.5, seed++, spec);
    }
    t += p.sec;
  }
  return x;
}

std::vector<KeySegment> timelineOf(const std::vector<float>& x) {
  CqtSpectrogram s;
  EXPECT_EQ(computeCqt(x, kRate, CqtConfig{}, nullptr, s), TC_OK);
  const Chromagram ch = computeChroma(s);
  const KeyDetectConfig keyCfg;
  const auto global = estimateGlobalKey(ch, keyCfg);
  EXPECT_TRUE(global.has_value());
  if (!global) return {};
  return estimateKeyTimeline(ch, static_cast<double>(x.size()) / kRate, keyCfg, KeyTimelineConfig{},
                             *global);
}

std::string describe(const std::vector<KeySegment>& tl) {
  static const char* const kNames[12] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                         "F#", "G",  "Ab", "A",  "Bb", "B"};
  std::string s;
  for (const KeySegment& g : tl) {
    s += std::string(kNames[g.key.root]) + (g.key.mode == KeyMode::Minor ? "m" : "") + "[" +
         std::to_string(static_cast<int>(g.startSec)) + "–" +
         std::to_string(static_cast<int>(g.endSec)) + "] ";
  }
  return s;
}

// Timeline phủ kín [0, duration), liền mạch, độ tin cậy trong [0, 1].
void expectWellFormed(const std::vector<KeySegment>& tl, double duration) {
  ASSERT_FALSE(tl.empty());
  EXPECT_DOUBLE_EQ(tl.front().startSec, 0.0);
  EXPECT_DOUBLE_EQ(tl.back().endSec, duration);
  for (std::size_t i = 0; i < tl.size(); ++i) {
    EXPECT_LT(tl[i].startSec, tl[i].endSec);
    EXPECT_GE(tl[i].confidence, 0.0f);
    EXPECT_LE(tl[i].confidence, 1.0f);
    if (i > 0) {
      EXPECT_DOUBLE_EQ(tl[i].startSec, tl[i - 1].endSec);
      EXPECT_NE(tl[i].key, tl[i - 1].key);
    }
  }
}

void expectKeys(const std::vector<KeySegment>& tl, const std::vector<MusicalKey>& keys,
                const std::vector<double>& boundaries, double tolSec) {
  ASSERT_EQ(tl.size(), keys.size()) << describe(tl);
  for (std::size_t i = 0; i < keys.size(); ++i) EXPECT_EQ(tl[i].key, keys[i]) << describe(tl);
  for (std::size_t i = 0; i < boundaries.size(); ++i) {
    EXPECT_NEAR(tl[i + 1].startSec, boundaries[i], tolSec) << describe(tl);
  }
}

}  // namespace

TEST(KeyTimeline, RelatedKeys) {
  const MusicalKey c{0, KeyMode::Major};
  const MusicalKey am{9, KeyMode::Minor};
  for (const MusicalKey& k : {MusicalKey{7, KeyMode::Major}, MusicalKey{5, KeyMode::Major},
                              MusicalKey{9, KeyMode::Minor}, MusicalKey{0, KeyMode::Minor},
                              MusicalKey{1, KeyMode::Major}, MusicalKey{2, KeyMode::Major}}) {
    EXPECT_TRUE(relatedKeys(c, k)) << k.root;
  }
  EXPECT_FALSE(relatedKeys(c, c));
  EXPECT_FALSE(relatedKeys(c, {4, KeyMode::Major}));
  EXPECT_FALSE(relatedKeys(c, {4, KeyMode::Minor}));
  EXPECT_TRUE(relatedKeys(am, {0, KeyMode::Major}));   // tương đối
  EXPECT_TRUE(relatedKeys(am, {9, KeyMode::Major}));   // song song
  EXPECT_TRUE(relatedKeys(am, {4, KeyMode::Minor}));   // quãng 5
  EXPECT_TRUE(relatedKeys(am, {10, KeyMode::Minor}));  // nâng nửa cung
}

// Kiểu bolero: vào bài La thứ, điệp khúc lên La trưởng.
TEST(KeyTimeline, ParallelModulationAmToA) {
  const auto x = song({{60, 9, true}, {42, 9, false}}, 1);
  const auto tl = timelineOf(x);
  expectWellFormed(tl, 102.0);
  expectKeys(tl, {{9, KeyMode::Minor}, {9, KeyMode::Major}}, {60.0}, 4.0);
}

// Nâng tone nửa cung cuối bài.
TEST(KeyTimeline, SemitoneLiftNearTheEnd) {
  const auto x = song({{60, 0, false}, {30, 1, false}}, 2);
  const auto tl = timelineOf(x);
  expectWellFormed(tl, 90.0);
  expectKeys(tl, {{0, KeyMode::Major}, {1, KeyMode::Major}}, {60.0}, 4.0);
}

// La thứ → Đô trưởng (tương đối, cùng hoá biểu) → La thứ.
TEST(KeyTimeline, RelativeModulationAndReturn) {
  const auto x = song({{42, 9, true}, {42, 0, false}, {42, 9, true}}, 3);
  const auto tl = timelineOf(x);
  expectWellFormed(tl, 126.0);
  expectKeys(tl, {{9, KeyMode::Minor}, {0, KeyMode::Major}, {9, KeyMode::Minor}}, {42.0, 84.0},
             6.0);
}

// Một vòng (6 s) mượn Sol thứ giữa bài Si giáng trưởng: ngắn hơn một câu → không tính chuyển tone.
TEST(KeyTimeline, ShortBorrowedPassageIsIgnored) {
  const auto x = song({{48, 10, false}, {6, 7, true}, {42, 10, false}}, 4);
  const auto tl = timelineOf(x);
  expectWellFormed(tl, 96.0);
  expectKeys(tl, {{10, KeyMode::Major}}, {}, 0.0);
}

// Khoảng lặng 10 s giữa bài: không sinh đoạn mới.
TEST(KeyTimeline, SilenceKeepsKey) {
  const auto x = song({{36, 6, true}, {10, -1, false}, {36, 6, true}}, 5);
  const auto tl = timelineOf(x);
  expectWellFormed(tl, 82.0);
  expectKeys(tl, {{6, KeyMode::Minor}}, {}, 0.0);
}
