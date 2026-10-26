// Mốc so sánh A/B: Signalsmith Stretch (MIT,
// https://github.com/Signalsmith-Audio/signalsmith-stretch) dùng như hộp đen qua API công khai
// trong README — CHỈ trong beatlab, không link vào libtonecore, không ship. Tải bằng
// tools/stretch/fetch_signalsmith.sh; CMake tự bật khi thấy thư mục.

#include "signalsmith_ref.h"

#include <algorithm>

#include "signalsmith-stretch/signalsmith-stretch.h"

std::vector<float> signalsmithShift(const float* in, uint64_t frames, int channels, double rate,
                                    double semitones) {
  signalsmith::stretch::SignalsmithStretch<float> st;
  st.presetDefault(channels, static_cast<float>(rate));
  st.setTransposeSemitones(static_cast<float>(semitones));
  const auto inLat = static_cast<uint64_t>(st.inputLatency());
  const auto outLat = static_cast<uint64_t>(st.outputLatency());
  const auto ch = static_cast<std::size_t>(channels);

  // Planar, thêm inLat mẫu im lặng ở cuối để "thời điểm xử lý" chạy tới hết bài (README: Ending).
  std::vector<std::vector<float>> planarIn(ch, std::vector<float>(frames + inLat, 0.0f));
  for (uint64_t i = 0; i < frames; ++i) {
    for (std::size_t c = 0; c < ch; ++c) planarIn[c][i] = in[i * ch + c];
  }
  std::vector<std::vector<float>> planarOut(ch, std::vector<float>(frames + outLat, 0.0f));
  std::vector<float*> inPtr(ch);
  std::vector<float*> outPtr(ch);

  // seek() với inLat mẫu đầu → thời điểm xử lý trùng đầu bài (README: Seeking and starting).
  const uint64_t seekLen = std::min(inLat, frames);
  for (std::size_t c = 0; c < ch; ++c) inPtr[c] = planarIn[c].data();
  st.seek(inPtr.data(), static_cast<int>(seekLen), 1.0);
  for (std::size_t c = 0; c < ch; ++c) {
    inPtr[c] = planarIn[c].data() + seekLen;
    outPtr[c] = planarOut[c].data();
  }
  const uint64_t rest = frames + inLat - seekLen;
  st.process(inPtr.data(), static_cast<int>(rest), outPtr.data(), static_cast<int>(rest));
  for (std::size_t c = 0; c < ch; ++c) outPtr[c] = planarOut[c].data() + rest;
  st.flush(outPtr.data(), static_cast<int>(frames + outLat - rest));

  // Bỏ outLat mẫu "pre-roll" → khớp thời gian với bản gốc.
  std::vector<float> out(static_cast<std::size_t>(frames) * ch);
  for (uint64_t i = 0; i < frames; ++i) {
    for (std::size_t c = 0; c < ch; ++c) out[i * ch + c] = planarOut[c][i + outLat];
  }
  return out;
}
