#include "codec/flac_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace tonecore {

namespace {

// Ghi bit từ bit cao xuống (thứ tự byte lớn trước, như mọi trường của FLAC) vào vector byte.
class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

  // `bits` (0…32) bit thấp của v.
  void put(uint32_t v, int bits) {
    if (bits == 0) return;
    const uint64_t mask = bits == 32 ? 0xFFFFFFFFull : (1ull << bits) - 1;
    acc_ = (acc_ << bits) | (v & mask);
    n_ += bits;
    while (n_ >= 8) {
      n_ -= 8;
      out_.push_back(static_cast<uint8_t>(acc_ >> n_));
    }
    acc_ &= (1ull << n_) - 1;
  }
  // Số có dấu bù hai, `bits` bit thấp.
  void putSigned(int32_t v, int bits) { put(static_cast<uint32_t>(v), bits); }
  // q bit 0 rồi một bit 1.
  void putUnary(uint32_t q) {
    while (q >= 32) {
      put(0, 32);
      q -= 32;
    }
    put(1, static_cast<int>(q) + 1);
  }
  void alignZero() {
    if (n_ > 0) put(0, 8 - n_);
  }

 private:
  std::vector<uint8_t>& out_;
  uint64_t acc_ = 0;
  int n_ = 0;
};

uint8_t crc8(const uint8_t* p, std::size_t n) {
  // Đa thức x⁸ + x² + x + 1, khởi tạo 0 (RFC 9639 mục 9.1).
  uint8_t c = 0;
  for (std::size_t i = 0; i < n; ++i) {
    c = static_cast<uint8_t>(c ^ p[i]);
    for (int b = 0; b < 8; ++b) {
      c = (c & 0x80) != 0 ? static_cast<uint8_t>((c << 1) ^ 0x07) : static_cast<uint8_t>(c << 1);
    }
  }
  return c;
}

const std::array<uint16_t, 256>& crc16Table() {
  static const std::array<uint16_t, 256> table = [] {
    std::array<uint16_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i << 8;
      for (int b = 0; b < 8; ++b) c = (c & 0x8000u) != 0 ? (c << 1) ^ 0x8005u : c << 1;
      t[i] = static_cast<uint16_t>(c);
    }
    return t;
  }();
  return table;
}

uint16_t crc16(const uint8_t* p, std::size_t n) {
  // Đa thức x¹⁶ + x¹⁵ + x² + 1, khởi tạo 0 (RFC 9639 mục 9.3).
  const auto& t = crc16Table();
  uint32_t c = 0;
  for (std::size_t i = 0; i < n; ++i) c = ((c << 8) ^ t[((c >> 8) ^ p[i]) & 0xFFu]) & 0xFFFFu;
  return static_cast<uint16_t>(c);
}

// Số thứ tự khung, mã như UTF-8 mở rộng (RFC 9639 mục 9.1.5).
void putCodedNumber(BitWriter& w, uint64_t v) {
  if (v < 0x80) {
    w.put(static_cast<uint32_t>(v), 8);
    return;
  }
  int bytes = 2;
  while (bytes < 7 && v >= (1ull << (5 * bytes + 1))) ++bytes;
  const uint32_t lead = (0xFF00u >> bytes) & 0xFFu;
  w.put(lead | static_cast<uint32_t>(v >> (6 * (bytes - 1))), 8);
  for (int i = bytes - 2; i >= 0; --i) {
    w.put(0x80u | static_cast<uint32_t>((v >> (6 * i)) & 0x3Fu), 8);
  }
}

// Gấp dấu: 0, −1, 1, −2, 2… → 0, 1, 2, 3, 4…
inline uint32_t fold(int32_t e) {
  return e >= 0 ? static_cast<uint32_t>(e) * 2u : static_cast<uint32_t>(-(e + 1)) * 2u + 1u;
}

// Phần dư của bộ dự đoán cố định bậc `order` tại mẫu i (i ≥ order): sai phân bậc `order`.
inline int32_t fixedResidual(const int32_t* x, uint32_t i, int order) {
  switch (order) {
    case 0:
      return x[i];
    case 1:
      return x[i] - x[i - 1];
    case 2:
      return x[i] - 2 * x[i - 1] + x[i - 2];
    case 3:
      return x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3];
    default:
      return x[i] - 4 * x[i - 1] + 6 * x[i - 2] - 4 * x[i - 3] + x[i - 4];
  }
}

