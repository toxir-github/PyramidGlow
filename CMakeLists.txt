cmake_minimum_required(VERSION 3.16)
project(PyramidGlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Path to a checkout of https://github.com/AcademySoftwareFoundation/openfx
set(OFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../openfx" CACHE PATH "OpenFX SDK checkout")

file(GLOB OFX_SUPPORT_SRC "${OFX_ROOT}/Support/Library/*.cpp")

find_package(OpenMP)

# --- standalone algorithm test (no host needed) ---
add_executable(test_core test_core.cpp)
if(OpenMP_CXX_FOUND)
  target_link_libraries(test_core PRIVATE OpenMP::OpenMP_CXX)
endif()

# --- the OFX plugin ---
add_library(PyramidGlow MODULE PyramidGlow.cpp ${OFX_SUPPORT_SRC})
target_include_directories(PyramidGlow PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OFX_ROOT}/include
  ${OFX_ROOT}/Support/include)
if(OpenMP_CXX_FOUND)
  target_link_libraries(PyramidGlow PRIVATE OpenMP::OpenMP_CXX)
endif()
set_target_properties(PyramidGlow PROPERTIES PREFIX "" SUFFIX ".ofx"
  CXX_VISIBILITY_PRESET hidden)

# Bundle layout: PyramidGlow.ofx.bundle/Contents/<Arch>/PyramidGlow.ofx
if(WIN32)
  set(OFX_ARCH "Win64")
elseif(APPLE)
  set(OFX_ARCH "MacOS")
else()
  set(OFX_ARCH "Linux-x86-64")
endif()
set(BUNDLE "${CMAKE_BINARY_DIR}/PyramidGlow.ofx.bundle/Contents/${OFX_ARCH}")
add_custom_command(TARGET PyramidGlow POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:PyramidGlow>" "${BUNDLE}/PyramidGlow.ofx")
