#pragma once

// Đo khách quan cho bộ FX giọng (M5): đáp ứng tần số, RT60, đường nén tĩnh, độ "lụp bụp".
// Dùng chung cho unit test và `fxlab`. Mọi hàm nhận một bộ xử lý mono tại chỗ
// `process(float* x, uint32_t n)` (khối ≤ 256 mẫu) hoặc tín hiệu đã có.

#include <cstdint>
#include <functional>
#include <vector>

namespace tonecore::test {

using MonoProcess = std::function<void(float* x, uint32_t n)>;

// Đáp ứng xung (n mẫu) của bộ xử lý, cho chạy theo khối `block` mẫu.
std::vector<float> impulseResponse(const MonoProcess& process, std::size_t n, uint32_t block = 256);

// |H| (dB) tại f (Hz) từ đáp ứng xung: DFT tại đúng tần số f.
double magnitudeDb(const std::vector<float>& ir, double f, double sampleRate);

// 48 tần số cách đều theo log từ 20 Hz tới 20 kHz.
std::vector<double> logFrequencies(int count = 48, double from = 20.0, double to = 20000.0);

// RT60 (giây) theo Schroeder: tích phân ngược năng lượng đáp ứng xung (EDC), khớp thẳng đoạn
// −5…−35 dB rồi ngoại suy ra 60 dB (T30). bandLow/bandHigh > 0: lọc băng trước (bậc 2 hai lần).
double rt60(const std::vector<float>& ir, double sampleRate, double bandLow = 0.0,
            double bandHigh = 0.0);

// Đường nén tĩnh: sin `freq` ở từng mức vào (dBFS đỉnh) chạy `settleSec`, đo RMS của 100 ms cuối
// → mức ra (dBFS đỉnh tương đương: RMS + 3,01 dB).
std::vector<double> staticCurveDb(const MonoProcess& process, const std::vector<double>& levelsDb,
                                  double freq, double sampleRate, double settleSec = 0.5);

// Hệ số tương quan Pearson của a và b trên [from, to).
double correlation(const std::vector<float>& a, const std::vector<float>& b, std::size_t from,
                   std::size_t to);

// Độ "nhô" của phổ (dB) — dò tiếng vang "kim loại" (vài mode ngân trội): phổ công suất trung bình
// Welch (đoạn 4096, Hann, chồng 50%, mỗi đoạn chuẩn hoá theo năng lượng của nó) của x[from, to) so
// với trung bình trượt ±1/6 quãng tám quanh mỗi bin; trả mức nhô lớn nhất trong [loHz, hiHz].
double spectralPeakinessDb(const std::vector<float>& x, double sampleRate, std::size_t from,
                           std::size_t to, double loHz, double hiHz);

// Năng lượng trên `cutoffHz` so với tổng (dB) — đo tiếng "lụp bụp" (zipper) khi đầu vào chỉ có
// thành phần thấp hơn cutoff. Lọc thông cao bậc 4 (hai SVF Butterworth).
double energyAboveDb(const std::vector<float>& x, double cutoffHz, double sampleRate);

}  // namespace tonecore::test