struct RiceChoice {
  int k = 0;
  uint64_t bits = 0;
};

// Tham số Rice tốt nhất cho một vùng `count` số với tổng `sum` (số bit ước: count·(k + 1) +
// sum/2^k).
RiceChoice bestRice(uint64_t sum, uint64_t count, int maxK) {
  if (count == 0) return {0, 0};
  int k0 = 0;
  while (k0 < maxK && (count << (k0 + 1)) <= sum) ++k0;
  RiceChoice best{k0, std::numeric_limits<uint64_t>::max()};
  for (int k = std::max(0, k0 - 1); k <= std::min(maxK, k0 + 1); ++k) {
    const uint64_t bits = count * static_cast<uint64_t>(k + 1) + (sum >> k);
    if (bits < best.bits) best = {k, bits};
  }
  return best;
}

enum class SubframeType { Constant, Verbatim, Fixed, Lpc };

struct ResidualPlan {
  int partitionOrder = 0;
  bool rice2 = false;
  std::vector<int> params;
  uint64_t bits = std::numeric_limits<uint64_t>::max();  // gồm 6 bit phương pháp + bậc chia vùng
};

struct SubframePlan {
  SubframeType type = SubframeType::Verbatim;
  int order = 0;               // bậc dự đoán: FIXED 0–4, LPC 1–kLpcMaxOrder
  int precision = 0;           // LPC: số bit mỗi hệ số
  int shift = 0;               // LPC: số bit dịch phải tổng dự đoán
  std::vector<int32_t> coefs;  // LPC: hệ số đã lượng tử; coefs[j] nhân x[i − j − 1]
  ResidualPlan residual;
  std::vector<uint32_t> u;  // phần dư đã gấp dấu (n − order số)
  uint64_t bits = 0;
};

// Bậc chia vùng p và tham số Rice từng vùng cho phần dư u (của mẫu order…n − 1 trong khối n mẫu).
ResidualPlan planResidual(const std::vector<uint32_t>& u, uint32_t n, uint32_t order,
                          int maxPartitionOrder) {
  // p hợp lệ: n chia hết cho 2^p và mỗi vùng dài hơn bậc dự đoán.
  int pMax = 0;
  while (pMax < maxPartitionOrder && (n % (2u << pMax)) == 0 && (n >> (pMax + 1)) > order) ++pMax;
  // Tổng / số phần tử ở mức mịn nhất, gộp đôi dần cho các mức thô hơn.
  const uint32_t partLen = n >> pMax;
  std::vector<uint64_t> sums(1u << pMax, 0);
  std::vector<uint64_t> counts(1u << pMax, 0);
  for (uint32_t i = order; i < n; ++i) {
    sums[i / partLen] += u[i - order];
    counts[i / partLen] += 1;
  }
  ResidualPlan best;
  for (int p = pMax; p >= 0; --p) {
    for (const bool rice2 : {false, true}) {
      const int maxK = rice2 ? 30 : 14;
      uint64_t bits = 6;  // phương pháp (2) + bậc chia vùng (4)
      for (std::size_t j = 0; j < sums.size(); ++j) {
        bits += (rice2 ? 5u : 4u) + bestRice(sums[j], counts[j], maxK).bits;
      }
      if (bits < best.bits) {
        best.bits = bits;
        best.partitionOrder = p;
        best.rice2 = rice2;
        best.params.clear();
        for (std::size_t j = 0; j < sums.size(); ++j) {
          best.params.push_back(bestRice(sums[j], counts[j], maxK).k);
        }
      }
    }
    if (p > 0) {  // gộp đôi → mức p − 1
      for (std::size_t j = 0; j < sums.size() / 2; ++j) {
        sums[j] = sums[2 * j] + sums[2 * j + 1];
        counts[j] = counts[2 * j] + counts[2 * j + 1];
      }
      sums.resize(sums.size() / 2);
      counts.resize(counts.size() / 2);
    }
  }
  return best;
}

