cmake_minimum_required(VERSION 3.16)

project(drawstuff-modern
  VERSION 0.1.0
  LANGUAGES C CXX
)

# ---- C++17 is required ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Options ----
option(DRAWSTUFF_MODERN_BUILD_SHARED "Build shared library instead of static" OFF)

set(_LIB_TYPE STATIC)
if(DRAWSTUFF_MODERN_BUILD_SHARED)
  set(_LIB_TYPE SHARED)
endif()

# ---- glad (bundled as OBJECT library) ----
add_library(glad_obj OBJECT
  external/glad/src/glad.c
)

target_include_directories(glad_obj
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

# ---- drawstuff-modern library ----
add_library(drawstuff-modern ${_LIB_TYPE}
  src/drawstuff_core.cpp
  src/mesh_utils.cpp
  src/primitive_meshes.cpp
  src/platform_x11_glx.cpp
  src/shader_programs.cpp
  src/drawstuffCompat.cpp
  $<TARGET_OBJECTS:glad_obj>
)

set_target_properties(drawstuff-modern PROPERTIES
  OUTPUT_NAME "drawstuff-modern"
)

# Ensure consumers also build with C++17
target_compile_features(drawstuff-modern PUBLIC cxx_std_17)

target_include_directories(drawstuff-modern
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

# ---- Dependencies ----
find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

target_link_libraries(drawstuff-modern
  PUBLIC
    OpenGL::GL
    X11::X11
)

# ---- Warnings (optional) ----
if (MSVC)
  target_compile_options(drawstuff-modern PRIVATE /W4)
else()
  target_compile_options(drawstuff-modern PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Install (optional) ----
include(GNUInstallDirs)

install(TARGETS drawstuff-modern
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

# glad headers (vendored, optional to install)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/drawstuff-modern/external/glad
  FILES_MATCHING
    PATTERN "*.h"
)

# ---- Build demos ----
option(DRAWSTUFF_MODERN_BUILD_DEMOS "Build demo programs" ON)

if (DRAWSTUFF_MODERN_BUILD_DEMOS)
  add_subdirectory(demo)
endif()
