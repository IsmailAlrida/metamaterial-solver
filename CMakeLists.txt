cmake_minimum_required(VERSION 3.25)

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

set(METAMATERIAL_USE_MPI OFF)
if(METAMATERIAL_MFEM_BACKEND MATCHES "^parallel-cpu")
  set(METAMATERIAL_USE_MPI ON)
endif()

project(metamaterial_solver LANGUAGES C CXX)

if(METAMATERIAL_USE_MPI OR UNIX)
  enable_language(Fortran)
endif()

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
option(METAMATERIAL_BUILD_TESTS "Build project checks and diagnostic miniapps" OFF)

# Keep memory-heavy link steps out of each other's way. Compilation parallelism
# is still controlled by build.bat/build.sh.
if(CMAKE_GENERATOR MATCHES "Ninja")
  set_property(GLOBAL PROPERTY JOB_POOLS metamaterial_link_pool=1)
  set(CMAKE_JOB_POOL_LINK metamaterial_link_pool)
endif()

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
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-2.32.10
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/sdl2-src"
  BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/sdl2-build"
)
FetchContent_MakeAvailable(SDL2)

# 3. Fetch Assimp
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
# Assimp carries the ZIP implementation through its 3MF exporter sources.
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3MF_EXPORTER ON CACHE BOOL "" FORCE)
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
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
set(ENABLE_THREADS OFF CACHE BOOL "" FORCE)
set(WITH_COMBINED_THREADS OFF CACHE BOOL "" FORCE)
if(WIN32)
  # The Windows CRT already supplies these functions. Prevent FFTW from
  # finding oneAPI's Unix libm and exporting the nonexistent name m.lib.
  set(LIBM_LIBRARY "" CACHE FILEPATH "Windows CRT supplies libm functions" FORCE)
  unset(fftw3_LIB_DEPENDS CACHE)
endif()
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

# 7. Configure the numerical dependencies used by MFEM and Ipopt.
if(METAMATERIAL_USE_MPI AND WIN32)
  set(_msmpi_sdk "$ENV{SystemDrive}/Program Files (x86)/Microsoft SDKs/MPI")
  if(NOT EXISTS "$ENV{MSMPI_INC}/mpi.h" AND
     EXISTS "${_msmpi_sdk}/Include/mpi.h")
    set(ENV{MSMPI_INC} "${_msmpi_sdk}/Include")
  endif()
  if(NOT EXISTS "$ENV{MSMPI_LIB64}/msmpi.lib" AND
     EXISTS "${_msmpi_sdk}/Lib/x64/msmpi.lib")
    set(ENV{MSMPI_LIB64} "${_msmpi_sdk}/Lib/x64")
  endif()
  unset(_msmpi_sdk)
endif()
if(METAMATERIAL_USE_MPI AND WIN32 AND
   EXISTS "$ENV{ProgramFiles}/Microsoft MPI/Bin/mpiexec.exe")
  set(MPIEXEC_EXECUTABLE
      "$ENV{ProgramFiles}/Microsoft MPI/Bin/mpiexec.exe"
      CACHE FILEPATH "Microsoft MPI launcher" FORCE)
  set(MPI_GUESS_LIBRARY_NAME MSMPI CACHE STRING "Windows MPI implementation" FORCE)
  file(TO_CMAKE_PATH "$ENV{MSMPI_INC}" _msmpi_include)
  set(_msmpi_fortran_includes "${_msmpi_include};${_msmpi_include}/x64")
  if(NOT "${MPI_Fortran_ADDITIONAL_INCLUDE_DIRS}" STREQUAL
         "${_msmpi_fortran_includes}")
    foreach(_probe F77_HEADER F90_MODULE F08_MODULE F77_HEADER_NOKIND)
      unset(MPI_RESULT_Fortran_test_mpi_${_probe} CACHE)
    endforeach()
  endif()
  set(MPI_Fortran_ADDITIONAL_INCLUDE_DIRS "${_msmpi_fortran_includes}"
      CACHE STRING "Microsoft MPI Fortran include directories" FORCE)
  unset(_msmpi_fortran_includes)
  unset(_msmpi_include)
endif()
if(METAMATERIAL_USE_MPI)
  find_package(MPI REQUIRED COMPONENTS C CXX Fortran)