constexpr int kLpcMaxOrder = 8;
constexpr int kLpcPrecision = 12;  // bit mỗi hệ số, như libFLAC với khối 4096 mẫu

// Dự đoán tuyến tính (J. Makhoul, "Linear prediction: A tutorial review", Proc. IEEE 1975):
//  1. Nhân khối với cửa sổ Tukey (α = 0,5: phẳng ở giữa, cosin ở hai đầu) → tự tương quan R[0…p].
//  2. Levinson–Durbin: bậc m tính từ bậc m − 1,
//       k = (R[m] − Σ a_j R[m − j]) / E,  a_m = k,  a_j ← a_j − k·a_{m−j},  E ← E·(1 − k²)
//     → hệ số mọi bậc 1…p cùng năng lượng sai số dự đoán E_m.
//  3. Chọn bậc theo ước số bit: ½·n·log2(E_m) (phần dư) + m·(bps + độ chính xác) (hệ số + mẫu đầu).
// Trả về bậc đã chọn (0 nếu không dùng được) và hệ số thực của bậc đó.
int chooseLpc(const int32_t* x, uint32_t n, int bps, std::array<double, kLpcMaxOrder>& coefs) {
  const int maxOrder = static_cast<int>(std::min<uint32_t>(kLpcMaxOrder, n > 1 ? n - 1 : 0));
  if (maxOrder < 1 || n < 32) return 0;
  std::vector<double> w(n);
  const double last = static_cast<double>(n - 1);
  const double edge = 0.25 * last;  // α·N/2 với α = 0,5
  for (uint32_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i);
    double win = 1.0;
    if (t < edge) {
      win = 0.5 * (1.0 - std::cos(M_PI * t / edge));
    } else if (t > last - edge) {
      win = 0.5 * (1.0 - std::cos(M_PI * (last - t) / edge));
    }
    w[i] = static_cast<double>(x[i]) * win;
  }
  std::array<double, kLpcMaxOrder + 1> r{};
  for (int lag = 0; lag <= maxOrder; ++lag) {
    double acc = 0.0;
    for (uint32_t i = static_cast<uint32_t>(lag); i < n; ++i)
      acc += w[i] * w[i - static_cast<uint32_t>(lag)];
    r[static_cast<std::size_t>(lag)] = acc;
  }
  if (!(r[0] > 0.0)) return 0;
  std::array<double, kLpcMaxOrder> a{};
  std::array<double, kLpcMaxOrder> prev{};
  double e = r[0];
  int bestOrder = 0;
  double bestCost = 0.5 * static_cast<double>(n) * std::log2(e);  // "bậc 0"
  for (int m = 1; m <= maxOrder; ++m) {
    double acc = r[static_cast<std::size_t>(m)];
    for (int j = 1; j < m; ++j)
      acc -= a[static_cast<std::size_t>(j - 1)] * r[static_cast<std::size_t>(m - j)];
    const double k = acc / e;
    prev = a;
    a[static_cast<std::size_t>(m - 1)] = k;
    for (int j = 1; j < m; ++j) {
      a[static_cast<std::size_t>(j - 1)] =
          prev[static_cast<std::size_t>(j - 1)] - k * prev[static_cast<std::size_t>(m - j - 1)];
    }
    e *= 1.0 - k * k;
    if (!(e > 0.0)) break;  // dự đoán hoàn hảo / suy biến: dừng ở bậc trước
    const double cost = 0.5 * static_cast<double>(n) * std::log2(e) +
                        static_cast<double>(m * (bps + kLpcPrecision));
    if (cost < bestCost) {
      bestCost = cost;
      bestOrder = m;
      coefs = a;
    }
  }
  return bestOrder;
}

