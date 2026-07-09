cmake_minimum_required(VERSION 3.22)
project(metamaterial_solver LANGUAGES C CXX CUDA)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Set standard for both C++ and CUDA compiler components
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

option(METAMATERIAL_DEPS_ONLY "Configure dependencies without creating the app target" OFF)

# ---------------------------------------------------------
# AUTOMATED DEPENDENCY MANAGEMENT (Eigen, GLFW, & MFEM)
# ---------------------------------------------------------
include(FetchContent)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(_default_mfem_backend "serial")
if(DEFINED ENV{METAMATERIAL_MFEM_BACKEND} AND NOT "$ENV{METAMATERIAL_MFEM_BACKEND}" STREQUAL "")
  set(_default_mfem_backend "$ENV{METAMATERIAL_MFEM_BACKEND}")
endif()
set(METAMATERIAL_MFEM_BACKEND "${_default_mfem_backend}" CACHE STRING "MFEM backend: serial, mpi, cuda, or mpi-cuda")
set_property(CACHE METAMATERIAL_MFEM_BACKEND PROPERTY STRINGS serial mpi cuda mpi-cuda)
set(_valid_mfem_backends serial mpi cuda mpi-cuda)
if(NOT METAMATERIAL_MFEM_BACKEND IN_LIST _valid_mfem_backends)
  message(FATAL_ERROR "METAMATERIAL_MFEM_BACKEND must be one of: serial, mpi, cuda, mpi-cuda")
endif()

# 1. Fetch Eigen (Header-only library)
# Note: Ensure the gitlab URL points to the correct official repository path
FetchContent_Declare(
  Eigen3
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        5.0.1
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
)
FetchContent_MakeAvailable(assimp)

# 4. Fetch Dear ImGui docking
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.8-docking
)
FetchContent_MakeAvailable(imgui)

# 5. Fetch ImPlot
FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v1.0
)
FetchContent_MakeAvailable(implot)

# 6. Fetch MFEM (Finite Element Method Library)
if(METAMATERIAL_MFEM_BACKEND MATCHES "mpi")
  set(MFEM_USE_MPI ON CACHE BOOL "" FORCE)
else()
  set(MFEM_USE_MPI OFF CACHE BOOL "" FORCE)
endif()
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  set(MFEM_USE_CUDA ON CACHE BOOL "" FORCE)
  set(CUDA_ARCH "native" CACHE STRING "" FORCE)
else()
  set(MFEM_USE_CUDA OFF CACHE BOOL "" FORCE)
endif()
set(MFEM_USE_METIS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_OPENMP OFF CACHE BOOL "" FORCE) # Turn ON if you want quick multi-threaded CPU assembly
set(MFEM_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MFEM_ENABLE_MINIAPPS OFF CACHE BOOL "" FORCE)
set(MFEM_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
message(STATUS "MFEM backend: ${METAMATERIAL_MFEM_BACKEND}")

FetchContent_Declare(
  mfem
  GIT_REPOSITORY https://github.com/mfem/mfem.git
  GIT_TAG        v4.9 # Stable, production-ready release tag
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
find_package(CUDAToolkit REQUIRED)

# Gather all user source files from your directory
file(GLOB sources "src/*.cpp" "src/*.cu")

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
)

# Link all targets cleanly
# MFEM injects its internal include paths automatically when linked as PRIVATE
target_link_libraries(metamaterial_app PRIVATE 
    glfw 
    OpenGL::GL
    assimp
    Eigen3::Eigen
    mfem         # Enforces direct linkage to the compiled MFEM static library
    CUDA::cudart
    CUDA::cublas
    CUDA::cusparse
    CUDA::cusolver
)
