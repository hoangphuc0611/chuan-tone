// Preset FX giọng (từ v0.6) dạng Dart thuần: đọc/ghi từng thông số, lưu JSON. Chuyển sang struct C
// `tc_fx_preset` nằm ở tonecore.dart (xem docs/adr/005-fx-chain.md).

/// Thể loại (thứ tự khớp `tc_genre`). [dry] = "Khô": không hiệu ứng.
enum Genre { dry, bolero, ballad, pop, remix, rap, lofi }

/// Loại băng EQ; [code] là giá trị `tc_eq_band_type` (không liên tiếp).
enum EqBandType {
  off(0),
  bell(3),
  lowShelf(4),
  highShelf(5);

  const EqBandType(this.code);

  final int code;

  static EqBandType fromCode(int code) =>
      values.firstWhere((t) => t.code == code, orElse: () => off);
}

/// Thời gian delay (thứ tự khớp `tc_delay_division`): theo nhịp bài, hoặc [ms] = theo
/// [FxPreset.delayMs]. [off] = tắt delay.
enum DelayDivision { off, quarter, eighth, dottedEighth, sixteenth, ms }

/// Kiểu phòng (thứ tự khớp `tc_reverb_type`).
enum ReverbType { room, hall, plate }

T _byName<T extends Enum>(List<T> values, Object? name, T fallback) {
  for (final v in values) {
    if (v.name == name) return v;
  }
  return fallback;
}

double _num(Map<String, dynamic> j, String key, double fallback) {
  final v = j[key];
  return v is num && v.isFinite ? v.toDouble() : fallback;
}

bool _bool(Map<String, dynamic> j, String key, bool fallback) {
  final v = j[key];
  return v is bool ? v : fallback;
}

/// Một băng EQ (bell / shelf).
class EqBand {
  const EqBand({
    this.type = EqBandType.off,
    this.freqHz = 1000,
    this.gainDb = 0,
    this.q = 0.7071,
  });

  final EqBandType type;
  final double freqHz;

  /// −18…18
  final double gainDb;

  /// 0,1…10
  final double q;

  EqBand copyWith({
    EqBandType? type,
    double? freqHz,
    double? gainDb,
    double? q,
  }) => EqBand(
    type: type ?? this.type,
    freqHz: freqHz ?? this.freqHz,
    gainDb: gainDb ?? this.gainDb,
    q: q ?? this.q,
  );

  Map<String, dynamic> toJson() => {
    'type': type.name,
    'freqHz': freqHz,
    'gainDb': gainDb,
    'q': q,
  };

  /// Khoá thiếu hoặc sai kiểu → giá trị mặc định.
  factory EqBand.fromJson(Map<String, dynamic> j) {
    const d = EqBand();
    return EqBand(
      type: _byName(EqBandType.values, j['type'], d.type),
      freqHz: _num(j, 'freqHz', d.freqHz),
      gainDb: _num(j, 'gainDb', d.gainDb),
      q: _num(j, 'q', d.q),
    );
  }

  @override
  bool operator ==(Object other) =>
      other is EqBand &&
      other.type == type &&
      other.freqHz == freqHz &&
      other.gainDb == gainDb &&
      other.q == q;

  @override
  int get hashCode => Object.hash(type, freqHz, gainDb, q);
}

/// Một bộ thông số FX giọng — các trường khớp `tc_fx_preset`. Mặc định = preset "Khô" (mọi khối
/// tắt, thông số còn lại bằng giá trị mặc định của engine). Preset theo thể loại: lấy từ engine bằng
/// [ToneCore.genrePreset]. Engine kẹp giá trị ngoài khoảng khi áp dụng.
class FxPreset {
  const FxPreset({
    this.eqHighPassHz = 0,
    this.eqLowPassHz = 0,
    this.eqBands = const [EqBand(), EqBand(), EqBand(), EqBand(), EqBand()],
    this.compEnabled = false,
    this.compThresholdDb = -18,
    this.compRatio = 3,
    this.compKneeDb = 6,
    this.compAttackMs = 5,
    this.compReleaseMs = 80,
    this.compMakeupDb = 0,
    this.deessEnabled = false,
    this.deessThresholdDb = -30,
    this.deessMaxReductionDb = 6,
    this.delayDivision = DelayDivision.off,
    this.delayMs = 300,
    this.delayFeedback = 0.25,
    this.delayMix = 0.2,
    this.delayLowPassHz = 4000,
    this.delayHighPassHz = 200,
    this.reverbEnabled = false,
    this.reverbType = ReverbType.hall,
    this.reverbDecayS = 2.2,
    this.reverbPreDelayMs = 30,
    this.reverbDamping = 0.5,
    this.reverbMix = 0.22,
  });