endif()
if(WIN32 AND NOT DEFINED BLA_VENDOR)
  set(BLA_VENDOR Intel10_64lp_seq CACHE STRING "BLAS/LAPACK implementation" FORCE)
endif()
find_package(BLAS REQUIRED)
find_package(LAPACK REQUIRED)

# MUMPS is a normal compiled dependency of the true MPI backend. Linux also
# reuses this source build for Ipopt instead of linking a second MUMPS copy.
if(METAMATERIAL_USE_MPI OR UNIX)
  set(BUILD_SINGLE OFF CACHE BOOL "" FORCE)
  set(BUILD_DOUBLE ON CACHE BOOL "" FORCE)
  set(BUILD_COMPLEX OFF CACHE BOOL "" FORCE)
  set(BUILD_COMPLEX16 OFF CACHE BOOL "" FORCE)
  set(MUMPS_parallel ${METAMATERIAL_USE_MPI} CACHE BOOL "" FORCE)
  # Keep MUMPS on the same sequential oneMKL threading layer selected above.
  # A separate build preset can opt into threaded MKL once that stack is proven.
  set(MUMPS_openmp OFF CACHE BOOL "" FORCE)
  # MUMPS 5.9 supports parallel solves without ScaLAPACK. Keeping it off
  # avoids mixing oneMKL's Intel-MPI BLACS layer with the MS-MPI process.
  set(MUMPS_scalapack OFF CACHE BOOL "" FORCE)
  set(MUMPS_scotch OFF CACHE BOOL "" FORCE)
  set(MUMPS_ptscotch OFF CACHE BOOL "" FORCE)
  set(MUMPS_metis OFF CACHE BOOL "" FORCE)
  set(MUMPS_parmetis OFF CACHE BOOL "" FORCE)
  set(MUMPS_BUILD_TESTING OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    mumps_superbuild
    GIT_REPOSITORY https://github.com/scivision/mumps-superbuild.git
    GIT_TAG        v5.9.1.0
    SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/mumps-superbuild-src"
    BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/mumps-build"
  )
  FetchContent_MakeAvailable(mumps_superbuild)

  if(NOT TARGET MUMPS::MUMPS)
    message(FATAL_ERROR "The pinned MUMPS superbuild did not expose MUMPS::MUMPS.")
  endif()
  if(UNIX)
    # Ipopt links this same static MUMPS build into libipopt.so.
    foreach(_target dmumps mumps_common mumps_common_C mumps_common_Fortran pord)
      set_property(TARGET ${_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
    endforeach()
    if(TARGET mpiseq)
      foreach(_target mpiseq mpiseq_c mpiseq_fortran)
        set_property(TARGET ${_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
      endforeach()
    endif()
  endif()
  # MFEM exports its build-tree target and therefore requires every linked
  # non-imported dependency to belong to a build-tree export as well.
  export(EXPORT MUMPS-targets
      FILE "${CMAKE_BINARY_DIR}/deps/mumps-build/MUMPSBuildTargets.cmake"
      NAMESPACE MUMPS::)
  get_target_property(_mumps_include_dirs
    MUMPS::MUMPS INTERFACE_INCLUDE_DIRECTORIES)
  set(MUMPS_FOUND ON CACHE BOOL "" FORCE)
  # MFEM's FindMUMPS strips dots for numeric preprocessor comparisons.
  set(MUMPS_VERSION 591 CACHE STRING "" FORCE)
  set(MUMPS_INCLUDE_DIRS "${_mumps_include_dirs}" CACHE STRING "" FORCE)
  set(MUMPS_LIBRARIES MUMPS::MUMPS CACHE STRING "" FORCE)
endif()

# Ipopt and IFOPT are fetched and compiled from their official source trees.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/optimizer_dependencies.cmake")

# 8. Fetch MFEM's CutFEM dependencies
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

if(METAMATERIAL_USE_MPI)
  set(HYPRE_ENABLE_MPI ON CACHE BOOL "" FORCE)
  set(HYPRE_ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
  set(HYPRE_ENABLE_FORTRAN OFF CACHE BOOL "" FORCE)
  set(HYPRE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(HYPRE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    hypre
    GIT_REPOSITORY https://github.com/hypre-space/hypre.git
    GIT_TAG        v2.33.0
    SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/hypre-src"
    BINARY_DIR     "${CMAKE_BINARY_DIR}/deps/hypre-build"
    SOURCE_SUBDIR  src
  )
  FetchContent_MakeAvailable(hypre)
  if(NOT TARGET HYPRE)
    message(FATAL_ERROR "The pinned HYPRE source did not expose the HYPRE target.")
  endif()
  if(MSVC)
    # MSVC does not define __STDC_VERSION__ in its default C mode, so HYPRE's
    # legacy PILUT guard misses stdbool.h. Force that standard header into only
    # the two affected translation units and preserve every cached HYPRE object.
    set_source_files_properties(
        "${hypre_SOURCE_DIR}/src/distributed_ls/pilut/trifactor.c"
        "${hypre_SOURCE_DIR}/src/distributed_ls/pilut/parilut.c"
        TARGET_DIRECTORY HYPRE
        PROPERTIES COMPILE_OPTIONS "/FIstdbool.h")
  endif()
  get_target_property(_hypre_include_dirs HYPRE INTERFACE_INCLUDE_DIRECTORIES)
  set(HYPRE_FOUND ON CACHE BOOL "" FORCE)
  set(HYPRE_VERSION 23300 CACHE STRING "" FORCE)
  set(HYPRE_INCLUDE_DIRS "${_hypre_include_dirs}" CACHE STRING "" FORCE)
  set(HYPRE_LIBRARIES HYPRE CACHE STRING "" FORCE)
endif()

# 9. Fetch MFEM (Finite Element Method Library)
set(MFEM_USE_MPI ${METAMATERIAL_USE_MPI} CACHE BOOL "" FORCE)
set(MFEM_FETCH_TPLS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_METIS OFF CACHE BOOL "" FORCE)
set(MFEM_USE_LAPACK OFF CACHE BOOL "" FORCE)
set(MFEM_USE_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(MFEM_USE_MUMPS ${METAMATERIAL_USE_MPI} CACHE BOOL "" FORCE)
set(MFEM_USE_ALGOIM ON CACHE BOOL "" FORCE)
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  set(MFEM_USE_CUDA ON CACHE BOOL "" FORCE)
else()
  set(MFEM_USE_CUDA OFF CACHE BOOL "" FORCE)
endif()
set(MFEM_USE_OPENMP OFF CACHE BOOL "" FORCE)
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
set(_glvis_compile_opts
  $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
)
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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/metamaterial_response.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Metamaterial.m"
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/metamaterial_response.py"
  METAMATERIAL_PYTHON_TEMPLATE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Metamaterial.m"
  METAMATERIAL_MATLAB_TEMPLATE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/export_templates.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/export_templates.hpp"
  @ONLY
)

file(GLOB sources "src/*.cpp")
list(REMOVE_ITEM sources "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  file(GLOB cuda_sources "src/*.cu")
  list(APPEND sources ${cuda_sources})
endif()

add_library(metamaterial_imgui STATIC
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(metamaterial_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(metamaterial_imgui PUBLIC SDL2::SDL2 OpenGL::GL)

add_library(metamaterial_implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(metamaterial_implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(metamaterial_implot PUBLIC metamaterial_imgui)

add_library(metamaterial_core STATIC ${sources})

target_compile_definitions(metamaterial_core PUBLIC
    METAMATERIAL_DEMO_MODE=$<BOOL:${METAMATERIAL_DEMO_MODE}>
    METAMATERIAL_USE_CUDA=$<BOOL:${MFEM_USE_CUDA}>
    METAMATERIAL_USE_MPI=$<BOOL:${METAMATERIAL_USE_MPI}>
)
message(STATUS "Interactive demo mode: ${METAMATERIAL_DEMO_MODE}")

# Target-specific paths
target_include_directories(metamaterial_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${fftw_SOURCE_DIR}/api"
    "${assimp_SOURCE_DIR}/contrib/zip/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# Link all targets cleanly
# MFEM injects its internal include paths automatically when linked as PRIVATE
target_link_libraries(metamaterial_core PUBLIC
    metamaterial_imgui
    metamaterial_implot
    assimp
    Eigen3::Eigen
    fftw3
    Ipopt::ipopt
    mfem         # Enforces direct linkage to the compiled MFEM static library
    glvis
)

add_executable(metamaterial_app src/main.cpp)
target_link_libraries(metamaterial_app PRIVATE metamaterial_core)

if(TARGET SDL2::SDL2main)
  target_link_libraries(metamaterial_app PRIVATE SDL2::SDL2main)
endif()

if(METAMATERIAL_MFEM_BACKEND MATCHES "cuda")
  target_link_libraries(metamaterial_core PUBLIC
      CUDA::cudart
      CUDA::cublas
      CUDA::cusparse
      CUDA::cusolver
  )
endif()

if(METAMATERIAL_BUILD_TESTS)
enable_testing()

add_executable(dispatcher_backend_check
    tests/dispatcher_backend_check.cpp
)
target_include_directories(dispatcher_backend_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(dispatcher_backend_check PRIVATE mfem)
add_test(NAME dispatcher_backend COMMAND dispatcher_backend_check)

add_executable(settings_persistence_check
    tests/settings_persistence_check.cpp
    src/settings.cpp
)
target_include_directories(settings_persistence_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(settings_persistence_check PRIVATE mfem)
add_test(NAME settings_persistence COMMAND settings_persistence_check)

add_executable(exporter_bundle_check
    tests/exporter_bundle_check.cpp
)
target_link_libraries(exporter_bundle_check PRIVATE metamaterial_core)
add_test(NAME exporter_bundle COMMAND exporter_bundle_check)

add_executable(optimizer_test_suite tests/OptimizerTestSuite.cpp)
target_link_libraries(optimizer_test_suite PRIVATE metamaterial_core)
add_test(NAME optimizer_suite COMMAND optimizer_test_suite)

add_executable(solver_test_suite tests/SolverTestSuite.cpp)
target_link_libraries(solver_test_suite PRIVATE metamaterial_core)

add_executable(paper_optimizer_miniapp
    tests/paper_optimizer_miniapp.cpp
)
target_link_libraries(paper_optimizer_miniapp PRIVATE
    metamaterial_core
)
if(METAMATERIAL_USE_MPI)
  set(_solver_rank_reference
      "${CMAKE_CURRENT_BINARY_DIR}/solver-suite-rank-reference.txt")
  add_test(NAME solver_suite_fgmres_1_rank
      COMMAND "${MPIEXEC_EXECUTABLE}" "${MPIEXEC_NUMPROC_FLAG}" 1
              ${MPIEXEC_PREFLAGS} $<TARGET_FILE:solver_test_suite>
              --write-rank-reference "${_solver_rank_reference}"
              ${MPIEXEC_POSTFLAGS})
  set_tests_properties(solver_suite_fgmres_1_rank PROPERTIES
      PROCESSORS 1 FIXTURES_SETUP solver_rank_reference)

  foreach(_solver_case IN ITEMS fgmres_2_rank mumps_1_rank mumps_2_rank)
    if(_solver_case MATCHES "_2_rank$")
      set(_solver_ranks 2)
    else()
      set(_solver_ranks 1)
    endif()
    if(_solver_case MATCHES "^mumps_")
      set(_solver_method --mumps)
    else()
      set(_solver_method)
    endif()
    add_test(NAME solver_suite_${_solver_case}
        COMMAND "${MPIEXEC_EXECUTABLE}" "${MPIEXEC_NUMPROC_FLAG}"
                ${_solver_ranks} ${MPIEXEC_PREFLAGS}
                $<TARGET_FILE:solver_test_suite> ${_solver_method}
                --compare-rank-reference "${_solver_rank_reference}"
                ${MPIEXEC_POSTFLAGS})
    set_tests_properties(solver_suite_${_solver_case} PROPERTIES
        PROCESSORS ${_solver_ranks}
        FIXTURES_REQUIRED solver_rank_reference)
  endforeach()
else()
  add_test(NAME solver_suite COMMAND solver_test_suite)
endif()
endif()

set(_ipopt_runtime_targets metamaterial_app)
if(METAMATERIAL_BUILD_TESTS)
  list(APPEND _ipopt_runtime_targets
    exporter_bundle_check
    optimizer_test_suite
    solver_test_suite
    paper_optimizer_miniapp)
endif()
foreach(_target IN LISTS _ipopt_runtime_targets)
  add_custom_command(TARGET ${_target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Ipopt::ipopt>"
            "$<TARGET_FILE_DIR:${_target}>"
    VERBATIM)
  if(UNIX)
    set_property(TARGET ${_target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    set_property(TARGET ${_target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
  endif()
endforeach()
