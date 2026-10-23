#pragma once

#include <cstddef>
#include <vector>

#include "key/analysis_common.h"

namespace tonecore {

// Biến đổi Constant-Q (CQT): các bin cách đều theo cao độ (B bin mỗi quãng 8), độ rộng dải tỉ lệ
// với tần số (Q = f / Δf không đổi) → nốt trầm (bass) vẫn tách được từng nửa cung, điều STFT
// cửa sổ ngắn không làm được.
//
// Cách tính nhanh theo C. Schörkhuber, A. Klapuri, "Constant-Q transform toolbox for music
// processing", SMC 2010, với kernel phổ thưa của J. C. Brown, M. S. Puckette, "An efficient
// algorithm for the calculation of a constant Q transform", JASA 1992:
//  1. Kernel cho MỘT quãng 8 (quãng cao nhất): mỗi bin b là sóng phức e^{i2πν_b n} nhân cửa sổ
//     Hann dài N_b = Q / ν_b mẫu. Đổi sang miền tần số (DFT dài L) và bỏ các hệ số rất nhỏ →
//     ma trận thưa; hệ số CQT = Σ_j X[j]·K_b*[j] / L (định lý Parseval), X = FFT của khung.
//  2. Quãng 8 thấp hơn: lọc thông thấp + hạ mẫu ÷2 rồi dùng LẠI đúng kernel đó (tần số chuẩn
//     hoá của quãng dưới sau khi hạ mẫu bằng đúng quãng trên).
//  3. Mỗi quãng 8 có bước khung `hopTop` mẫu theo tần số của chính nó (≤ ½ cửa sổ ngắn nhất →
//     không bỏ sót mẫu nào). Quãng thấp nhất có bước dài nhất = khoảng cách khung đầu ra; công
//     suất các khung dày hơn của quãng cao được lấy trung bình vào khung đầu ra gần nhất (đã bù độ
//     trễ của các bộ lọc hạ mẫu).
// Ngoài audio thread (có cấp phát).
struct CqtConfig {
  int binsPerOctave = 36;  // 3 bin mỗi nửa cung: đủ để thấy độ lệch tuning
  int octaves = 6;
  // MIDI (số thực) của bin thấp nhất. Mặc định E1 − 1/3 nửa cung → bin 3n + 1 rơi đúng tâm nốt
  // MIDI 28 + n (Mi1 = 41,2 Hz); bin cao nhất ≈ Rê#7 (2,5 kHz).
  double minMidi = 28.0 - 1.0 / 3.0;
  float tuningCents = 0.0f;         // dời cả lưới bin theo tuning của bài
  std::size_t hopTop = 128;         // bước khung (mẫu) ở mỗi quãng 8, theo tần số của quãng đó
  int decimatorTaps = 24;           // FIR hạ mẫu ÷2 (dải cần giữ chỉ tới ~0,21 tần số mới)
  float kernelThreshold = 0.0054f;  // bỏ hệ số kernel nhỏ hơn mức này × đỉnh (Schörkhuber 2010)
};

struct CqtSpectrogram {
  int binsPerOctave = 0;
  int bins = 0;
  double minMidi = 0.0;
  float tuningCents = 0.0f;
  double frameSec = 0.0;  // khung i có tâm tại i · frameSec
  std::size_t frames = 0;
  // frames × bins. Công suất: sóng sin biên độ A đúng tần số bin → A²/4.
  std::vector<float> power;

  const float* frame(std::size_t i) const {
    return power.data() + i * static_cast<std::size_t>(bins);
  }
  // Cao độ (MIDI thực, đã gồm tuning) của bin k.
  double binMidi(int k) const {
    return minMidi + 12.0 * k / binsPerOctave + static_cast<double>(tuningCents) / 100.0;
  }
};

// Trả về TC_OK, kResultCancelled hoặc TC_ERR_INVALID_ARG (cấu hình sai, bin cao nhất vượt
// ~0,45 tần số lấy mẫu).
tc_result computeCqt(const std::vector<float>& x, double rate, const CqtConfig& cfg,
                     const ProgressFn& onProgress, CqtSpectrogram& out);

}  // namespace tonecore
