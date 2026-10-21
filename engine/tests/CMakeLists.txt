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

# Tiện ích test dùng chung (giọng tổng hợp…) — cả test, bench và công cụ dùng được.
add_library(tonecore_testutil STATIC util/synth_voice.cpp)
target_include_directories(tonecore_testutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
tc_set_warnings(tonecore_testutil)
target_compile_options(tonecore_testutil PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(tonecore_testutil)

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
)
target_link_libraries(tonecore_tests PRIVATE tonecore_core tonecore_testutil GTest::gtest_main)
tc_set_warnings(tonecore_tests)
# Test chủ ý tính sai số bằng double; giữ cảnh báo này cho mã engine.
target_compile_options(tonecore_tests PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(tonecore_tests)

include(GoogleTest)
gtest_discover_tests(tonecore_tests DISCOVERY_TIMEOUT 30)
