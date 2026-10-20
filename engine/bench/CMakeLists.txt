include(FetchContent)

# Google Benchmark (Apache-2.0) – chỉ dùng cho đo hiệu năng, không ship trong app.
FetchContent_Declare(benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
  URL_HASH SHA256=32131c08ee31eeff2c8968d7e874f3cb648034377dfc32a4c377fa8796d84981
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(tonecore_bench
  engine_bench.cpp
  spsc_ring_bench.cpp
)
target_link_libraries(tonecore_bench PRIVATE tonecore_core benchmark::benchmark_main)
