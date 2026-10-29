/*
 * ToneCore – C API công khai.
 *
 * Quy tắc ổn định ABI:
 *  - Chỉ dùng kiểu C thuần; struct cấu hình có trường `struct_size` để mở rộng về sau.
 *  - Không exception nào vượt qua ranh giới C; lỗi trả về qua tc_result.
 *  - Không hàm nào gọi ngược (callback) từ audio thread. UI kéo dữ liệu qua
 *    tc_get_metrics / tc_poll_events (xem docs/adr/001-c-api-ffi.md).
 *  - Mọi hàm nhận tc_engine* đều an toàn khi gọi từ một luồng điều khiển (UI);
 *    không gọi đồng thời cùng một engine từ nhiều luồng điều khiển.
 */
#ifndef TONECORE_H
#define TONECORE_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(TONECORE_BUILDING)
#define TC_EXPORT __declspec(dllexport)
#else
#define TC_EXPORT
#endif
#else
#define TC_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Phiên bản API: (major << 16) | (minor << 8) | patch.
 * Tăng major khi phá vỡ ABI; tăng minor khi thêm hàm. */
#define TC_API_VERSION_MAJOR 0
#define TC_API_VERSION_MINOR 6
#define TC_API_VERSION_PATCH 0
#define TC_API_VERSION \
  ((TC_API_VERSION_MAJOR << 16) | (TC_API_VERSION_MINOR << 8) | TC_API_VERSION_PATCH)

/* Mã kết quả. Dùng int32_t (không dùng kiểu enum) trong chữ ký hàm vì kích thước enum
 * phụ thuộc trình biên dịch — giữ ABI cố định cho FFI. Các enum bên dưới chỉ để đặt tên giá trị;
 * trường struct và tham số tương ứng luôn là int32_t. */
typedef int32_t tc_result;
enum tc_result_code {
  TC_OK = 0,
  TC_ERR_INVALID_ARG = -1,
  TC_ERR_OUT_OF_MEMORY = -2,
  TC_ERR_INVALID_STATE = -3,
  TC_ERR_NOT_SUPPORTED = -4,
  TC_ERR_INTERNAL = -5,
  /* Không mở được thiết bị âm thanh (thiếu quyền micro, thiết bị bận...). */
  TC_ERR_DEVICE = -6,
  /* Việc bị huỷ giữa chừng (từ v0.4, vd. tc_analyzer_cancel). */
  TC_ERR_CANCELLED = -7
};

typedef enum tc_backend {
  /* Không có thiết bị âm thanh; xử lý bằng tc_engine_process_offline (test, render). */
  TC_BACKEND_NULL = 0,
  /* Oboe trên Android, RemoteIO trên iOS. */
  TC_BACKEND_PLATFORM = 1
} tc_backend;

typedef enum tc_engine_state { TC_STATE_STOPPED = 0, TC_STATE_RUNNING = 1 } tc_engine_state;

/* API âm thanh thực tế của hệ điều hành đang dùng. */
typedef enum tc_audio_api {
  TC_AUDIO_API_NONE = 0,
  TC_AUDIO_API_AAUDIO = 1,
  TC_AUDIO_API_OPENSLES = 2,
  TC_AUDIO_API_REMOTEIO = 3
} tc_audio_api;

/* Đường ra/vào âm thanh hiện tại. */
typedef enum tc_route {
  TC_ROUTE_UNKNOWN = 0,
  TC_ROUTE_SPEAKER = 1,   /* loa ngoài của máy */
  TC_ROUTE_WIRED = 2,     /* tai nghe có dây (3.5 mm hoặc USB-C analog) */
  TC_ROUTE_BLUETOOTH = 3, /* tai nghe Bluetooth – không nghe lại giọng được */
  TC_ROUTE_USB = 4,       /* soundcard / micro USB (OTG) */
  TC_ROUTE_RECEIVER = 5   /* loa thoại */
} tc_route;

