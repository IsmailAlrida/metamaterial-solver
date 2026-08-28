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

option(METAMATERIAL_DEMO_MODE "Use deterministic fake solver, optimizer, exporter, and executor classes" ON)

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

# 2. Fetch SDL2
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-2.32.10
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/sdl2-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/sdl2-build"
)
FetchContent_MakeAvailable(SDL2)

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

# 7. Fetch MFEM's CutFEM dependencies
FetchContent_Declare(
  blitz
  GIT_REPOSITORY https://github.com/blitzpp/blitz.git
  GIT_TAG        f24a250a43dff88c31ad92916da828b7ea9a98b7
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/blitz-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/blitz-build"
  SOURCE_SUBDIR  cmake # Populate only; Blitz's top-level project is not subproject-safe.
)
FetchContent_MakeAvailable(blitz)

set(_blitz_configure_args
  -S "${blitz_SOURCE_DIR}"
  -B "${blitz_BINARY_DIR}"
  -G "${CMAKE_GENERATOR}"
  "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -DBUILD_TESTING=OFF
  -DBUILD_DOC=OFF
)
if(CMAKE_GENERATOR_PLATFORM)
  list(APPEND _blitz_configure_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
  list(APPEND _blitz_configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" ${_blitz_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)

FetchContent_Declare(
  algoim
  GIT_REPOSITORY https://github.com/algoim/algoim.git
  GIT_TAG        9c9ca0ef094d8ab0390ed36367a1151b459bbe0a
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/algoim-src"
)
FetchContent_MakeAvailable(algoim)

# MFEM's find modules otherwise look only for installed copies.
set(BLITZ_FOUND ON CACHE BOOL "" FORCE)
set(BLITZ_INCLUDE_DIRS "${blitz_BINARY_DIR};${blitz_SOURCE_DIR}" CACHE STRING "" FORCE)
set(BLITZ_LIBRARIES "" CACHE STRING "" FORCE)
set(ALGOIM_DIR "${algoim_SOURCE_DIR}" CACHE PATH "" FORCE)

# 8. Fetch MFEM (Finite Element Method Library)
set(MFEM_USE_MPI OFF CACHE BOOL "" FORCE)
set(MFEM_FETCH_TPLS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_METIS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_LAPACK OFF CACHE BOOL "" FORCE)
set(MFEM_USE_ALGOIM ON CACHE BOOL "" FORCE)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  set(MFEM_USE_CUDA ON CACHE BOOL "" FORCE)
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
set(CMAKE_SKIP_INSTALL_RULES ON)
FetchContent_MakeAvailable(mfem)
set(CMAKE_SKIP_INSTALL_RULES OFF)
find_package(OpenGL REQUIRED)

# GLVis is a library internally, but its standalone build expects system copies
# of these three rendering dependencies. Keep them in the shared dependency tree
# with the rest of the application instead.
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  glew
  GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
  GIT_TAG        glew-cmake-2.2.0
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/glew-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/glew-build"
)
FetchContent_MakeAvailable(glew)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/glm-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/glm-build"
)
FetchContent_MakeAvailable(glm)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  freetype
  GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
  GIT_TAG        VER-2-13-3
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/freetype-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/freetype-build"
)
FetchContent_MakeAvailable(freetype)

set(GLVIS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/glvis")
if(NOT EXISTS "${GLVIS_SOURCE_DIR}/lib/window.cpp")
  message(FATAL_ERROR "GLVis source is missing. Run build.bat deps ${METAMATERIAL_MFEM_BACKEND} first.")
endif()

set(_glvis_compile_defs
  GLVIS_MULTISAMPLE=4
  GLVIS_MS_LINEWIDTH=1.4
  GLVIS_FONT_SIZE=12
  GLEW_NO_GLU
  GLEW_STATIC
  $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
)
set(_glvis_compile_opts)
set(_glvis_include_dirs
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${GLVIS_SOURCE_DIR}/lib"
)
set(_glvis_libraries
  SDL2::SDL2
  OpenGL::GL
  libglew_static
  glm::glm
  freetype
  mfem
)
add_subdirectory(
  "${GLVIS_SOURCE_DIR}/lib"
  "${CMAKE_BINARY_DIR}/deps/glvis-build"
)

# ---------------------------------------------------------
# IMGUI & OPENGL WORKSPACE
# ---------------------------------------------------------
set(IMGUI_DIR "${imgui_SOURCE_DIR}")

# Find native system OpenGL
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
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp 
    ${IMGUI_DIR}/imgui.cpp 
    ${IMGUI_DIR}/imgui_draw.cpp 
    ${IMGUI_DIR}/imgui_demo.cpp 
    ${IMGUI_DIR}/imgui_tables.cpp 
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

target_compile_definitions(metamaterial_app PRIVATE
    METAMATERIAL_DEMO_MODE=$<BOOL:${METAMATERIAL_DEMO_MODE}>
)
message(STATUS "Interactive demo mode: ${METAMATERIAL_DEMO_MODE}")

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
    SDL2::SDL2
    OpenGL::GL
    assimp
    Eigen3::Eigen
    fftw3
    mfem         # Enforces direct linkage to the compiled MFEM static library
    glvis
)

if(TARGET SDL2::SDL2main)
  target_link_libraries(metamaterial_app PRIVATE SDL2::SDL2main)
endif()

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
