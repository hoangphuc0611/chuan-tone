include(FetchContent)

# GoogleTest (BSD-3-Clause) – chỉ dùng khi build test, không ship trong app.
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(tonecore_tests
  unit/spsc_ring_test.cpp
  unit/smoothed_param_test.cpp
  unit/c_api_test.cpp
  unit/mls_test.cpp
  unit/latency_probe_test.cpp
  unit/resampler_test.cpp
  unit/decoder_test.cpp
  unit/engine_m1_test.cpp
  unit/dsp_test.cpp
  unit/pitch_detect_test.cpp
  unit/corrector_test.cpp
  unit/psola_test.cpp
  unit/voice_pitch_node_test.cpp
  unit/rt_alloc_test.cpp
  unit/song_analyzer_test.cpp
  unit/hpss_test.cpp
  unit/cqt_test.cpp
  unit/tuning_chroma_test.cpp
  unit/key_detect_test.cpp
  unit/key_timeline_test.cpp
  unit/loudness_test.cpp
  unit/tempo_test.cpp
  unit/analyzer_api_test.cpp
  unit/engine_m3_test.cpp
  unit/pitch_shifter_test.cpp
  unit/transient_detector_test.cpp
  unit/beat_transpose_test.cpp
  unit/engine_m4_test.cpp
  unit/engine_m5_test.cpp
  unit/peak_limiter_test.cpp
  unit/fx_eval_test.cpp
  unit/fx_eq_test.cpp
  unit/fx_gate_test.cpp
  unit/fx_dynamics_test.cpp
  unit/fx_space_test.cpp
  unit/fx_rack_test.cpp
)
target_link_libraries(tonecore_tests PRIVATE tonecore_testutil GTest::gtest_main)
tc_set_warnings(tonecore_tests)
# Test chủ ý tính sai số bằng double; giữ cảnh báo này cho mã engine.
target_compile_options(tonecore_tests PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(tonecore_tests)

include(GoogleTest)
if(NOT CMAKE_CROSSCOMPILING)  # iOS Simulator: chạy thẳng file test bằng `xcrun simctl spawn`
  gtest_discover_tests(tonecore_tests DISCOVERY_TIMEOUT 30)
endif()