/* Định danh node / tham số dùng cho tc_set_param. Danh sách sẽ mở rộng theo milestone. */
typedef enum tc_node_id {
  TC_NODE_MASTER = 0,  /* tổng đầu ra */
  TC_NODE_MONITOR = 1, /* nghe lại giọng (micro → tai nghe) */
  TC_NODE_BEAT = 2,    /* nhạc beat */
  TC_NODE_PITCH = 3,   /* tự chỉnh giọng (từ v0.3) */
  TC_NODE_GATE = 4,    /* cổng chặn tiếng ồn trên micro, trước dò cao độ (từ v0.6) */
  TC_NODE_FX = 5       /* chuỗi FX giọng EQ → nén → de-esser → delay → reverb (từ v0.6) */
} tc_node_id;

typedef enum tc_param_id {
  /* Hệ số khuếch đại tuyến tính, 0..4. Được làm mượt trong audio thread. */
  TC_PARAM_GAIN = 0,
  /* Bật/tắt: 0 hoặc 1 (TC_NODE_MONITOR, TC_NODE_PITCH). */
  TC_PARAM_ENABLED = 1,
  /* ---- TC_NODE_PITCH (từ v0.3) ---- */
  TC_PARAM_RETUNE_MS = 2,        /* 0..200 ms: thời gian kéo về nốt đích (0 = tức thì) */
  TC_PARAM_HUMANIZE = 3,         /* 0..1: nốt ngân dài thì chỉnh chậm dần */
  TC_PARAM_FLEX_CENTS = 4,       /* 0..100 cent: lệch dưới ngưỡng này thì không chỉnh */
  TC_PARAM_VIBRATO_PRESERVE = 5, /* 0..1: giữ rung giọng / luyến láy */
  TC_PARAM_HYSTERESIS_CENTS = 6, /* 0..50 cent: vùng trễ khi chuyển nốt đích */
  /* ---- từ v0.6 ----
   * TC_NODE_GATE: TC_PARAM_ENABLED (mặc định tắt), TC_PARAM_THRESHOLD_DB.
   * TC_NODE_FX: TC_PARAM_ENABLED (mặc định bật; tắt = preset "Khô", đuôi vang đang có vẫn ngân
   * nốt), TC_PARAM_MIX ("Vang": nhân phần vọng + vang của preset, 0..2, mặc định 1, trượt 20 ms).
   */
  TC_PARAM_MIX = 7,
  TC_PARAM_THRESHOLD_DB = 8 /* −80..0 dBFS: gate mở khi tiếng to hơn (đóng thì giảm −30 dB) */
} tc_param_id;

/* Thang âm cho tự chỉnh giọng (từ v0.3). */
typedef enum tc_scale {
  TC_SCALE_MAJOR = 0,            /* trưởng */
  TC_SCALE_NATURAL_MINOR = 1,    /* thứ tự nhiên */
  TC_SCALE_HARMONIC_MINOR = 2,   /* thứ hoà âm */
  TC_SCALE_PENTATONIC_MAJOR = 3, /* ngũ cung trưởng */
  TC_SCALE_PENTATONIC_MINOR = 4, /* ngũ cung thứ */
  TC_SCALE_CHROMATIC = 5         /* 12 nốt */
} tc_scale;

/* 3 mức tự chỉnh giọng người dùng thấy (từ v0.3). */
typedef enum tc_correction_preset {
  TC_PRESET_NATURAL = 0, /* Tự nhiên */
  TC_PRESET_SINGER = 1,  /* Ca sĩ */
  TC_PRESET_ROBOT = 2    /* Robot */
} tc_correction_preset;

/* Sự kiện gửi lên UI, lấy bằng tc_poll_events. */
typedef enum tc_event_type {
  TC_EVENT_NONE = 0,
  TC_EVENT_BEAT_LOADED = 1,      /* code = tc_result; value = thời lượng (ms) */
  TC_EVENT_BEAT_ENDED = 2,       /* phát hết bài */
  TC_EVENT_ROUTE_CHANGED = 3,    /* code = tc_route mới */
  TC_EVENT_INTERRUPTED = 4,      /* code = 1 bắt đầu bị ngắt (cuộc gọi...), 0 kết thúc */
  TC_EVENT_LATENCY_DONE = 5,     /* code = tc_result; value = độ trễ (ms) nếu thành công */
  TC_EVENT_STREAM_RESTARTED = 6, /* code = tc_result của lần mở lại stream */
  TC_EVENT_MONITOR_BLOCKED = 7,  /* nghe lại giọng bị chặn vì đang dùng Bluetooth */
  /* (từ v0.4) Key chỉnh giọng đang áp dụng đổi — theo timeline, hoặc (từ v0.5) beat bắt đầu nghe ở
   * tone mới. code = chủ âm 0..11 (đã cộng số nửa cung đổi tone), value = tc_scale. */
  TC_EVENT_KEY_CHANGED = 8,
  /* (từ v0.5) Đã render xong cả bài ở tone mới: code = số nửa cung, value = thời gian render (ms)
   */
  TC_EVENT_TRANSPOSE_READY = 9
} tc_event_type;

