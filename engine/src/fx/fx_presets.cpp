#include "fx/fx_presets.h"

namespace tonecore {

namespace {

EqBand bell(float hz, float db, float q = 1.0f) {
  return {SvfType::Bell, hz, db, q};
}

CompressorParams comp(float thresholdDb, float ratio, float makeupDb) {
  CompressorParams c;
  c.thresholdDb = thresholdDb;
  c.ratio = ratio;
  c.kneeDb = 6.0f;
  c.attackMs = 5.0f;
  c.releaseMs = 80.0f;
  c.makeupDb = makeupDb;
  return c;
}

DelayParams delay(NoteDivision div, float feedback, float mix, float lowPassHz = 4000.0f) {
  DelayParams d;
  d.division = div;
  d.feedback = feedback;
  d.mix = mix;
  d.lowPassHz = lowPassHz;
  d.highPassHz = 200.0f;
  return d;
}

ReverbParams reverb(ReverbType type, float decay, float preDelayMs, float damping, float mix) {
  ReverbParams r;
  r.type = type;
  r.decaySec = decay;
  r.preDelayMs = preDelayMs;
  r.damping = damping;
  r.mix = mix;
  return r;
}

}  // namespace

FxPreset genrePreset(Genre g) noexcept {
  FxPreset p;  // Khô
  if (g == Genre::Dry) return p;
  p.deessOn = true;  // mọi thể loại: de-esser mặc định (5 kHz shelf, −6 dB tối đa)
  p.compOn = true;
  p.reverbOn = true;
  switch (g) {
    case Genre::Bolero:  // Trữ tình: Hall 2,2 s, vọng 1/4 nốt, ấm (bớt 300 Hz đục), rõ chữ 3 kHz
      p.eq.bands[0] = bell(300.0f, -2.0f);
      p.eq.bands[1] = bell(3000.0f, 2.0f);
      p.comp = comp(-18.0f, 3.0f, 2.0f);
      p.delay = delay(NoteDivision::Quarter, 0.25f, 0.25f);
      p.reverb = reverb(ReverbType::Hall, 2.2f, 30.0f, 0.5f, 0.22f);
      break;
    case Genre::Ballad:
      p.eq.bands[0] = bell(4000.0f, 1.5f);
      p.comp = comp(-18.0f, 3.0f, 2.0f);
      p.delay = delay(NoteDivision::Eighth, 0.2f, 0.15f);
      p.reverb = reverb(ReverbType::Plate, 1.8f, 20.0f, 0.3f, 0.20f);
      break;
    case Genre::Pop:  // Nhạc trẻ: sáng, "air" 12 kHz
      p.eq.bands[0] = bell(5000.0f, 3.0f);
      p.eq.bands[1] = {SvfType::HighShelf, 12000.0f, 2.0f, 0.7071f};
      p.comp = comp(-20.0f, 4.0f, 3.0f);
      p.delay = delay(NoteDivision::Eighth, 0.2f, 0.18f);
      p.reverb = reverb(ReverbType::Plate, 1.4f, 15.0f, 0.3f, 0.15f);
      break;
    case Genre::Remix:
      p.eq.bands[0] = bell(5000.0f, 3.0f);
      p.comp = comp(-22.0f, 5.0f, 3.5f);
      p.delay = delay(NoteDivision::DottedEighth, 0.3f, 0.22f);
      p.reverb = reverb(ReverbType::Room, 1.0f, 10.0f, 0.5f, 0.12f);
      break;
    case Genre::Rap:  // khô, rõ: nén mạnh, phòng nhỏ, không vọng
      p.eq.bands[0] = bell(2500.0f, 2.0f);
      p.comp = comp(-24.0f, 6.0f, 4.0f);
      p.reverb = reverb(ReverbType::Room, 0.6f, 5.0f, 0.5f, 0.08f);
      break;
    case Genre::Lofi:  // mờ: cắt cao 9 kHz, cắt trầm 150 Hz, vọng tối
      p.eq.highPassHz = 150.0f;
      p.eq.lowPassHz = 9000.0f;
      p.comp = comp(-18.0f, 3.0f, 2.0f);
      p.delay = delay(NoteDivision::Quarter, 0.3f, 0.20f, 2500.0f);
      p.reverb = reverb(ReverbType::Room, 1.2f, 20.0f, 0.6f, 0.18f);
      break;
    case Genre::Dry:
      break;
  }
  return p;
}

}  // namespace tonecore