// Lượng tử hệ số về số nguyên `precision` bit với tổng dịch phải `shift` (cách của libFLAC, có
// khuếch tán sai số lượng tử sang hệ số kế tiếp). false nếu cần shift âm (RFC 9639 không cho phép).
bool quantizeLpc(const std::array<double, kLpcMaxOrder>& c, int order, int precision,
                 std::vector<int32_t>& q, int& shift) {
  double cmax = 0.0;
  for (int j = 0; j < order; ++j) cmax = std::max(cmax, std::abs(c[static_cast<std::size_t>(j)]));
  if (!(cmax > 0.0)) return false;
  int exp2 = 0;
  std::frexp(cmax, &exp2);  // cmax ∈ [2^(exp2 − 1), 2^exp2)
  shift = std::min(15, precision - exp2 - 1);
  if (shift < 0) return false;
  const int32_t qmax = (1 << (precision - 1)) - 1;
  const int32_t qmin = -(1 << (precision - 1));
  q.resize(static_cast<std::size_t>(order));
  double carry = 0.0;
  for (int j = 0; j < order; ++j) {
    const double v = c[static_cast<std::size_t>(j)] * std::ldexp(1.0, shift) + carry;
    const auto qi = static_cast<int32_t>(std::clamp<long>(std::lround(v), qmin, qmax));
    carry = v - static_cast<double>(qi);
    q[static_cast<std::size_t>(j)] = qi;
  }
  return true;
}

// Chọn cách mã một kênh con: CONSTANT / FIXED bậc 0–4 / LPC / VERBATIM — cách nào ít bit nhất.
// allowLpc = false: chỉ dùng để so các cách ghép kênh stereo (nhanh).
void planSubframe(const int32_t* x, uint32_t n, int bps, int maxPartitionOrder, bool allowLpc,
                  SubframePlan& plan) {
  plan.u.clear();
  constexpr uint64_t kHeaderBits = 8;
  if (std::all_of(x, x + n, [&](int32_t v) { return v == x[0]; })) {
    plan.type = SubframeType::Constant;
    plan.bits = kHeaderBits + static_cast<uint64_t>(bps);
    return;
  }
  // FIXED: bậc có Σ|e| nhỏ nhất (tính từ mẫu thứ maxOrder cho mọi bậc — chỉ để so sánh).
  const int maxOrder = static_cast<int>(std::min<uint32_t>(4, n - 1));
  std::array<uint64_t, 5> absSum{};
  for (uint32_t i = static_cast<uint32_t>(maxOrder); i < n; ++i) {
    for (int o = 0; o <= maxOrder; ++o) {
      absSum[static_cast<std::size_t>(o)] += fold(fixedResidual(x, i, o));
    }
  }
  int order = 0;
  for (int o = 1; o <= maxOrder; ++o) {
    if (absSum[static_cast<std::size_t>(o)] < absSum[static_cast<std::size_t>(order)]) order = o;
  }
  const auto ord = static_cast<uint32_t>(order);
  plan.type = SubframeType::Fixed;
  plan.order = order;
  plan.u.resize(n - ord);
  for (uint32_t i = ord; i < n; ++i) plan.u[i - ord] = fold(fixedResidual(x, i, order));
  plan.residual = planResidual(plan.u, n, ord, maxPartitionOrder);
  plan.bits = kHeaderBits + ord * static_cast<uint64_t>(bps) + plan.residual.bits;

  // LPC: thay FIXED nếu ít bit hơn.
  std::array<double, kLpcMaxOrder> c{};
  const int lpcOrder = allowLpc ? chooseLpc(x, n, bps, c) : 0;
  std::vector<int32_t> q;
  int shift = 0;
  if (lpcOrder > 0 && quantizeLpc(c, lpcOrder, kLpcPrecision, q, shift)) {
    const auto lord = static_cast<uint32_t>(lpcOrder);
    std::vector<uint32_t> u(n - lord);
    for (uint32_t i = lord; i < n; ++i) {
      int64_t sum = 0;
      for (uint32_t j = 0; j < lord; ++j) sum += static_cast<int64_t>(q[j]) * x[i - j - 1];
      u[i - lord] = fold(x[i] - static_cast<int32_t>(sum >> shift));
    }
    ResidualPlan res = planResidual(u, n, lord, maxPartitionOrder);
    const uint64_t bits = kHeaderBits + lord * static_cast<uint64_t>(bps) + 4 + 5 +
                          lord * static_cast<uint64_t>(kLpcPrecision) + res.bits;
    if (bits < plan.bits) {
      plan.type = SubframeType::Lpc;
      plan.order = lpcOrder;
      plan.precision = kLpcPrecision;
      plan.shift = shift;
      plan.coefs = std::move(q);
      plan.residual = std::move(res);
      plan.u = std::move(u);
      plan.bits = bits;
    }
  }

  const uint64_t verbatimBits = kHeaderBits + static_cast<uint64_t>(n) * static_cast<uint64_t>(bps);
  if (verbatimBits <= plan.bits) {
    plan.type = SubframeType::Verbatim;
    plan.bits = verbatimBits;
  }
}

