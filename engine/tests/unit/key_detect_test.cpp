#include "key/key_detect.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "key/chroma.h"
#include "key/cqt.h"
#include "test_util.h"
#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 12000.0;

Chromagram chromaOf(const std::vector<float>& x, float tuningCents = 0.0f) {
  CqtConfig cfg;
  cfg.tuningCents = tuningCents;
  CqtSpectrogram s;
  EXPECT_EQ(computeCqt(x, kRate, cfg, nullptr, s), TC_OK);
  return computeChroma(s);
}

std::string name(const MusicalKey& k) {
  static const char* const kNames[12] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                         "F#", "G",  "Ab", "A",  "Bb", "B"};
  return std::string(kNames[k.root]) + (k.mode == KeyMode::Minor ? "m" : "");
}

}  // namespace

// Chroma đúng bằng profile xoay tới tone T → mọi profile phải trả về T (kiểm chiều xoay, chỉ số).
TEST(KeyDetect, ProfileRotationSelfConsistent) {
  for (const KeyProfile& p : builtinKeyProfiles()) {
    for (int i = 0; i < 24; ++i) {
      const MusicalKey k = keyFromIndex(i);
      const auto& base = k.mode == KeyMode::Minor ? p.minor : p.major;
      const auto& bassOpt = k.mode == KeyMode::Minor ? p.bassMinor : p.bassMajor;
      const auto& bassBase = bassOpt ? *bassOpt : base;
      ChromaVector v{};
      ChromaVector b{};
      for (int pc = 0; pc < 12; ++pc) {
        const auto deg = static_cast<std::size_t>((pc - k.root + 12) % 12);
        v[static_cast<std::size_t>(pc)] = base[deg];
        b[static_cast<std::size_t>(pc)] = bassBase[deg];
      }
      const KeyEstimate est = pickKey(scoreKeys(b, v, p, 0.25f), KeyDetectConfig{});
      EXPECT_EQ(est.key, k) << p.name << " " << name(k) << " → " << name(est.key);
      EXPECT_NEAR(est.scores[static_cast<std::size_t>(i)], 1.0f, 1e-5f);
    }
  }
}

// Vòng hoà âm I–IV–V–I / i–iv–V–i ở cả 24 tone, voicing ngẫu nhiên: mọi profile (trừ mốc so sánh
// "diatonic") đúng 24/24.
TEST(KeyDetect, CadencesInAll24Keys) {
  const auto& profiles = builtinKeyProfiles();
  std::vector<int> correct(profiles.size(), 0);
  for (int i = 0; i < 24; ++i) {
    const MusicalKey truth = keyFromIndex(i);
    std::vector<float> x(static_cast<std::size_t>(kRate * 12.0), 0.0f);
    test::addCadence(x, kRate, 0.0, truth.root, truth.mode == KeyMode::Minor, 2, 1.5,
                     static_cast<uint32_t>(100 + i));
    const Chromagram ch = chromaOf(x);
    for (std::size_t p = 0; p < profiles.size(); ++p) {
      KeyDetectConfig cfg;
      cfg.profile = profiles[p].name;
      const auto est = estimateGlobalKey(ch, cfg);
      ASSERT_TRUE(est.has_value());
      if (est->key == truth) {
        ++correct[p];
      } else {
        ADD_FAILURE() << profiles[p].name << ": " << name(truth) << " → " << name(est->key);
      }
    }
  }
  for (std::size_t p = 0; p < profiles.size(); ++p) {
    if (profiles[p].name == "diatonic") continue;  // mốc so sánh, không đòi hỏi
    EXPECT_EQ(correct[p], 24) << profiles[p].name;
  }
}

// Treble C–E–G–A (vừa là C6 vừa là Am7): chỉ nốt bass phân định C trưởng hay La thứ.
TEST(KeyDetect, BassResolvesRelativeAmbiguity) {
  for (const auto& [bassMidi, want] :
       {std::pair<int, MusicalKey>{45, {9, KeyMode::Minor}}, {48, {0, KeyMode::Major}}}) {
    std::vector<float> x(static_cast<std::size_t>(kRate * 8.0), 0.0f);
    for (const int m : {60, 64, 67, 69}) test::addNote(x, kRate, 0.0, 8.0, m, 0.06f);
    test::addNote(x, kRate, 0.0, 8.0, bassMidi, 0.1f);
    const Chromagram ch = chromaOf(x);
    KeyDetectConfig cfg;
    cfg.bassWeight = 0.5f;
    const auto est = estimateGlobalKey(ch, cfg);
    ASSERT_TRUE(est.has_value());
    EXPECT_EQ(est->key, want) << "bass " << bassMidi << " → " << name(est->key);
  }
}

