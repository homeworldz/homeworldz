add_library(homeworldz-region-core
  src/avatar_controller.cpp
  src/api_models.cpp
  src/falcon_runtime.cpp
  src/grid_client.cpp
  src/http_response.cpp
  src/image.cpp
  src/inventory_asset.cpp
  src/object_asset.cpp
  src/physics_scene.cpp
  src/physics_scenarios.cpp
  src/region_config.cpp
  src/region_transit.cpp
  src/region_storage.cpp
  src/scene.cpp
  src/sha256.cpp
  src/simulation_loop.cpp)
target_sources(homeworldz-region-core PRIVATE src/terrain_edit.cpp)
target_sources(homeworldz-region-core PRIVATE src/viewer_protocol.cpp)
target_sources(homeworldz-region-core PRIVATE src/viewer_capabilities.cpp)
target_include_directories(homeworldz-region-core PUBLIC include)
target_compile_features(homeworldz-region-core PUBLIC cxx_std_20)
target_compile_definitions(homeworldz-region-core PUBLIC
  HOMEWORLDZ_VERSION="${HOMEWORLDZ_VERSION}")
target_link_libraries(homeworldz-region-core PUBLIC homeworldz-sqlite homeworldz-script homeworldz-openjpeg)
if(HOMEWORLDZ_JOLT_AVAILABLE)
  target_sources(homeworldz-region-core PRIVATE src/jolt_world.cpp)
  target_link_libraries(homeworldz-region-core PUBLIC Jolt::Jolt)
  target_compile_definitions(homeworldz-region-core PUBLIC HOMEWORLDZ_JOLT_AVAILABLE=1)
endif()
if(HOMEWORLDZ_PHYSX_AVAILABLE)
  target_sources(homeworldz-region-core PRIVATE src/physx_world.cpp)
  target_link_libraries(homeworldz-region-core PUBLIC unofficial::omniverse-physx-sdk::sdk)
endif()

add_executable(homeworldz-region src/main.cpp)
target_link_libraries(homeworldz-region PRIVATE homeworldz-region-core)

if(MSVC)
  target_compile_options(homeworldz-region PRIVATE /W4 /permissive-)
  target_link_libraries(homeworldz-region-core PUBLIC ws2_32)
