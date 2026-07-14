cmake_minimum_required(VERSION 3.24)
project(homeworldz VERSION 0.1.0 LANGUAGES CXX)

set(HOMEWORLDZ_VERSION "dev" CACHE STRING "Version reported by HomeWorldz executables")

find_package(unofficial-sqlite3 CONFIG QUIET)
find_package(Jolt CONFIG QUIET)
find_package(unofficial-omniverse-physx-sdk CONFIG QUIET)
set(HOMEWORLDZ_PHYSICS_ADAPTERS_AVAILABLE OFF)
if(TARGET Jolt::Jolt AND TARGET unofficial::omniverse-physx-sdk::sdk)
  set(HOMEWORLDZ_PHYSICS_ADAPTERS_AVAILABLE ON)
endif()
add_library(homeworldz-sqlite INTERFACE)
if(TARGET unofficial::sqlite3::sqlite3)
  target_link_libraries(homeworldz-sqlite INTERFACE unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  target_link_libraries(homeworldz-sqlite INTERFACE SQLite::SQLite3)
endif()

enable_testing()
add_subdirectory(region)

