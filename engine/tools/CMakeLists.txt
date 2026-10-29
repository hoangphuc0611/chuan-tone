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

# beatlab dùng bộ tín hiệu + chỉ số trong tests/util.
if(TARGET tonecore_testutil)
  add_executable(beatlab beatlab.cpp)
  target_link_libraries(beatlab PRIVATE tonecore_testutil)
  target_include_directories(beatlab SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
  tc_set_warnings(beatlab)
  target_compile_options(beatlab PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
  tc_apply_sanitizer(beatlab)

  add_executable(fxlab fxlab.cpp)
  target_link_libraries(fxlab PRIVATE tonecore_testutil)
  tc_set_warnings(fxlab)
  target_compile_options(fxlab PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
  tc_apply_sanitizer(fxlab)

  # Mốc so sánh Signalsmith Stretch (MIT) — chỉ trong beatlab, không vào libtonecore. Tải bằng
  # tools/stretch/fetch_signalsmith.sh (vào testdata/ref/, không commit).
  set(TC_SIGNALSMITH_DIR "${PROJECT_SOURCE_DIR}/../testdata/ref" CACHE PATH
      "Thư mục chứa signalsmith-stretch/ và signalsmith-linear/ (mốc so sánh của beatlab)")
  if(EXISTS "${TC_SIGNALSMITH_DIR}/signalsmith-stretch/include/signalsmith-stretch/signalsmith-stretch.h")
    message(STATUS "beatlab: có mốc so sánh Signalsmith Stretch (${TC_SIGNALSMITH_DIR})")
    target_sources(beatlab PRIVATE signalsmith_ref.cpp)
    target_include_directories(beatlab SYSTEM PRIVATE
      "${TC_SIGNALSMITH_DIR}/signalsmith-stretch/include"
      "${TC_SIGNALSMITH_DIR}/signalsmith-linear/include")
    target_compile_definitions(beatlab PRIVATE TC_HAVE_SIGNALSMITH=1)
    # Mã bên thứ ba: không áp -Werror / cảnh báo của dự án cho file bọc này.
    set_source_files_properties(signalsmith_ref.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endif()

# Nén / giải mã FLAC cho server tách giọng (M6) — dùng chính mã của engine.
add_executable(tcflac tcflac.cpp)
target_link_libraries(tcflac PRIVATE tonecore_core)
tc_set_warnings(tcflac)
tc_apply_sanitizer(tcflac)
