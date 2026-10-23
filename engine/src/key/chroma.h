#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "key/cqt.h"

namespace tonecore {

// Chroma: năng lượng gộp theo 12 lớp cao độ (Đô, Đô#, … Si), bỏ qua quãng 8. Tách 2 dải:
//  - bass: các nốt trầm (≤ ~Đô3, 131 Hz; giảm dần tới Đô4) — nốt bass hay là chủ âm/bậc V, giúp
//    phân biệt trưởng/thứ tương đối (C ↔ Am) là chỗ dò tone hay nhầm nhất;
//  - treble: phần còn lại (giai điệu, hợp âm).
// Chuyển tiếp tuyến tính giữa bassFullMidi và trebleFullMidi để một nốt không nhảy cụt từ dải này
// sang dải kia.
//
// Mỗi nốt = tổng công suất 3 bin CQT quanh tâm nốt (±1/3 nửa cung, đã dời theo tuning). Nén log
// c = log(1 + γ·E / E_ref) (M. Müller, "Fundamentals of Music Processing", 2015, mục 3.1) với
// E_ref = công suất khung ở phân vị 95 của cả bài → không phụ thuộc âm lượng bài, nốt to không
// át hẳn nốt nhỏ.
struct ChromaConfig {
  double bassFullMidi = 48.0;    // ≤ Đô3: hoàn toàn thuộc bass
  double trebleFullMidi = 60.0;  // ≥ Đô4: hoàn toàn thuộc treble
  float compression = 100.0f;    // γ
};

using ChromaVector = std::array<float, 12>;  // chỉ số 0 = Đô (C) … 11 = Si (B)

struct Chromagram {
  double frameSec = 0.0;  // khung i có tâm tại i · frameSec
  std::vector<ChromaVector> bass;
  std::vector<ChromaVector> treble;
  std::vector<float> energyDb;  // tổng công suất CQT của khung (dB), để bỏ khung im lặng

  std::size_t frames() const { return bass.size(); }
};

// binsPerOctave của CQT phải là bội của 12.
Chromagram computeChroma(const CqtSpectrogram& cqt, const ChromaConfig& cfg = {});

}  // namespace tonecore