void writeResidual(BitWriter& w, uint32_t n, const SubframePlan& plan) {
  const ResidualPlan& res = plan.residual;
  w.put(res.rice2 ? 1u : 0u, 2);
  w.put(static_cast<uint32_t>(res.partitionOrder), 4);
  const uint32_t partLen = n >> res.partitionOrder;
  const auto ord = static_cast<uint32_t>(plan.order);
  std::size_t r = 0;
  for (std::size_t j = 0; j < res.params.size(); ++j) {
    const int k = res.params[j];
    w.put(static_cast<uint32_t>(k), res.rice2 ? 5 : 4);
    const uint32_t count = j == 0 ? partLen - ord : partLen;
    for (uint32_t i = 0; i < count; ++i, ++r) {
      const uint32_t u = plan.u[r];
      w.putUnary(u >> k);
      w.put(u, k);
    }
  }
}

void writeSubframe(BitWriter& w, const int32_t* x, uint32_t n, int bps, const SubframePlan& plan) {
  w.put(0, 1);  // bit đệm
  switch (plan.type) {
    case SubframeType::Constant:
      w.put(0b000000, 6);
      w.put(0, 1);  // không có "wasted bits"
      w.putSigned(x[0], bps);
      return;
    case SubframeType::Verbatim:
      w.put(0b000001, 6);
      w.put(0, 1);
      for (uint32_t i = 0; i < n; ++i) w.putSigned(x[i], bps);
      return;
    case SubframeType::Fixed:
      w.put(0b001000u | static_cast<uint32_t>(plan.order), 6);
      w.put(0, 1);
      for (int i = 0; i < plan.order; ++i) w.putSigned(x[i], bps);
      writeResidual(w, n, plan);
      return;
    case SubframeType::Lpc:
      w.put(0b100000u | static_cast<uint32_t>(plan.order - 1), 6);
      w.put(0, 1);
      for (int i = 0; i < plan.order; ++i) w.putSigned(x[i], bps);
      w.put(static_cast<uint32_t>(plan.precision - 1), 4);
      w.putSigned(plan.shift, 5);
      for (const int32_t q : plan.coefs) w.putSigned(q, plan.precision);
      writeResidual(w, n, plan);
      return;
  }
}

uint32_t blockSizeCode(uint32_t n, int& extraBits) {
  extraBits = 0;
  switch (n) {
    case 192:
      return 1;
    case 576:
      return 2;
    case 1152:
      return 3;
    case 2304:
      return 4;
    case 4608:
      return 5;
    case 256:
      return 8;
    case 512:
      return 9;
    case 1024:
      return 10;
    case 2048:
      return 11;
    case 4096:
      return 12;
    case 8192:
      return 13;
    case 16384:
      return 14;
    case 32768:
      return 15;
    default:
      break;
  }
  extraBits = n <= 256 ? 8 : 16;  // ghi (n − 1) sau số thứ tự khung
  return n <= 256 ? 6u : 7u;
}