  static const bandCount = 5;

  /// Thông cao / thông thấp bậc 2 của EQ (0 = tắt).
  final double eqHighPassHz;
  final double eqLowPassHz;

  /// [bandCount] băng; thiếu thì các băng sau coi như tắt, thừa thì bỏ.
  final List<EqBand> eqBands;

  final bool compEnabled;

  /// dBFS (sin toàn thang = 0).
  final double compThresholdDb;

  /// ≥ 1
  final double compRatio;
  final double compKneeDb;
  final double compAttackMs;
  final double compReleaseMs;
  final double compMakeupDb;

  /// De-esser: băng dò 5–9 kHz, shelf cao 5 kHz.
  final bool deessEnabled;
  final double deessThresholdDb;

  /// 0…12
  final double deessMaxReductionDb;

  final DelayDivision delayDivision;

  /// Khi [delayDivision] = [DelayDivision.ms]: 1…2000.
  final double delayMs;

  /// 0…0,5
  final double delayFeedback;

  /// 0…1: độ lớn tiếng vọng so với tiếng khô.
  final double delayMix;
  final double delayLowPassHz;
  final double delayHighPassHz;

  final bool reverbEnabled;
  final ReverbType reverbType;

  /// RT60 0,2…8 s.
  final double reverbDecayS;

  /// 0…100
  final double reverbPreDelayMs;

  /// 0…0,9: RT60 ở 4 kHz = decay · (1 − damping).
  final double reverbDamping;

  /// 0…1: độ lớn tiếng vang so với tiếng khô (trong băng giọng).
  final double reverbMix;

  bool get delayEnabled => delayDivision != DelayDivision.off;

  FxPreset copyWith({
    double? eqHighPassHz,
    double? eqLowPassHz,
    List<EqBand>? eqBands,
    bool? compEnabled,
    double? compThresholdDb,
    double? compRatio,
    double? compKneeDb,
    double? compAttackMs,
    double? compReleaseMs,
    double? compMakeupDb,
    bool? deessEnabled,
    double? deessThresholdDb,
    double? deessMaxReductionDb,
    DelayDivision? delayDivision,
    double? delayMs,
    double? delayFeedback,
    double? delayMix,
    double? delayLowPassHz,
    double? delayHighPassHz,
    bool? reverbEnabled,
    ReverbType? reverbType,
    double? reverbDecayS,
    double? reverbPreDelayMs,
    double? reverbDamping,
    double? reverbMix,
  }) => FxPreset(
    eqHighPassHz: eqHighPassHz ?? this.eqHighPassHz,
    eqLowPassHz: eqLowPassHz ?? this.eqLowPassHz,
    eqBands: eqBands ?? this.eqBands,
    compEnabled: compEnabled ?? this.compEnabled,
    compThresholdDb: compThresholdDb ?? this.compThresholdDb,
    compRatio: compRatio ?? this.compRatio,
    compKneeDb: compKneeDb ?? this.compKneeDb,
    compAttackMs: compAttackMs ?? this.compAttackMs,
    compReleaseMs: compReleaseMs ?? this.compReleaseMs,
    compMakeupDb: compMakeupDb ?? this.compMakeupDb,
    deessEnabled: deessEnabled ?? this.deessEnabled,
    deessThresholdDb: deessThresholdDb ?? this.deessThresholdDb,
    deessMaxReductionDb: deessMaxReductionDb ?? this.deessMaxReductionDb,
    delayDivision: delayDivision ?? this.delayDivision,
    delayMs: delayMs ?? this.delayMs,
    delayFeedback: delayFeedback ?? this.delayFeedback,
    delayMix: delayMix ?? this.delayMix,
    delayLowPassHz: delayLowPassHz ?? this.delayLowPassHz,
    delayHighPassHz: delayHighPassHz ?? this.delayHighPassHz,
    reverbEnabled: reverbEnabled ?? this.reverbEnabled,
    reverbType: reverbType ?? this.reverbType,
    reverbDecayS: reverbDecayS ?? this.reverbDecayS,
    reverbPreDelayMs: reverbPreDelayMs ?? this.reverbPreDelayMs,
    reverbDamping: reverbDamping ?? this.reverbDamping,
    reverbMix: reverbMix ?? this.reverbMix,
  );

