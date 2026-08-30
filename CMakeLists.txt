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

if(WIN32 AND METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  message(FATAL_ERROR
      "CUDA backends are supported on Linux only. Use serial or parallel-cpu on Windows.")
endif()

if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  enable_language(CUDA)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Set standard for compiler components
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CUDA_COMPILER)
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

option(METAMATERIAL_DEMO_MODE "Use deterministic fake solver, optimizer, and exporter classes" OFF)

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
  # The Git repository omits generated scalar codelets required by the
  # library. The official release tarball includes them.
  URL            https://www.fftw.org/fftw-3.3.10.tar.gz
  URL_HASH       SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/fftw3-release-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/fftw3-build"
)
FetchContent_MakeAvailable(fftw)

# 7. Fetch NLopt
set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
set(NLOPT_JAVA OFF CACHE BOOL "" FORCE)
set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlopt
  GIT_REPOSITORY https://github.com/stevengj/nlopt.git
  GIT_TAG        v2.11.0
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/nlopt-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/nlopt-build"
)
FetchContent_MakeAvailable(nlopt)

# 8. Fetch ParOpt and its METIS dependency. ParOpt requires a system MPI SDK
# and BLAS/LAPACK implementation; setup.bat installs MS-MPI and oneMKL.
find_package(MPI REQUIRED COMPONENTS CXX)
if(WIN32 AND NOT DEFINED BLA_VENDOR)
  set(BLA_VENDOR Intel10_64lp_seq CACHE STRING "BLAS/LAPACK implementation" FORCE)
endif()
find_package(BLAS REQUIRED)
find_package(LAPACK REQUIRED)

FetchContent_Declare(
  gklib
  GIT_REPOSITORY https://github.com/KarypisLab/GKlib.git
  GIT_TAG        8bd6bad750b2b0d90800c632cf18e8ee93ad72d7
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/gklib-src"
  SOURCE_SUBDIR  _source-only
)
FetchContent_MakeAvailable(gklib)

FetchContent_Declare(
  metis
  GIT_REPOSITORY https://github.com/KarypisLab/METIS.git
  GIT_TAG        v5.2.1
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/metis-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/metis-build"
  SOURCE_SUBDIR  _source-only
)
FetchContent_MakeAvailable(metis)

# METIS 5.2.1's CMake assumes it is the top-level project and that its
# Makefile has already generated build/xinclude/metis.h. Build the two small
# static libraries directly instead of patching fetched dependency sources.
file(GLOB _gklib_sources CONFIGURE_DEPENDS "${gklib_SOURCE_DIR}/*.c")
if(WIN32)
  list(APPEND _gklib_sources "${gklib_SOURCE_DIR}/win32/adapt.c")