typedef struct tc_engine tc_engine;

typedef struct tc_engine_config {
  uint32_t struct_size;      /* = sizeof(tc_engine_config); tc_engine_config_init tự điền */
  uint32_t sample_rate;      /* Hz, mặc định 48000 */
  uint32_t max_block_frames; /* số frame tối đa mỗi lần xử lý, mặc định 1024 */
  uint32_t input_channels;   /* mặc định 1 (micro mono) */
  uint32_t output_channels;  /* mặc định 2 (stereo) */
  int32_t backend;           /* tc_backend, mặc định TC_BACKEND_NULL */
} tc_engine_config;

/* struct_size: người gọi điền sizeof(tc_metrics) của header họ dùng; engine chỉ ghi phần
 * nằm trong struct_size → binding cũ (v0.1) vẫn dùng được. */
typedef struct tc_metrics {
  uint32_t struct_size;
  int32_t state; /* tc_engine_state */
  uint32_t sample_rate;
  uint64_t frames_processed; /* tổng số frame đã xử lý */
  uint64_t callbacks;        /* số lần gọi xử lý */
  uint32_t xruns;            /* số lần thiếu/tràn buffer */
  float cpu_load;            /* 0..1 – tỉ lệ thời gian xử lý / thời lượng block gần nhất */
  /* ---- từ v0.2 ---- */
  uint32_t buffer_frames;            /* kích thước buffer đầu ra hiện tại */
  uint32_t burst_frames;             /* số frame mỗi lần callback của thiết bị */
  double reported_input_latency_ms;  /* độ trễ vào do hệ điều hành báo (ước lượng) */
  double reported_output_latency_ms; /* độ trễ ra do hệ điều hành báo (ước lượng) */
  int32_t route;                     /* tc_route */
  int32_t audio_api;                 /* tc_audio_api */
  int32_t monitor_active;            /* 1 nếu đang nghe lại giọng */
  int32_t input_channels;            /* số kênh micro thực tế */
  /* ---- từ v0.3 ---- */
  float cpu_load_peak;              /* 0..1 – tải cao nhất trong 1 giây gần nhất */
  int32_t algorithm_latency_frames; /* độ trễ thêm do xử lý: 48 (limiter đầu ra 1 ms, từ M5) + 480
                                       khi bật chỉnh giọng, ở 48 kHz */
  /* ---- từ v0.6 ---- */
  float fx_load;     /* 0..1 – phần thời gian của block dành cho chuỗi FX giọng (block gần nhất) */
  int32_t fx_chains; /* số chuỗi FX đang chạy: 1 + số chuỗi cũ còn ngân sau khi đổi preset */
} tc_metrics;

typedef struct tc_playback_state {
  uint32_t struct_size;
  int32_t has_beat;
  int32_t playing;
  double position_ms;
  double duration_ms;
  /* (từ v0.5) Đổi tone beat. Struct của binding v0.4 (kết thúc ở duration_ms) vẫn được nhận. */
  int32_t transpose;         /* số nửa cung đã yêu cầu (tc_set_transpose) */
  int32_t audible_transpose; /* số nửa cung đang nghe (đổi sau ~0,1 s khi bản mới sẵn) */
  float transpose_progress;  /* 0..1: phần bài đã render ở tone yêu cầu (1 khi transpose = 0) */
} tc_playback_state;

