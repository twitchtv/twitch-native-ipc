cmake_minimum_required(VERSION 3.12)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmakeUtils;")
include(setup)

project(nativeipc)

setup_project()

add_subdirectory(libnativeipc)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
