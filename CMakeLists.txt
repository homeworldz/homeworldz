cmake_minimum_required(VERSION 3.24)
project(homeworldz VERSION 0.1.0 LANGUAGES CXX)

find_package(unofficial-sqlite3 CONFIG QUIET)
add_library(homeworldz-sqlite INTERFACE)
if(TARGET unofficial::sqlite3::sqlite3)
  target_link_libraries(homeworldz-sqlite INTERFACE unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  target_link_libraries(homeworldz-sqlite INTERFACE SQLite::SQLite3)
endif()

enable_testing()
add_subdirectory(region)