endif()
add_library(GKlib STATIC ${_gklib_sources})
target_include_directories(GKlib PUBLIC "${gklib_SOURCE_DIR}")
set_target_properties(GKlib PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_definitions(GKlib PUBLIC
    WIN32
    MSC
    _CRT_SECURE_NO_DEPRECATE
    USE_GKREGEX
    NDEBUG
    NDEBUG2
    "__thread=__declspec(thread)"
  )
endif()
if(UNIX)
  target_link_libraries(GKlib PUBLIC m)
endif()

set(_metis_include_dir "${metis_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${_metis_include_dir}")
file(READ "${metis_SOURCE_DIR}/include/metis.h" _metis_header)
file(WRITE "${_metis_include_dir}/metis.h"
  "#define IDXTYPEWIDTH 32\n#define REALTYPEWIDTH 32\n${_metis_header}")
file(GLOB _metis_sources CONFIGURE_DEPENDS
  "${metis_SOURCE_DIR}/libmetis/*.c"
)
add_library(metis STATIC ${_metis_sources})
target_include_directories(metis
  PUBLIC "${_metis_include_dir}"
  PRIVATE "${metis_SOURCE_DIR}/libmetis"
)
target_link_libraries(metis PUBLIC GKlib)
set_target_properties(metis PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

# ParOpt is source-only upstream, so expose only its C++ core as a static target.
FetchContent_Declare(
  paropt
  GIT_REPOSITORY https://github.com/smdogroup/paropt.git
  GIT_TAG        v2.1.5
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/paropt-src"
  SOURCE_SUBDIR  _source-only
)
FetchContent_MakeAvailable(paropt)

add_library(paropt STATIC
  "${paropt_SOURCE_DIR}/src/ParOptOptions.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptInteriorPoint.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptVec.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptQuasiNewton.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptMMA.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptTrustRegion.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptProblem.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptOptimizer.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptSparseMat.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptCompactEigenvalueApprox.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptAMD.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptSparseCholesky.cpp"
  "${paropt_SOURCE_DIR}/src/ParOptSparseUtils.cpp"
)
add_library(ParOpt::paropt ALIAS paropt)
target_include_directories(paropt PUBLIC
  "${paropt_SOURCE_DIR}/src"
  "${_metis_include_dir}"
)
target_link_libraries(paropt PUBLIC
  MPI::MPI_CXX
  LAPACK::LAPACK
  BLAS::BLAS
  metis
)
if(MSVC)
  target_compile_definitions(paropt PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # ParOptInteriorPoint.cpp gives min2/max2 external linkage while three
  # other translation units define the same inline helpers. Keep the fetched
  # source untouched and give only the strong MSVC definitions local names.
  set_source_files_properties(
    "${paropt_SOURCE_DIR}/src/ParOptInteriorPoint.cpp"
    PROPERTIES COMPILE_DEFINITIONS
      "min2=paropt_interior_min2;max2=paropt_interior_max2"
  )
endif()

# 9. Fetch MFEM's CutFEM dependencies
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

# 10. Fetch MFEM (Finite Element Method Library)
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
# TODO: Add MPI/HPC builds later with hypre/METIS. Keep normal local builds to serial/OpenMP and Linux CUDA.
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
if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu")
  find_package(OpenMP REQUIRED)
endif()
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

set(GLVIS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/glvis"
    CACHE PATH "GLVis source directory")
if(NOT EXISTS "${GLVIS_SOURCE_DIR}/lib/window.cpp")
  message(FATAL_ERROR "GLVis source is missing. Run build.bat deps or ./build.sh deps for ${METAMATERIAL_MFEM_BACKEND} first.")
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
    METAMATERIAL_USE_CUDA=$<BOOL:${MFEM_USE_CUDA}>
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
    nlopt
    ParOpt::paropt
    mfem         # Enforces direct linkage to the compiled MFEM static library
    glvis
)

if(TARGET SDL2::SDL2main)
  target_link_libraries(metamaterial_app PRIVATE SDL2::SDL2main)
endif()

if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu" AND TARGET fftw3_omp)
  target_link_libraries(metamaterial_app PRIVATE fftw3_omp OpenMP::OpenMP_CXX)
endif()

if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  target_link_libraries(metamaterial_app PRIVATE
      CUDA::cudart
      CUDA::cublas
      CUDA::cusparse
      CUDA::cusolver
  )
endif()

add_executable(dispatcher_backend_check
    tests/dispatcher_backend_check.cpp
)
target_include_directories(dispatcher_backend_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(dispatcher_backend_check PRIVATE mfem)

add_executable(paropt_smoke_check
    tests/paropt_smoke_check.cpp
)
target_link_libraries(paropt_smoke_check PRIVATE ParOpt::paropt)

add_executable(solver_adjoint_check
    tests/solver_adjoint_check.cpp
    src/solver.cpp
)
target_include_directories(solver_adjoint_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${fftw_SOURCE_DIR}/api"
)
target_link_libraries(solver_adjoint_check PRIVATE fftw3 mfem)
target_compile_definitions(solver_adjoint_check PRIVATE
    METAMATERIAL_USE_CUDA=$<BOOL:${MFEM_USE_CUDA}>
)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  target_link_libraries(solver_adjoint_check PRIVATE
      CUDA::cudart CUDA::cublas CUDA::cusparse CUDA::cusolver)
endif()
if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu")
  target_link_libraries(solver_adjoint_check PRIVATE OpenMP::OpenMP_CXX)
endif()

add_executable(paper_optimizer_miniapp
    tests/paper_optimizer_miniapp.cpp
    src/optimizer.cpp
    src/solver.cpp
)
target_include_directories(paper_optimizer_miniapp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${fftw_SOURCE_DIR}/api"
)
target_link_libraries(paper_optimizer_miniapp PRIVATE
    Eigen3::Eigen
    ParOpt::paropt
    fftw3
    mfem
)
target_compile_definitions(paper_optimizer_miniapp PRIVATE
    METAMATERIAL_USE_CUDA=$<BOOL:${MFEM_USE_CUDA}>
)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  target_link_libraries(paper_optimizer_miniapp PRIVATE
      CUDA::cudart CUDA::cublas CUDA::cusparse CUDA::cusolver)
endif()
if(METAMATERIAL_MFEM_BACKEND MATCHES "parallel-cpu")
  target_link_libraries(paper_optimizer_miniapp PRIVATE OpenMP::OpenMP_CXX)
endif()