typedef struct tc_latency_result {
  uint32_t struct_size;
  int32_t valid;     /* 1 nếu đo thành công */
  double latency_ms; /* round-trip đo được (trung vị) */
  double latency_frames;
  float confidence;     /* 0..1 */
  int32_t valid_bursts; /* số lần đo hợp lệ */
  int32_t total_bursts;
  double spread_ms;                 /* chênh lệch giữa các lần hợp lệ */
  double reported_input_latency_ms; /* HĐH báo lúc đo, để so sánh */
  double reported_output_latency_ms;
  uint32_t buffer_frames;
  uint32_t burst_frames;
  int32_t route;
} tc_latency_result;

/* Trạng thái cao độ cho đồng hồ cao độ (từ v0.3). */
typedef struct tc_pitch_state {
  uint32_t struct_size;
  int32_t voiced;         /* 1 nếu đang có giọng hát */
  float f0_hz;            /* cao độ người hát (Hz), 0 khi không có giọng */
  float confidence;       /* 0..1 */
  float input_note;       /* cao độ người hát dạng số MIDI thực (69 = La 440 Hz) */
  int32_t target_note;    /* nốt đích (MIDI), -1 nếu chưa có */
  float cents_off;        /* người hát lệch bao nhiêu cent so với nốt đích */
  float correction_cents; /* engine đang dịch bao nhiêu cent */
  int32_t enabled;        /* 1 nếu tự chỉnh giọng đang bật */
} tc_pitch_state;

typedef struct tc_event {
  int32_t type; /* tc_event_type */
  int32_t code;
  double value;
} tc_event;

/* ---- Phân tích bài (từ v0.4, xem docs/adr/003-song-analyzer-api.md) ---- */

typedef enum tc_analysis_state {
  TC_ANALYSIS_IDLE = 0,     /* chưa chạy */
  TC_ANALYSIS_RUNNING = 1,  /* đang chạy ở luồng nền */
  TC_ANALYSIS_DONE = 2,     /* xong, đọc bằng tc_analyzer_get_result */
  TC_ANALYSIS_FAILED = 3,   /* lỗi: tc_analysis_status.result */
  TC_ANALYSIS_CANCELLED = 4 /* bị huỷ */
} tc_analysis_state;

/* Giai đoạn đang chạy, theo thứ tự (để UI hiện "Đang dò tone…"). */
typedef enum tc_analysis_stage {
  TC_STAGE_NONE = 0,
  TC_STAGE_DECODING = 1,   /* giải mã file */
  TC_STAGE_LOUDNESS = 2,   /* đo độ to */
  TC_STAGE_RESAMPLING = 3, /* chuẩn bị tín hiệu */
  TC_STAGE_SEPARATING = 4, /* tách nhạc cụ hoà âm / trống */
  TC_STAGE_SPECTRUM = 5,   /* phổ cao độ */
  TC_STAGE_KEY = 6,        /* dò tone + chuyển tone */
  TC_STAGE_TEMPO = 7,      /* dò BPM */
  TC_STAGE_DONE = 8
} tc_analysis_stage;

typedef struct tc_analysis_status {
  uint32_t struct_size; /* = sizeof(tc_analysis_status) */
  int32_t state;        /* tc_analysis_state */
  int32_t stage;        /* tc_analysis_stage */
  float progress;       /* 0..1 */
  int32_t result;       /* tc_result của lần chạy gần nhất (TC_OK khi chưa xong) */
} tc_analysis_status;

/* Một đoạn tone: [start_ms, end_ms). Kích thước cố định (dùng trong mảng) — muốn mở rộng thì
 * thêm struct mới, không sửa struct này. */
typedef struct tc_key_segment {
  double start_ms;
  double end_ms;
  int32_t root;     /* 0 = Đô (C) … 11 = Si (B) */
  int32_t scale;    /* tc_scale; phân tích chỉ trả TC_SCALE_MAJOR hoặc TC_SCALE_NATURAL_MINOR */
  float confidence; /* 0..1 (đầu vào của tc_set_key_timeline: bỏ qua) */
} tc_key_segment;

