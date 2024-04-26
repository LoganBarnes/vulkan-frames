# ##############################################################################
# A Logan Thomas Barnes project
# ##############################################################################
cmake_minimum_required(VERSION 3.16.3)
project(LtbVlk VERSION 0.0.1)

# Prevent cyclic dependencies even for static libs
set(GLOBAL_DEPENDS_NO_CYCLES ON)

# Prevent cmake from creating build artifacts in the source directory
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

if (NOT UNIX OR APPLE)
  message(FATAL_ERROR "This project only supports Linux")
endif ()

# ##############################################################################
# External Packages
# ##############################################################################
include(cmake/CPM.cmake) # CMake Package Manager
include(cmake/ThirdParty.cmake)

# ##############################################################################
# Configuration
# ##############################################################################

# A directory where generated files can be stored and referenced
set(LTB_VLK_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/src/ltb/ltb_config.hpp.in
  ${LTB_VLK_GENERATED_DIR}/ltb/ltb_config.hpp
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/res/shaders)

# ##############################################################################
# Source Files
# ##############################################################################
file(GLOB_RECURSE LtbVlk_SOURCE_FILES
  LIST_DIRECTORIES false
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/src/ltb/*.cpp
)

# ##############################################################################
# Library
# ##############################################################################
add_library(
  LtbVlk
  ${LtbVlk_SOURCE_FILES}
)
add_library(
  LtbVlk::LtbVlk
  ALIAS
  LtbVlk
)
target_link_libraries(
  LtbVlk
  PUBLIC
  LtbVlk::generate_spirv
  spdlog::spdlog
  Vulkan::Vulkan
  glfw::glfw
)
target_include_directories(
  LtbVlk
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${LTB_VLK_GENERATED_DIR}>
  PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
)
target_compile_features(
  LtbVlk
  PUBLIC
  cxx_std_20
)
set_target_properties(
  LtbVlk
  PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_compile_options(
  LtbVlk
  PUBLIC
  # Strict warnings/errors with gcc and clang
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wextra>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Werror>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wpedantic>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wunused>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-pedantic-errors>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Winit-self>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wold-style-cast>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Woverloaded-virtual>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wsign-conversion>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wshadow>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wmissing-declarations>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wmissing-include-dirs>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-unknown-pragmas>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wformat-truncation>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wimplicit-float-conversion>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wfloat-conversion>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fstrict-aliasing>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wstrict-aliasing>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wcast-align>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wcast-align=strict>
)

# ##############################################################################
# Applications
# ##############################################################################
add_subdirectory(src/ltb/exec)
