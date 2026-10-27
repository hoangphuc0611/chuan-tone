cmake_minimum_required(VERSION 3.22)

project(ToneCore VERSION 0.5.0 LANGUAGES C CXX)

# ---- Tùy chọn ----
option(TC_BUILD_TESTS "Build unit test (GoogleTest)" ON)
option(TC_BUILD_BENCH "Build benchmark (Google Benchmark)" OFF)
option(TC_BUILD_TOOLS "Build công cụ dòng lệnh (pitchlab, keylab, beatlab) cho máy dev" ON)
option(TC_BUILD_FRAMEWORK "Build tonecore dạng Apple framework (iOS)" OFF)
set(TC_SANITIZER "" CACHE STRING "Sanitizer cho bản host: address, undefined, thread hoặc rỗng")

# Khi engine được build như thư mục con (vd. từ Gradle của plugin Flutter) thì tắt test.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(TC_BUILD_TESTS OFF)
  set(TC_BUILD_BENCH OFF)
  set(TC_BUILD_TOOLS OFF)
endif()
if(ANDROID OR IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(TC_BUILD_TESTS OFF)
  set(TC_BUILD_BENCH OFF)
  set(TC_BUILD_TOOLS OFF)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)

configure_file(cmake/tonecore_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tonecore_version.h @ONLY)

# ---- tonecore_core: thư viện tĩnh nội bộ (test và bench link trực tiếp) ----
add_library(tonecore_core STATIC
  src/core/engine.cpp
  src/api/tonecore.cpp
  src/api/analyzer.cpp
  src/latency/mls.cpp
  src/latency/latency_probe.cpp
  src/dsp/resampler.cpp
  src/dsp/fft.cpp
  src/dsp/decimator.cpp
  src/pitch/mpm.cpp
  src/pitch/yin.cpp
  src/pitch/f0_tracker.cpp
  src/pitch/pitch_analyzer.cpp
  src/pitch/scales.cpp
  src/pitch/corrector.cpp
  src/pitch/psola.cpp
  src/pitch/voice_pitch_node.cpp
  src/player/track.cpp
  src/player/beat_player.cpp
  src/player/beat_renderer.cpp
  src/key/hpss.cpp
  src/key/tuning.cpp
  src/key/cqt.cpp
  src/key/chroma.cpp
  src/key/key_profiles.cpp
  src/key/key_detect.cpp
  src/key/key_timeline.cpp
  src/key/loudness.cpp
  src/key/tempo.cpp
  src/key/song_analyzer.cpp
  src/stretch/pitch_shifter.cpp
  src/stretch/transient_detector.cpp
  src/stretch/peak_limiter.cpp
)

# Mã theo nền tảng: backend âm thanh + bộ giải mã file.
if(ANDROID)
  target_sources(tonecore_core PRIVATE
    src/io/android/oboe_backend.cpp
    src/decode/decoder_android.cpp)
  find_package(oboe REQUIRED CONFIG)  # Oboe qua Gradle prefab (Apache-2.0)
  target_link_libraries(tonecore_core PUBLIC oboe::oboe mediandk log)
elseif(IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  target_sources(tonecore_core PRIVATE
    src/io/ios/remoteio_backend.mm
    src/decode/decoder_apple.cpp)
  set_source_files_properties(src/io/ios/remoteio_backend.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(tonecore_core PUBLIC
    "-framework AudioToolbox" "-framework AVFAudio" "-framework CoreFoundation" "-framework Foundation")
elseif(APPLE)
  # macOS host: giải mã bằng ExtAudioFile (cùng mã với iOS → kiểm được trên máy dev),
  # dr_libs để test so sánh; chưa có backend âm thanh thật.
  target_sources(tonecore_core PRIVATE
    src/io/host_backend.cpp
    src/decode/decoder_apple.cpp
    src/decode/decoder_drlibs.cpp)
  target_link_libraries(tonecore_core PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
else()
  target_sources(tonecore_core PRIVATE
    src/io/host_backend.cpp
    src/decode/decoder_drlibs.cpp)
endif()

if(NOT ANDROID AND NOT (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  # dr_libs chỉ dùng cho bản host (test/CLI), không ship trong app.
  target_include_directories(tonecore_core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs)
  find_package(Threads REQUIRED)
  target_link_libraries(tonecore_core PUBLIC Threads::Threads)
endif()

target_include_directories(tonecore_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_definitions(tonecore_core PUBLIC TONECORE_BUILDING=1)
tc_set_warnings(tonecore_core)
tc_apply_sanitizer(tonecore_core)

# ---- tonecore: thư viện động export C API (Dart FFI nạp file này) ----
add_library(tonecore SHARED src/api/export_anchor.c)
target_link_libraries(tonecore PRIVATE tonecore_core)
target_include_directories(tonecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
tc_apply_sanitizer(tonecore)

# Giữ lại toàn bộ ký hiệu tc_* từ thư viện tĩnh (không để linker loại bỏ vì "không dùng").
if(APPLE)
  target_link_options(tonecore PRIVATE "LINKER:-force_load,$<TARGET_FILE:tonecore_core>")
elseif(UNIX)
  target_link_options(tonecore PRIVATE
    "LINKER:--whole-archive" "$<TARGET_FILE:tonecore_core>" "LINKER:--no-whole-archive")
endif()

if(ANDROID)
  # Trang bộ nhớ 16 KB (bắt buộc với app mới trên Google Play từ Android 15).
  target_link_options(tonecore PRIVATE "LINKER:-z,max-page-size=16384")
endif()

if(TC_BUILD_FRAMEWORK)
  set_target_properties(tonecore PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER vn.chuantone.tonecore
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_FRAMEWORK_BUNDLE_VERSION ${PROJECT_VERSION}
    PUBLIC_HEADER include/tonecore/tonecore.h
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION YES
  )
endif()

# ---- Test & bench ----
if(TC_BUILD_TESTS OR TC_BUILD_BENCH OR TC_BUILD_TOOLS)
  add_subdirectory(tests/util)
endif()

if(TC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TC_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(TC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