// Mã tần số trong đầu khung. Tần số không có mã riêng thì ghi thẳng sau số thứ tự khung (kHz 8 bit,
// Hz 16 bit, chục Hz 16 bit) chứ không dùng mã 0 "lấy từ STREAMINFO": bộ giải mã FLAC của Apple
// (ExtAudioFile) không đọc được khung mã 0 (đo trên macOS 26).
uint32_t sampleRateCode(uint32_t rate, uint32_t& extra, int& extraBits) {
  extra = 0;
  extraBits = 0;
  switch (rate) {
    case 88200:
      return 1;
    case 176400:
      return 2;
    case 192000:
      return 3;
    case 8000:
      return 4;
    case 16000:
      return 5;
    case 22050:
      return 6;
    case 24000:
      return 7;
    case 32000:
      return 8;
    case 44100:
      return 9;
    case 48000:
      return 10;
    case 96000:
      return 11;
    default:
      break;
  }
  if (rate % 1000 == 0 && rate / 1000 <= 255) {
    extra = rate / 1000;
    extraBits = 8;
    return 12;
  }
  if (rate <= 65535) {
    extra = rate;
    extraBits = 16;
    return 13;
  }
  if (rate % 10 == 0 && rate / 10 <= 65535) {
    extra = rate / 10;
    extraBits = 16;
    return 14;
  }
  return 0;  // lấy từ STREAMINFO
}

struct FrameScratch {
  std::array<std::vector<int32_t>, 8> ch;
  std::vector<int32_t> mid;
  std::vector<int32_t> side;
  std::array<SubframePlan, 4> plans;  // stereo: trái, phải, giữa, hiệu
};

void writeFrame(std::vector<uint8_t>& out, uint64_t index, const int16_t* in, uint32_t n,
                uint32_t channels, uint32_t rate, const FlacEncodeOptions& opt, FrameScratch& s) {
  for (uint32_t c = 0; c < channels; ++c) {
    auto& v = s.ch[c];
    v.resize(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = in[static_cast<std::size_t>(i) * channels + c];
  }
  // Ghép kênh (RFC 9639 mục 4.2): 0…7 = độc lập; 8 = trái / hiệu; 9 = hiệu / phải; 10 = giữa /
  // hiệu.
  uint32_t assignment = channels - 1;
  const int32_t* firstData = nullptr;
  const int32_t* secondData = nullptr;
  int firstBps = 16;
  int secondBps = 16;
  if (channels == 2) {
    const auto& l = s.ch[0];
    const auto& r = s.ch[1];
    s.mid.resize(n);
    s.side.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
      s.mid[i] = (l[i] + r[i]) >> 1;  // dịch phải số âm: làm tròn xuống, như bộ giải mã cần
      s.side[i] = l[i] - r[i];
    }
    // So bốn cách bằng bộ dự đoán cố định (nhanh), rồi mới tính LPC cho hai kênh được chọn.
    planSubframe(l.data(), n, 16, opt.maxPartitionOrder, false, s.plans[0]);
    planSubframe(r.data(), n, 16, opt.maxPartitionOrder, false, s.plans[1]);
    planSubframe(s.mid.data(), n, 16, opt.maxPartitionOrder, false, s.plans[2]);
    planSubframe(s.side.data(), n, 17, opt.maxPartitionOrder, false, s.plans[3]);
    const uint64_t L = s.plans[0].bits, R = s.plans[1].bits, M = s.plans[2].bits,
                   S = s.plans[3].bits;
    const uint64_t best = std::min({L + R, L + S, S + R, M + S});
    if (best == L + R) {
      assignment = 1;
      firstData = l.data();
      secondData = r.data();
    } else if (best == L + S) {
      assignment = 8;
      firstData = l.data();
      secondData = s.side.data();
      secondBps = 17;
    } else if (best == S + R) {
      assignment = 9;
      firstData = s.side.data();
      firstBps = 17;
      secondData = r.data();
    } else {
      assignment = 10;
      firstData = s.mid.data();
      secondData = s.side.data();
      secondBps = 17;
    }
  }

  const std::size_t start = out.size();
  BitWriter w(out);
  w.put(0x3FFE, 14);  // mã đồng bộ
  w.put(0, 1);
  w.put(0, 1);  // cỡ khối cố định
  int extraBits = 0;
  w.put(blockSizeCode(n, extraBits), 4);
  uint32_t rateExtra = 0;
  int rateExtraBits = 0;
  w.put(sampleRateCode(rate, rateExtra, rateExtraBits), 4);
  w.put(assignment, 4);
  w.put(0b100, 3);  // 16 bit
  w.put(0, 1);
  putCodedNumber(w, index);
  if (extraBits > 0) w.put(n - 1, extraBits);
  if (rateExtraBits > 0) w.put(rateExtra, rateExtraBits);
  w.put(crc8(out.data() + start, out.size() - start), 8);

  if (channels == 2) {
    SubframePlan& a = s.plans[0];
    SubframePlan& b = s.plans[1];
    planSubframe(firstData, n, firstBps, opt.maxPartitionOrder, true, a);
    planSubframe(secondData, n, secondBps, opt.maxPartitionOrder, true, b);
    writeSubframe(w, firstData, n, firstBps, a);
    writeSubframe(w, secondData, n, secondBps, b);
  } else {
    SubframePlan& plan = s.plans[0];
    for (uint32_t c = 0; c < channels; ++c) {
      planSubframe(s.ch[c].data(), n, 16, opt.maxPartitionOrder, true, plan);
      writeSubframe(w, s.ch[c].data(), n, 16, plan);
    }
  }
  w.alignZero();
  w.put(crc16(out.data() + start, out.size() - start), 16);
}

