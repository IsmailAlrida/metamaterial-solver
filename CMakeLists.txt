cmake_minimum_required(VERSION 3.22)

set(_default_mfem_backend "serial")
if(DEFINED ENV{METAMATERIAL_MFEM_BACKEND} AND NOT "$ENV{METAMATERIAL_MFEM_BACKEND}" STREQUAL "")
  set(_default_mfem_backend "$ENV{METAMATERIAL_MFEM_BACKEND}")
endif()
set(METAMATERIAL_MFEM_BACKEND "${_default_mfem_backend}" CACHE STRING "MFEM backend: serial, serial-cuda, parallel-cpu, or parallel-cpu-cuda")
set_property(CACHE METAMATERIAL_MFEM_BACKEND PROPERTY STRINGS serial serial-cuda parallel-cpu parallel-cpu-cuda)
set(_valid_mfem_backends serial serial-cuda parallel-cpu parallel-cpu-cuda)
if(NOT METAMATERIAL_MFEM_BACKEND IN_LIST _valid_mfem_backends)
  message(FATAL_ERROR "METAMATERIAL_MFEM_BACKEND must be one of: serial, serial-cuda, parallel-cpu, parallel-cpu-cuda")
endif()

project(metamaterial_solver LANGUAGES C CXX)

if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  enable_language(CUDA)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Set standard for compiler components
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CUDA_COMPILER)
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

option(METAMATERIAL_DEPS_ONLY "Configure dependencies without creating the app target" OFF)

# ---------------------------------------------------------
# AUTOMATED DEPENDENCY MANAGEMENT
# ---------------------------------------------------------
include(FetchContent)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Keep fetched dependency sources hackable after first checkout" FORCE)
set(METAMATERIAL_DEPS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/deps/src" CACHE PATH "Shared FetchContent dependency source directory")

# 1. Fetch Eigen (Header-only library)
# Note: Ensure the gitlab URL points to the correct official repository path
FetchContent_Declare(
  Eigen3
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        5.0.1
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/eigen3-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/eigen3-build"
)
FetchContent_MakeAvailable(Eigen3)

# 2. Fetch GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/glfw-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/glfw-build"
  OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(glfw)

# 3. Fetch Assimp
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v6.0.5
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/assimp-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/assimp-build"
)
FetchContent_MakeAvailable(assimp)

# 4. Fetch Dear ImGui docking
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.8-docking
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/imgui-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/imgui-build"
)
FetchContent_MakeAvailable(imgui)

# 5. Fetch ImPlot
FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v1.0
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/implot-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/implot-build"
)
FetchContent_MakeAvailable(implot)

# 6. Fetch FFTW
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu")
  set(ENABLE_OPENMP ON CACHE BOOL "" FORCE)
else()
  set(ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
endif()
set(ENABLE_THREADS OFF CACHE BOOL "" FORCE)
set(WITH_COMBINED_THREADS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  fftw
  GIT_REPOSITORY https://github.com/FFTW/fftw3.git
  GIT_TAG        fftw-3.3.10
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/fftw3-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/fftw3-build"
)
FetchContent_MakeAvailable(fftw)

# 7. Fetch MFEM (Finite Element Method Library)
set(MFEM_USE_MPI OFF CACHE BOOL "" FORCE)
set(MFEM_FETCH_TPLS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_METIS OFF CACHE BOOL "" FORCE)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  set(MFEM_USE_CUDA ON CACHE BOOL "" FORCE)
  set(CUDA_ARCH "native" CACHE STRING "" FORCE)
else()
  set(MFEM_USE_CUDA OFF CACHE BOOL "" FORCE)
endif()
if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu")
  set(MFEM_USE_OPENMP ON CACHE BOOL "" FORCE)
else()
  set(MFEM_USE_OPENMP OFF CACHE BOOL "" FORCE)
endif()
# TODO: Add MPI/HPC builds later with hypre/METIS. Keep normal local builds to serial/OpenMP/CUDA.
set(MFEM_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MFEM_ENABLE_MINIAPPS OFF CACHE BOOL "" FORCE)
set(MFEM_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
message(STATUS "MFEM backend: ${METAMATERIAL_MFEM_BACKEND}")

FetchContent_Declare(
  mfem
  GIT_REPOSITORY https://github.com/mfem/mfem.git
  GIT_TAG        v4.9 # Stable, production-ready release tag
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/mfem-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/mfem-build"
)
FetchContent_MakeAvailable(mfem)

if(METAMATERIAL_DEPS_ONLY)
  message(STATUS "Configured dependencies only; skipping metamaterial_app target.")
  return()
endif()

# ---------------------------------------------------------
# IMGUI & OPENGL WORKSPACE
# ---------------------------------------------------------
set(IMGUI_DIR "${imgui_SOURCE_DIR}")

# Find native system OpenGL
find_package(OpenGL REQUIRED)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  find_package(CUDAToolkit REQUIRED)
endif()

# Gather all user source files from your directory
file(GLOB sources "src/*.cpp")
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  file(GLOB cuda_sources "src/*.cu")
  list(APPEND sources ${cuda_sources})
endif()

add_executable(metamaterial_app 
    ${sources} 
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp 
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp 
    ${IMGUI_DIR}/imgui.cpp 
    ${IMGUI_DIR}/imgui_draw.cpp 
    ${IMGUI_DIR}/imgui_demo.cpp 
    ${IMGUI_DIR}/imgui_tables.cpp 
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

# Target-specific paths
target_include_directories(metamaterial_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${implot_SOURCE_DIR}
    "${fftw_SOURCE_DIR}/api"
)

# Link all targets cleanly
# MFEM injects its internal include paths automatically when linked as PRIVATE
target_link_libraries(metamaterial_app PRIVATE 
    glfw 
    OpenGL::GL
    assimp
    Eigen3::Eigen
    fftw3
    mfem         # Enforces direct linkage to the compiled MFEM static library
)

if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu" AND TARGET fftw3_omp)
  target_link_libraries(metamaterial_app PRIVATE fftw3_omp)
endif()

if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  target_link_libraries(metamaterial_app PRIVATE
      CUDA::cudart
      CUDA::cublas
      CUDA::cusparse
      CUDA::cusolver
  )
endif()
