cmake_minimum_required(VERSION 3.22)

project(ToneCore VERSION 0.1.0 LANGUAGES C CXX)

# ---- Tùy chọn ----
option(TC_BUILD_TESTS "Build unit test (GoogleTest)" ON)
option(TC_BUILD_BENCH "Build benchmark (Google Benchmark)" OFF)
option(TC_BUILD_FRAMEWORK "Build tonecore dạng Apple framework (iOS)" OFF)
set(TC_SANITIZER "" CACHE STRING "Sanitizer cho bản host: address, undefined, thread hoặc rỗng")

# Khi engine được build như thư mục con (vd. từ Gradle của plugin Flutter) thì tắt test.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(TC_BUILD_TESTS OFF)
  set(TC_BUILD_BENCH OFF)
endif()
if(ANDROID OR IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(TC_BUILD_TESTS OFF)
  set(TC_BUILD_BENCH OFF)
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
)
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
if(TC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TC_BUILD_BENCH)
  add_subdirectory(bench)
endif()