typedef struct tc_analysis_result {
  uint32_t struct_size; /* = sizeof(tc_analysis_result) */
  double duration_ms;
  uint32_t sample_rate; /* của file gốc */
  uint32_t channels;
  int32_t has_key; /* 0 nếu không kết luận được (bài quá ngắn, chỉ có trống…) */
  int32_t key_root;
  int32_t key_scale; /* tc_scale */
  float key_confidence;
  int32_t has_tuning;
  float tuning_cents; /* lệch so với La = 440 Hz, −50…+50 → đưa vào tc_set_tuning */
  int32_t has_bpm;
  float bpm;
  float bpm_confidence;
  int32_t has_loudness;
  float integrated_lufs; /* ITU-R BS.1770-4 */
  float sample_peak_db;
  uint32_t timeline_count; /* số đoạn tone — đọc bằng tc_analyzer_get_key_timeline */
} tc_analysis_result;

typedef struct tc_analyzer tc_analyzer;

/* ---- Thông tin thư viện ---- */
TC_EXPORT uint32_t tc_get_api_version(void);
TC_EXPORT const char* tc_get_version_string(void);
TC_EXPORT const char* tc_result_to_string(tc_result result);

/* ---- Vòng đời engine ---- */
TC_EXPORT void tc_engine_config_init(tc_engine_config* config);
TC_EXPORT tc_result tc_engine_create(const tc_engine_config* config, tc_engine** out_engine);
/* Với TC_BACKEND_PLATFORM: mở thiết bị âm thanh. Cần quyền micro (app xin trước). */
TC_EXPORT tc_result tc_engine_start(tc_engine* engine);
TC_EXPORT tc_result tc_engine_stop(tc_engine* engine);
/* Cho phép engine == NULL. */
TC_EXPORT void tc_engine_destroy(tc_engine* engine);

/* ---- Tham số ---- */
TC_EXPORT tc_result tc_set_param(tc_engine* engine, int32_t node_id, int32_t param_id, float value);
TC_EXPORT tc_result tc_get_param(tc_engine* engine, int32_t node_id, int32_t param_id,
                                 float* out_value);

/* ---- Beat ----
 * tc_load_beat chạy nền; kết quả đến qua sự kiện TC_EVENT_BEAT_LOADED.
 * Hỗ trợ định dạng mà codec của hệ điều hành đọc được (MP3, M4A/AAC, WAV...). */
TC_EXPORT tc_result tc_load_beat(tc_engine* engine, const char* path_utf8);
TC_EXPORT tc_result tc_play(tc_engine* engine);
TC_EXPORT tc_result tc_pause(tc_engine* engine);
TC_EXPORT tc_result tc_seek(tc_engine* engine, double position_ms);
TC_EXPORT tc_result tc_get_playback_state(tc_engine* engine, tc_playback_state* out_state);

/* ---- Đổi tone beat (từ v0.5, xem docs/adr/004-beat-transpose.md) ----
 * semitones −6…+6, giữ nguyên tempo và vị trí. Render ở luồng nền bắt đầu từ vị trí đang phát:
 * beat đang nghe tiếp tục tới khi bản mới sẵn tại vị trí phát (~0,1 s), rồi chuyển bằng crossfade
 * 20 ms. 0 = beat gốc (chuyển ngay). Gọi liên tiếp → chỉ lần cuối được render.
 * Tua tới vùng chưa render: beat im lặng, vị trí đứng yên tới khi vùng đó xong (~0,1 s).
 * Khi bản mới bắt đầu được nghe, engine tự cộng số nửa cung vào key chỉnh giọng (key chọn tay và
 * timeline vẫn là tone của beat GỐC) và phát TC_EVENT_KEY_CHANGED. Xong cả bài →
 * TC_EVENT_TRANSPOSE_READY. Cần đã nạp beat (TC_ERR_INVALID_STATE nếu chưa). tc_load_beat → tone về
 * 0. */
TC_EXPORT tc_result tc_set_transpose(tc_engine* engine, int32_t semitones);
TC_EXPORT tc_result tc_get_transpose(tc_engine* engine, int32_t* out_semitones);

