# Tiện ích dùng chung (giọng / nhạc tổng hợp, bộ tín hiệu + chỉ số đổi tone) — cho test, bench và
# công cụ dòng lệnh. Không phụ thuộc GoogleTest.
add_library(tonecore_testutil STATIC synth_voice.cpp synth_music.cpp stretch_eval.cpp fx_eval.cpp wav_out.cpp)
target_include_directories(tonecore_testutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tonecore_testutil PUBLIC tonecore_core)
target_include_directories(tonecore_testutil SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
tc_set_warnings(tonecore_testutil)
target_compile_options(tonecore_testutil PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(tonecore_testutil)
