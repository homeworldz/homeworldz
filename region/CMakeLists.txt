add_library(homeworldz-region-core src/http_response.cpp)
target_include_directories(homeworldz-region-core PUBLIC include)
target_compile_features(homeworldz-region-core PUBLIC cxx_std_20)

add_executable(homeworldz-region src/main.cpp)
target_link_libraries(homeworldz-region PRIVATE homeworldz-region-core)

if(MSVC)
  target_compile_options(homeworldz-region PRIVATE /W4 /permissive-)
  target_link_libraries(homeworldz-region PRIVATE ws2_32)
else()
  target_compile_options(homeworldz-region PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(homeworldz-region-tests tests/http_response_test.cpp)
target_link_libraries(homeworldz-region-tests PRIVATE homeworldz-region-core)
add_test(NAME region-http-response COMMAND homeworldz-region-tests)