/* ---- FX giọng (từ v0.6, xem docs/adr/005-fx-chain.md) ----
 * Chuỗi trên đường giọng nghe lại: micro → HPF 80 Hz → gate → chỉnh giọng → EQ → compressor →
 * de-esser → delay → reverb → trộn beat → limiter. Preset là một struct C (không JSON trong
 * engine). tc_apply_fx_preset đổi preset khi đang hát không click:
 *  - khác "cấu trúc" (bật / tắt khối, loại băng EQ, thời gian delay, kiểu phòng, pre-delay) → dựng
 *    chuỗi mới; phần khô crossfade 15 ms, đuôi delay / reverb của preset cũ ngân nốt (tới −80 dB,
 *    tối đa 4 s);
 *  - cùng cấu trúc (dB, tần số, Q, ngưỡng, tỉ lệ, độ ướt, hồi tiếp, RT60, damping…) → chuỗi đang
 *    chạy trượt dần sang giá trị mới.
 * Gọi liên tiếp trước khi audio kịp nhận → chỉ lần cuối được dùng. */
typedef enum tc_genre {
  TC_GENRE_DRY = 0, /* "Khô": không hiệu ứng */
  TC_GENRE_BOLERO = 1,
  TC_GENRE_BALLAD = 2,
  TC_GENRE_POP = 3, /* Nhạc trẻ */
  TC_GENRE_REMIX = 4,
  TC_GENRE_RAP = 5,
  TC_GENRE_LOFI = 6
} tc_genre;

typedef enum tc_eq_band_type {
  TC_EQ_OFF = 0,
  TC_EQ_BELL = 3,
  TC_EQ_LOW_SHELF = 4,
  TC_EQ_HIGH_SHELF = 5
} tc_eq_band_type;

typedef enum tc_delay_division {
  TC_DELAY_OFF = 0,
  TC_DELAY_QUARTER = 1,       /* 1/4 nốt (theo tc_set_tempo) */
  TC_DELAY_EIGHTH = 2,        /* 1/8 */
  TC_DELAY_DOTTED_EIGHTH = 3, /* 1/8 chấm */
  TC_DELAY_SIXTEENTH = 4,     /* 1/16 */
  TC_DELAY_MS = 5             /* theo delay_ms */
} tc_delay_division;

typedef enum tc_reverb_type {
  TC_REVERB_ROOM = 0,
  TC_REVERB_HALL = 1,
  TC_REVERB_PLATE = 2
} tc_reverb_type;

typedef struct tc_eq_band {
  int32_t type; /* tc_eq_band_type */
  float freq_hz;
  float gain_db; /* −18..18 */
  float q;       /* 0,1..10 */
} tc_eq_band;

typedef struct tc_fx_preset {
  uint32_t struct_size; /* = sizeof(tc_fx_preset); tc_fx_preset_init tự điền */
  /* EQ: thông cao / thông thấp bậc 2 (0 = tắt) + 5 băng */
  float eq_high_pass_hz;
  float eq_low_pass_hz;
  tc_eq_band eq_bands[5];
  /* Compressor */
  int32_t comp_enabled;
  float comp_threshold_db; /* dBFS (sin toàn thang = 0) */
  float comp_ratio;        /* ≥ 1 */
  float comp_knee_db;
  float comp_attack_ms;
  float comp_release_ms;
  float comp_makeup_db;
  /* De-esser (băng 5–9 kHz, shelf 5 kHz) */
  int32_t deess_enabled;
  float deess_threshold_db;
  float deess_max_reduction_db; /* 0..12 */
  /* Delay (mono; thời gian theo tc_set_tempo × phần nốt, hoặc delay_ms) */
  int32_t delay_division; /* tc_delay_division */
  float delay_ms;         /* khi TC_DELAY_MS: 1..2000 */
  float delay_feedback;   /* 0..0,5 */
  float delay_mix;        /* 0..1: độ lớn tiếng vọng so với tiếng khô */
  float delay_low_pass_hz;
  float delay_high_pass_hz;
  /* Reverb (FDN 8 dòng, stereo) */
  int32_t reverb_enabled;
  int32_t reverb_type;       /* tc_reverb_type */
  float reverb_decay_s;      /* RT60 0,2..8 */
  float reverb_pre_delay_ms; /* 0..100 */
  float reverb_damping;      /* 0..0,9: RT60 ở 4 kHz = decay · (1 − damping) */
  float reverb_mix;          /* 0..1: độ lớn tiếng vang so với tiếng khô (trong băng giọng) */
} tc_fx_preset;

