add_library(homeworldz-region-core
  src/api_models.cpp
  src/http_response.cpp
  src/scene.cpp
  src/simulation_loop.cpp)
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
target_compile_definitions(homeworldz-region-tests PRIVATE
  HOMEWORLDZ_OPERATIONAL_CONTRACT_PATH="${CMAKE_SOURCE_DIR}/api/contracts/operational.tsv")
add_test(NAME region-http-response COMMAND homeworldz-region-tests)

add_executable(homeworldz-scene-tests tests/scene_test.cpp)
target_link_libraries(homeworldz-scene-tests PRIVATE homeworldz-region-core)
add_test(NAME region-scene COMMAND homeworldz-scene-tests)