TEST(KeyDetect, SilenceOrTooShortHasNoKey) {
  KeyDetectConfig cfg;
  EXPECT_FALSE(
      estimateGlobalKey(chromaOf(std::vector<float>(static_cast<std::size_t>(kRate * 6))), cfg));
  std::vector<float> x(static_cast<std::size_t>(kRate * 10.0), 0.0f);
  test::addCadence(x, kRate, 0.0, 0, false, 1, 0.25, 1);  // chỉ 1 s có tiếng (CQT nhoè thêm ~1 s)
  EXPECT_FALSE(estimateGlobalKey(chromaOf(x), cfg));
  cfg.profile = "khong-co";
  test::addCadence(x, kRate, 2.0, 0, false, 1, 2.0, 1);
  EXPECT_FALSE(estimateGlobalKey(chromaOf(x), cfg));
}

// Vòng hoà âm rõ ràng → tin cậy cao; cụm đủ 12 nốt → tin cậy thấp.
TEST(KeyDetect, ConfidenceReflectsClarity) {
  std::vector<float> clear(static_cast<std::size_t>(kRate * 12.0), 0.0f);
  test::addCadence(clear, kRate, 0.0, 2, false, 2, 1.5, 9);
  const auto a = estimateGlobalKey(chromaOf(clear), KeyDetectConfig{});
  ASSERT_TRUE(a.has_value());

  std::vector<float> cluster(static_cast<std::size_t>(kRate * 12.0), 0.0f);
  for (int m = 60; m < 72; ++m) test::addNote(cluster, kRate, 0.0, 12.0, m, 0.05f);
  const auto b = estimateGlobalKey(chromaOf(cluster), KeyDetectConfig{});
  ASSERT_TRUE(b.has_value());
  EXPECT_GT(a->confidence, 0.5f);
  EXPECT_LT(b->confidence, 0.3f);
  EXPECT_GT(a->confidence, b->confidence);
}

TEST(KeyDetect, LoadProfileFile) {
  const std::string path = test::tempPath("profile.txt");
  auto write = [&](const std::string& text) { std::ofstream(path) << text; };
  write(
      "# hoc tu tap dev\n"
      "6, 2, 3.5, 2, 4.5, 4, 2, 5, 2, 3.5, 1.5, 3\n"
      "\n"
      "6 2 3.5 5 2 4 2 5 4 2 3 3\n");
  KeyProfile p;
  ASSERT_TRUE(loadKeyProfileFile(path, p));
  EXPECT_FLOAT_EQ(p.major[0], 6.0f);
  EXPECT_FLOAT_EQ(p.minor[3], 5.0f);
  EXPECT_FALSE(p.bassMajor.has_value());

  write(
      "1 0 0 0 0 0 0 0 0 0 0 0\n0 0 0 1 0 0 0 0 0 0 0 0\n"
      "1 0 0 0 0 0 0 0.5 0 0 0 0\n1 0 0 0 0 0 0 0.5 0 0 0 0\n");
  ASSERT_TRUE(loadKeyProfileFile(path, p));
  ASSERT_TRUE(p.bassMinor.has_value());
  EXPECT_FLOAT_EQ((*p.bassMinor)[7], 0.5f);

  for (const char* bad : {"1 2 3\n1 2 3\n",                                        // thiếu số
                          "1 1 1 1 1 1 1 1 1 1 1 1\n",                             // 1 dòng
                          "1 1 1 1 1 1 1 1 1 1 1 1\n1 1 1 1 1 1 1 1 1 1 1 1 9\n",  // thừa số
                          "1 1 1 1 1 1 1 1 1 1 1 x\n1 1 1 1 1 1 1 1 1 1 1 1\n"}) {
    write(bad);
    EXPECT_FALSE(loadKeyProfileFile(path, p)) << bad;
  }
  EXPECT_FALSE(loadKeyProfileFile("/khong/co/profile.txt", p));
  std::remove(path.c_str());
}