/* Điền preset theo thể loại (giá trị khởi đầu của bảng mục 6, CLAUDE.md). genre lạ → "Khô".
 * Mức chỉnh giọng của thể loại do app đặt riêng (tc_set_correction_preset). */
TC_EXPORT void tc_fx_preset_init(tc_fx_preset* preset, int32_t genre);
/* Thông số ngoài khoảng thì bị kẹp; NaN hoặc struct_size sai → TC_ERR_INVALID_ARG. */
TC_EXPORT tc_result tc_apply_fx_preset(tc_engine* engine, const tc_fx_preset* preset);
/* Preset đang đặt (kể cả khi TC_NODE_FX đang tắt). */
TC_EXPORT tc_result tc_get_fx_preset(tc_engine* engine, tc_fx_preset* out_preset);
/* Nhịp bài (BPM) cho delay theo nốt; ≤ 0 = không rõ (100). Thường gọi khi nạp bài (BPM từ phân
 * tích). */
TC_EXPORT tc_result tc_set_tempo(tc_engine* engine, float bpm);
TC_EXPORT tc_result tc_get_tempo(tc_engine* engine, float* out_bpm);

/* ---- Route ----
 * Android: lớp Kotlin báo loại thiết bị (NDK không đọc được). iOS: engine tự phát hiện. */
TC_EXPORT tc_result tc_set_route_hint(tc_engine* engine, int32_t route);

/* ---- Đo độ trễ ----
 * Engine phải đang RUNNING. Trong lúc đo (~1,5 s) beat và nghe lại giọng bị tắt tiếng.
 * Kết thúc → sự kiện TC_EVENT_LATENCY_DONE, đọc chi tiết bằng tc_get_latency_result. */
TC_EXPORT tc_result tc_measure_latency_start(tc_engine* engine);
TC_EXPORT tc_result tc_get_latency_result(tc_engine* engine, tc_latency_result* out_result);

/* ---- Sự kiện ----
 * Gọi định kỳ từ luồng UI (vd. mỗi khung hình). Trả về số sự kiện đã ghi (>= 0) hoặc mã lỗi. */
TC_EXPORT int32_t tc_poll_events(tc_engine* engine, tc_event* out_events, uint32_t max_events);

/* ---- Xử lý offline (chỉ với TC_BACKEND_NULL, engine phải đang RUNNING) ----
 * input: interleaved, input_channels kênh; được phép NULL (coi như im lặng).
 * output: interleaved, output_channels kênh.
 * frames: <= max_block_frames. */
TC_EXPORT tc_result tc_engine_process_offline(tc_engine* engine, const float* input, float* output,
                                              uint32_t frames);

/* ---- Tự chỉnh giọng (từ v0.3) ----
 * Dò cao độ luôn chạy khi engine RUNNING (đồng hồ cao độ dùng được cả khi tắt chỉnh giọng).
 * Bật/tắt bằng tc_set_param(TC_NODE_PITCH, TC_PARAM_ENABLED, 0|1). Khi bật, giọng nghe lại
 * trễ thêm 10 ms (xem tc_metrics.algorithm_latency_frames); chuyển bật/tắt có crossfade 20 ms. */
/* root: 0 = Đô (C) … 11 = Si (B); scale: tc_scale. */
TC_EXPORT tc_result tc_set_key(tc_engine* engine, int32_t root, int32_t scale);
/* Key của beat GỐC: chọn tay, hoặc đoạn timeline đang áp dụng. */
TC_EXPORT tc_result tc_get_key(tc_engine* engine, int32_t* out_root, int32_t* out_scale);
/* (từ v0.5) Key đang dùng để chỉnh giọng = tc_get_key + số nửa cung beat đang nghe. Hiển thị cái
 * này. */
