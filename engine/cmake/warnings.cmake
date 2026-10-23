function(tc_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
      -Wdouble-promotion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
    # clang trên Linux (CI) tính cảnh báo này vào -Wconversion, Apple clang thì không → bật hẳn để
    # máy dev bắt được lỗi giống CI (size_t → double/float ngầm định).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -Wimplicit-int-float-conversion)
    endif()
    if(TC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