  /// Lưu preset người dùng (enum ghi theo tên để bền khi đổi mã C).
  Map<String, dynamic> toJson() => {
    'eqHighPassHz': eqHighPassHz,
    'eqLowPassHz': eqLowPassHz,
    'eqBands': [for (final b in eqBands) b.toJson()],
    'compEnabled': compEnabled,
    'compThresholdDb': compThresholdDb,
    'compRatio': compRatio,
    'compKneeDb': compKneeDb,
    'compAttackMs': compAttackMs,
    'compReleaseMs': compReleaseMs,
    'compMakeupDb': compMakeupDb,
    'deessEnabled': deessEnabled,
    'deessThresholdDb': deessThresholdDb,
    'deessMaxReductionDb': deessMaxReductionDb,
    'delayDivision': delayDivision.name,
    'delayMs': delayMs,
    'delayFeedback': delayFeedback,
    'delayMix': delayMix,
    'delayLowPassHz': delayLowPassHz,
    'delayHighPassHz': delayHighPassHz,
    'reverbEnabled': reverbEnabled,
    'reverbType': reverbType.name,
    'reverbDecayS': reverbDecayS,
    'reverbPreDelayMs': reverbPreDelayMs,
    'reverbDamping': reverbDamping,
    'reverbMix': reverbMix,
  };

  /// Khoá thiếu hoặc sai kiểu → lấy từ [base] (mặc định "Khô"): đọc được file cũ / thiếu trường.
  factory FxPreset.fromJson(
    Map<String, dynamic> j, {
    FxPreset base = const FxPreset(),
  }) {
    final rawBands = j['eqBands'];
    final bands = [...base.eqBands];
    if (rawBands is List) {
      for (var i = 0; i < bandCount && i < rawBands.length; i++) {
        final b = rawBands[i];
        if (b is Map<String, dynamic>) bands[i] = EqBand.fromJson(b);
      }
    }
    return FxPreset(
      eqHighPassHz: _num(j, 'eqHighPassHz', base.eqHighPassHz),
      eqLowPassHz: _num(j, 'eqLowPassHz', base.eqLowPassHz),
      eqBands: List.unmodifiable(bands),
      compEnabled: _bool(j, 'compEnabled', base.compEnabled),
      compThresholdDb: _num(j, 'compThresholdDb', base.compThresholdDb),
      compRatio: _num(j, 'compRatio', base.compRatio),
      compKneeDb: _num(j, 'compKneeDb', base.compKneeDb),
      compAttackMs: _num(j, 'compAttackMs', base.compAttackMs),
      compReleaseMs: _num(j, 'compReleaseMs', base.compReleaseMs),
      compMakeupDb: _num(j, 'compMakeupDb', base.compMakeupDb),
      deessEnabled: _bool(j, 'deessEnabled', base.deessEnabled),
      deessThresholdDb: _num(j, 'deessThresholdDb', base.deessThresholdDb),
      deessMaxReductionDb: _num(
        j,
        'deessMaxReductionDb',
        base.deessMaxReductionDb,
      ),
      delayDivision: _byName(
        DelayDivision.values,
        j['delayDivision'],
        base.delayDivision,
      ),
      delayMs: _num(j, 'delayMs', base.delayMs),
      delayFeedback: _num(j, 'delayFeedback', base.delayFeedback),
      delayMix: _num(j, 'delayMix', base.delayMix),
      delayLowPassHz: _num(j, 'delayLowPassHz', base.delayLowPassHz),
      delayHighPassHz: _num(j, 'delayHighPassHz', base.delayHighPassHz),
      reverbEnabled: _bool(j, 'reverbEnabled', base.reverbEnabled),
      reverbType: _byName(ReverbType.values, j['reverbType'], base.reverbType),
      reverbDecayS: _num(j, 'reverbDecayS', base.reverbDecayS),
      reverbPreDelayMs: _num(j, 'reverbPreDelayMs', base.reverbPreDelayMs),
      reverbDamping: _num(j, 'reverbDamping', base.reverbDamping),
      reverbMix: _num(j, 'reverbMix', base.reverbMix),
    );
  }

  List<Object> get _fields => [
    eqHighPassHz,
    eqLowPassHz,
    ...eqBands,
    compEnabled,
    compThresholdDb,
    compRatio,
    compKneeDb,
    compAttackMs,
    compReleaseMs,
    compMakeupDb,
    deessEnabled,
    deessThresholdDb,
    deessMaxReductionDb,
    delayDivision,
    delayMs,
    delayFeedback,
    delayMix,
    delayLowPassHz,
    delayHighPassHz,
    reverbEnabled,
    reverbType,
    reverbDecayS,
    reverbPreDelayMs,
    reverbDamping,
    reverbMix,
  ];

  @override
  bool operator ==(Object other) {
    if (other is! FxPreset) return false;
    final a = _fields;
    final b = other._fields;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode => Object.hashAll(_fields);
}
