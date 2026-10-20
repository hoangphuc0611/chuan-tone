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
)
target_link_libraries(tonecore_tests PRIVATE tonecore_core GTest::gtest_main)
tc_set_warnings(tonecore_tests)
tc_apply_sanitizer(tonecore_tests)

include(GoogleTest)
gtest_discover_tests(tonecore_tests DISCOVERY_TIMEOUT 30)