TC_EXPORT tc_result tc_get_applied_key(tc_engine* engine, int32_t* out_root, int32_t* out_scale);
/* preset: tc_correction_preset. Ghi đè các tham số RETUNE_MS…HYSTERESIS_CENTS. */
TC_EXPORT tc_result tc_set_correction_preset(tc_engine* engine, int32_t preset);
/* out_state->struct_size phải = sizeof(tc_pitch_state). Gọi từ luồng UI (~30 lần/giây). */
TC_EXPORT tc_result tc_get_pitch_state(tc_engine* engine, tc_pitch_state* out_state);

/* ---- Theo tone của bài (từ v0.4) ----
 * Lưới nốt của tự chỉnh giọng dời `cents` so với La = 440 Hz (−100…100). Dùng tuning_cents của
 * tc_analysis_result để giọng được kéo về đúng cao độ của beat. Đồng hồ cao độ đo theo cùng lưới.
 */
TC_EXPORT tc_result tc_set_tuning(tc_engine* engine, float cents);
TC_EXPORT tc_result tc_get_tuning(tc_engine* engine, float* out_cents);
/* Engine tự đổi key chỉnh giọng theo vị trí beat (cả khi tua). Các đoạn phải tăng dần theo
 * start_ms, không chồng nhau, tối đa 256 đoạn; khoảng trống giữa hai đoạn giữ key đoạn trước.
 * count = 0 → tắt. Gọi tc_set_key → tắt chế độ theo timeline (người dùng chọn tay).
 * Mỗi lần engine tự đổi key → sự kiện TC_EVENT_KEY_CHANGED; tc_get_key trả key đoạn đang áp dụng
 * (tone beat gốc), tc_get_applied_key cộng thêm tone đang nghe. */
TC_EXPORT tc_result tc_set_key_timeline(tc_engine* engine, const tc_key_segment* segments,
                                        uint32_t count);

/* ---- Phân tích bài (từ v0.4) ----
 * Độc lập với tc_engine (chạy được khi engine chưa start, vd. lúc nhập thư viện bài). Mỗi analyzer
 * có một luồng nền riêng; UI kéo trạng thái bằng tc_analyzer_get_status (~10 lần/giây).
 * Bài 4 phút mất ~1 s trên máy mạnh. Gọi các hàm của cùng một analyzer từ một luồng. */
TC_EXPORT tc_result tc_analyzer_create(tc_analyzer** out_analyzer);
/* Bắt đầu phân tích file (định dạng như tc_load_beat). Đang chạy → TC_ERR_INVALID_STATE.
 * Kết quả lần trước bị xoá. Lỗi đọc file báo qua trạng thái TC_ANALYSIS_FAILED. */
TC_EXPORT tc_result tc_analyzer_start(tc_analyzer* analyzer, const char* path_utf8);
TC_EXPORT tc_result tc_analyzer_get_status(tc_analyzer* analyzer, tc_analysis_status* out_status);
/* Yêu cầu dừng (không chờ). Trạng thái chuyển sang TC_ANALYSIS_CANCELLED khi luồng nền dừng. */
TC_EXPORT tc_result tc_analyzer_cancel(tc_analyzer* analyzer);
/* Chỉ khi trạng thái là TC_ANALYSIS_DONE, ngược lại TC_ERR_INVALID_STATE. */
TC_EXPORT tc_result tc_analyzer_get_result(tc_analyzer* analyzer, tc_analysis_result* out_result);
/* Ghi tối đa `max_segments` đoạn vào `out_segments`, trả số đoạn đã ghi (≥ 0) hoặc mã lỗi.
 * out_segments = NULL và max_segments = 0 → trả tổng số đoạn. Chỉ khi TC_ANALYSIS_DONE. */
TC_EXPORT int32_t tc_analyzer_get_key_timeline(tc_analyzer* analyzer, tc_key_segment* out_segments,
                                               uint32_t max_segments);
/* Huỷ việc đang chạy và chờ luồng nền dừng (thường < 50 ms). Cho phép analyzer == NULL. */
TC_EXPORT void tc_analyzer_destroy(tc_analyzer* analyzer);

/* ---- Đo lường ---- */
TC_EXPORT tc_result tc_get_metrics(tc_engine* engine, tc_metrics* out_metrics);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TONECORE_H */
