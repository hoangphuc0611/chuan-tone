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
#define TC_API_VERSION_MINOR 2
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
  TC_ERR_DEVICE = -6
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
  TC_NODE_BEAT = 2     /* nhạc beat */
} tc_node_id;

typedef enum tc_param_id {
  /* Hệ số khuếch đại tuyến tính, 0..4. Được làm mượt trong audio thread. */
  TC_PARAM_GAIN = 0,
  /* Bật/tắt: 0 hoặc 1 (chỉ TC_NODE_MONITOR). */
  TC_PARAM_ENABLED = 1
} tc_param_id;

/* Sự kiện gửi lên UI, lấy bằng tc_poll_events. */
typedef enum tc_event_type {
  TC_EVENT_NONE = 0,
  TC_EVENT_BEAT_LOADED = 1,      /* code = tc_result; value = thời lượng (ms) */
  TC_EVENT_BEAT_ENDED = 2,       /* phát hết bài */
  TC_EVENT_ROUTE_CHANGED = 3,    /* code = tc_route mới */
  TC_EVENT_INTERRUPTED = 4,      /* code = 1 bắt đầu bị ngắt (cuộc gọi...), 0 kết thúc */
  TC_EVENT_LATENCY_DONE = 5,     /* code = tc_result; value = độ trễ (ms) nếu thành công */
  TC_EVENT_STREAM_RESTARTED = 6, /* code = tc_result của lần mở lại stream */
  TC_EVENT_MONITOR_BLOCKED = 7   /* nghe lại giọng bị chặn vì đang dùng Bluetooth */
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
} tc_metrics;

typedef struct tc_playback_state {
  uint32_t struct_size;
  int32_t has_beat;
  int32_t playing;
  double position_ms;
  double duration_ms;
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

typedef struct tc_event {
  int32_t type; /* tc_event_type */
  int32_t code;
  double value;
} tc_event;

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

/* ---- Đo lường ---- */
TC_EXPORT tc_result tc_get_metrics(tc_engine* engine, tc_metrics* out_metrics);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TONECORE_H */