// Profile bass riêng được dùng cho dải bass: bass chỉ có nốt La; profile bass "một nốt chủ âm"
// → điểm bass cao nhất ở tone có chủ âm La (cả trưởng lẫn thứ), không phụ thuộc profile treble.
TEST(KeyDetect, SeparateBassProfile) {
  KeyProfile p = *findKeyProfile("temperley-kp");
  std::array<float, 12> tonicOnly{};
  tonicOnly[0] = 1.0f;
  p.bassMajor = tonicOnly;
  p.bassMinor = tonicOnly;
  ChromaVector bass{};
  bass[9] = 1.0f;
  const ChromaVector flat{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  const KeyScores s = scoreKeys(bass, flat, p, 1.0f);
  EXPECT_NEAR(s[9], 1.0f, 1e-5f);       // A trưởng
  EXPECT_NEAR(s[12 + 9], 1.0f, 1e-5f);  // La thứ
  EXPECT_LT(s[0], 0.0f);
}

// Treble C–E–G–A suốt bài (C6 / Am7); bass Đô 7 s rồi La 3 s cuối. Không có gợi ý cuối bài →
// Đô trưởng (bass Đô nhiều hơn); bật gợi ý → La thứ (bài kết về La).
TEST(KeyDetect, EndingTonicHint) {
  std::vector<float> x(static_cast<std::size_t>(kRate * 10.0), 0.0f);
  for (const int m : {60, 64, 67, 69}) test::addNote(x, kRate, 0.0, 10.0, m, 0.06f);
  test::addNote(x, kRate, 0.0, 7.0, 48, 0.1f);
  test::addNote(x, kRate, 7.0, 3.0, 45, 0.1f);
  const Chromagram ch = chromaOf(x);
  KeyDetectConfig cfg;
  const auto plain = estimateGlobalKey(ch, cfg);
  ASSERT_TRUE(plain.has_value());
  EXPECT_EQ(plain->key, (MusicalKey{0, KeyMode::Major})) << name(plain->key);
  const std::vector<uint8_t> voiced = voicedFrames(ch, cfg.silenceDb);
  EXPECT_EQ(endingBassPitchClass(ch, voiced, 2.0f), 9);
  cfg.endWeight = 0.3f;
  cfg.endSec = 2.0f;
  const auto hinted = estimateGlobalKey(ch, cfg);
  ASSERT_TRUE(hinted.has_value());
  EXPECT_EQ(hinted->key, (MusicalKey{9, KeyMode::Minor})) << name(hinted->key);
}

// Phân xử tương đối bằng bass: Bb trưởng thắng sát nút Sol thứ, bass ở G mạnh hơn Bb → Sol thứ;
// chênh điểm lớn hơn margin, hoặc bass ở Bb mạnh hơn → giữ Bb trưởng.
TEST(KeyDetect, RelativeResolvedByBass) {
  KeyScores s{};
  s[10] = 0.80f;      // Bb trưởng
  s[12 + 7] = 0.78f;  // Sol thứ
  ChromaVector bass{};
  bass[7] = 0.17f;   // G
  bass[10] = 0.12f;  // Bb
  KeyScores a = s;
  resolveRelativeByBass(a, bass, 0.05f);
  EXPECT_EQ(pickKey(a, KeyDetectConfig{}).key, (MusicalKey{7, KeyMode::Minor}));
  KeyScores b = s;
  resolveRelativeByBass(b, bass, 0.01f);  // chênh 0,02 ≥ margin → không phân xử
  EXPECT_EQ(pickKey(b, KeyDetectConfig{}).key, (MusicalKey{10, KeyMode::Major}));
  ChromaVector bassMajor{};
  bassMajor[7] = 0.10f;
  bassMajor[10] = 0.20f;
  KeyScores c = s;
  resolveRelativeByBass(c, bassMajor, 0.05f);
  EXPECT_EQ(pickKey(c, KeyDetectConfig{}).key, (MusicalKey{10, KeyMode::Major}));
}
