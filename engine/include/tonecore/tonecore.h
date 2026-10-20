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
#define TC_API_VERSION_MINOR 1
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
  TC_ERR_INTERNAL = -5
};

typedef enum tc_backend {
  /* Không có thiết bị âm thanh; xử lý bằng tc_engine_process_offline (test, render). */
  TC_BACKEND_NULL = 0,
  /* Oboe trên Android, RemoteIO trên iOS (từ M1). */
  TC_BACKEND_PLATFORM = 1
} tc_backend;

typedef enum tc_engine_state { TC_STATE_STOPPED = 0, TC_STATE_RUNNING = 1 } tc_engine_state;

/* Định danh node / tham số dùng cho tc_set_param. Danh sách sẽ mở rộng theo milestone. */
typedef enum tc_node_id { TC_NODE_MASTER = 0 } tc_node_id;

typedef enum tc_param_id {
  /* Hệ số khuếch đại tuyến tính, 0..4. Được làm mượt trong audio thread. */
  TC_PARAM_GAIN = 0
} tc_param_id;

typedef struct tc_engine tc_engine;

typedef struct tc_engine_config {
  uint32_t struct_size;      /* = sizeof(tc_engine_config); tc_engine_config_init tự điền */
  uint32_t sample_rate;      /* Hz, mặc định 48000 */
  uint32_t max_block_frames; /* số frame tối đa mỗi lần xử lý, mặc định 1024 */
  uint32_t input_channels;   /* mặc định 1 (micro mono) */
  uint32_t output_channels;  /* mặc định 2 (stereo) */
  int32_t backend;           /* tc_backend, mặc định TC_BACKEND_NULL */
} tc_engine_config;

typedef struct tc_metrics {
  uint32_t struct_size; /* người gọi điền = sizeof(tc_metrics) */
  int32_t state;        /* tc_engine_state */
  uint32_t sample_rate;
  uint64_t frames_processed; /* tổng số frame đã xử lý */
  uint64_t callbacks;        /* số lần gọi xử lý */
  uint32_t xruns;            /* số lần thiếu/tràn buffer (từ M1) */
  float cpu_load;            /* 0..1 – tỉ lệ thời gian xử lý / thời lượng block gần nhất */
} tc_metrics;

/* ---- Thông tin thư viện ---- */
TC_EXPORT uint32_t tc_get_api_version(void);
TC_EXPORT const char* tc_get_version_string(void);
TC_EXPORT const char* tc_result_to_string(tc_result result);

/* ---- Vòng đời engine ---- */
TC_EXPORT void tc_engine_config_init(tc_engine_config* config);
TC_EXPORT tc_result tc_engine_create(const tc_engine_config* config, tc_engine** out_engine);
TC_EXPORT tc_result tc_engine_start(tc_engine* engine);
TC_EXPORT tc_result tc_engine_stop(tc_engine* engine);
/* Cho phép engine == NULL. */
TC_EXPORT void tc_engine_destroy(tc_engine* engine);

/* ---- Tham số ---- */
TC_EXPORT tc_result tc_set_param(tc_engine* engine, int32_t node_id, int32_t param_id, float value);
TC_EXPORT tc_result tc_get_param(tc_engine* engine, int32_t node_id, int32_t param_id,
                                 float* out_value);

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