else()
  target_compile_options(homeworldz-region PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(homeworldz-region-tests tests/http_response_test.cpp)
target_link_libraries(homeworldz-region-tests PRIVATE homeworldz-region-core)
target_compile_definitions(homeworldz-region-tests PRIVATE
  HOMEWORLDZ_OPERATIONAL_CONTRACT_PATH="${CMAKE_SOURCE_DIR}/api/contracts/operational.tsv")
add_test(NAME region-http-response COMMAND homeworldz-region-tests)

add_executable(homeworldz-image-tests tests/image_test.cpp)
target_link_libraries(homeworldz-image-tests PRIVATE homeworldz-region-core)
add_test(NAME region-image COMMAND homeworldz-image-tests)

add_executable(homeworldz-inventory-asset-tests tests/inventory_asset_test.cpp)
target_link_libraries(homeworldz-inventory-asset-tests PRIVATE homeworldz-region-core)
add_test(NAME region-inventory-asset COMMAND homeworldz-inventory-asset-tests)

add_executable(homeworldz-scene-tests tests/scene_test.cpp)
target_link_libraries(homeworldz-scene-tests PRIVATE homeworldz-region-core)
add_test(NAME region-scene COMMAND homeworldz-scene-tests)

add_executable(homeworldz-grid-client-tests tests/grid_client_test.cpp)
target_link_libraries(homeworldz-grid-client-tests PRIVATE homeworldz-region-core)
add_test(NAME region-grid-client COMMAND homeworldz-grid-client-tests)

add_executable(homeworldz-region-transit-tests tests/region_transit_test.cpp)
target_link_libraries(homeworldz-region-transit-tests PRIVATE homeworldz-region-core)
add_test(NAME region-transit COMMAND homeworldz-region-transit-tests)

add_executable(homeworldz-object-asset-tests tests/object_asset_test.cpp)
target_link_libraries(homeworldz-object-asset-tests PRIVATE homeworldz-region-core)
add_test(NAME region-object-asset COMMAND homeworldz-object-asset-tests)

add_executable(homeworldz-region-config-tests tests/region_config_test.cpp)
target_link_libraries(homeworldz-region-config-tests PRIVATE homeworldz-region-core)
add_test(NAME region-config COMMAND homeworldz-region-config-tests)

add_executable(homeworldz-storage-tests tests/region_storage_test.cpp)
target_link_libraries(homeworldz-storage-tests PRIVATE homeworldz-region-core)
add_test(NAME region-storage COMMAND homeworldz-storage-tests)

add_executable(homeworldz-physics-contract-tests tests/physics_contract_test.cpp)
target_link_libraries(homeworldz-physics-contract-tests PRIVATE homeworldz-region-core)
add_test(NAME region-physics-contract COMMAND homeworldz-physics-contract-tests)

add_executable(homeworldz-physics-scene-tests tests/physics_scene_test.cpp)
target_link_libraries(homeworldz-physics-scene-tests PRIVATE homeworldz-region-core)
add_test(NAME region-physics-scene COMMAND homeworldz-physics-scene-tests)

add_executable(homeworldz-viewer-protocol-tests tests/viewer_protocol_test.cpp)
target_link_libraries(homeworldz-viewer-protocol-tests PRIVATE homeworldz-region-core)
add_test(NAME region-viewer-protocol COMMAND homeworldz-viewer-protocol-tests)

add_executable(homeworldz-avatar-controller-tests tests/avatar_controller_test.cpp)
target_link_libraries(homeworldz-avatar-controller-tests PRIVATE homeworldz-region-core)
add_test(NAME region-avatar-controller COMMAND homeworldz-avatar-controller-tests)

add_executable(homeworldz-terrain-edit-tests tests/terrain_edit_test.cpp)
target_link_libraries(homeworldz-terrain-edit-tests PRIVATE homeworldz-region-core)
add_test(NAME region-terrain-edit COMMAND homeworldz-terrain-edit-tests)

add_executable(homeworldz-script-runtime-tests tests/script_runtime_test.cpp)
target_link_libraries(homeworldz-script-runtime-tests PRIVATE homeworldz-region-core)
add_test(NAME region-script-runtime COMMAND homeworldz-script-runtime-tests)

add_executable(homeworldz-falcon-runtime-tests tests/falcon_runtime_test.cpp)
target_link_libraries(homeworldz-falcon-runtime-tests PRIVATE homeworldz-region-core)
add_test(NAME region-falcon-runtime COMMAND homeworldz-falcon-runtime-tests)

if(HOMEWORLDZ_JOLT_AVAILABLE AND HOMEWORLDZ_PHYSX_AVAILABLE)
  add_executable(homeworldz-physics-adapter-smoke tests/physics_adapters_test.cpp)
  target_link_libraries(homeworldz-physics-adapter-smoke PRIVATE homeworldz-region-core)
  add_test(NAME region-physics-adapters COMMAND homeworldz-physics-adapter-smoke)
  add_executable(homeworldz-physics-scenario-tests tests/physics_scenarios_test.cpp)
  target_link_libraries(homeworldz-physics-scenario-tests PRIVATE homeworldz-region-core)
  add_test(NAME region-physics-scenarios COMMAND homeworldz-physics-scenario-tests)
  add_executable(homeworldz-physics-lab src/physics_lab.cpp)
  target_link_libraries(homeworldz-physics-lab PRIVATE homeworldz-region-core)
  if(WIN32)
    target_link_libraries(homeworldz-physics-lab PRIVATE psapi)
  endif()
endif()

