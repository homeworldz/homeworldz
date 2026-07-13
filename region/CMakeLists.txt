add_executable(homeworldz-region src/main.cpp)
target_compile_features(homeworldz-region PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(homeworldz-region PRIVATE /W4 /permissive-)
  target_link_libraries(homeworldz-region PRIVATE ws2_32)
else()
  target_compile_options(homeworldz-region PRIVATE -Wall -Wextra -Wpedantic)
endif()

