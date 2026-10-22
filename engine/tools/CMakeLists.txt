# Công cụ dòng lệnh chạy trên máy dev (không ship trong app).
add_executable(pitchlab pitchlab.cpp)
target_link_libraries(pitchlab PRIVATE tonecore_core)
tc_set_warnings(pitchlab)
target_compile_options(pitchlab PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(pitchlab)

add_executable(keylab keylab.cpp)
target_link_libraries(keylab PRIVATE tonecore_core)
tc_set_warnings(keylab)
target_compile_options(keylab PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
tc_apply_sanitizer(keylab)