void putAt(std::vector<uint8_t>& out, std::size_t pos, uint32_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i, ++pos) out[pos] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

std::vector<uint8_t> encodeFlac(const int16_t* interleaved, uint64_t frames, uint32_t channels,
                                uint32_t sampleRate, const FlacEncodeOptions& options) {
  if (channels == 0 || channels > 8 || sampleRate == 0 || sampleRate > 655350 ||
      options.blockSize < 16 || options.blockSize > 65535 || options.maxPartitionOrder < 0 ||
      options.maxPartitionOrder > 15 || (frames > 0 && interleaved == nullptr) ||
      frames >= (1ull << 36)) {
    throw std::invalid_argument("encodeFlac: tham số sai");
  }
  std::vector<uint8_t> out = {'f', 'L', 'a', 'C'};
  out.reserve(static_cast<std::size_t>(frames * channels * 2 * 7 / 10) + 64);
  {
    // STREAMINFO (RFC 9639 mục 8.2), khối metadata duy nhất (bit "cuối" = 1), dài 34 byte.
    BitWriter w(out);
    w.put(1, 1);
    w.put(0, 7);
    w.put(34, 24);
    w.put(options.blockSize, 16);  // cỡ khối nhỏ nhất (không tính khối cuối)
    w.put(options.blockSize, 16);  // lớn nhất
    w.put(0, 24);                  // cỡ khung nhỏ nhất — điền sau
    w.put(0, 24);                  // lớn nhất — điền sau
    w.put(sampleRate, 20);
    w.put(channels - 1, 3);
    w.put(15, 5);  // 16 bit
    w.put(static_cast<uint32_t>(frames >> 32), 4);
    w.put(static_cast<uint32_t>(frames & 0xFFFFFFFFu), 32);
    for (int i = 0; i < 4; ++i) w.put(0, 32);  // MD5 = 0: không rõ
  }
  constexpr std::size_t kMinFramePos = 12;
  constexpr std::size_t kMaxFramePos = 15;
  FrameScratch scratch;
  uint32_t minFrame = 0xFFFFFF;
  uint32_t maxFrame = 0;
  uint64_t index = 0;
  for (uint64_t pos = 0; pos < frames; pos += options.blockSize, ++index) {
    const auto n = static_cast<uint32_t>(std::min<uint64_t>(options.blockSize, frames - pos));
    const std::size_t before = out.size();
    writeFrame(out, index, interleaved + pos * channels, n, channels, sampleRate, options, scratch);
    const auto size = static_cast<uint32_t>(out.size() - before);
    minFrame = std::min(minFrame, size);
    maxFrame = std::max(maxFrame, size);
  }
  if (frames > 0) {
    putAt(out, kMinFramePos, minFrame, 3);
    putAt(out, kMaxFramePos, maxFrame, 3);
  }
  return out;
}

tc_result writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
  const std::string tmp = path + ".part";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) return TC_ERR_INVALID_ARG;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  const bool closed = std::fclose(f) == 0;
  if (!ok || !closed || std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

}  // namespace tonecore
