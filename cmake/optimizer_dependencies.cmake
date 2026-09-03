include(ExternalProject)

set(METAMATERIAL_DEPENDENCY_JOBS 1 CACHE STRING
  "Parallel jobs used by nested dependency builds")
if(NOT METAMATERIAL_DEPENDENCY_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "METAMATERIAL_DEPENDENCY_JOBS must be a positive integer.")
endif()

FetchContent_Declare(
  ipopt
  GIT_REPOSITORY https://github.com/coin-or/Ipopt.git
  GIT_TAG        releases/3.14.20
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/ipopt-src"
  SOURCE_SUBDIR  _source-only
)
FetchContent_MakeAvailable(ipopt)

FetchContent_Declare(
  ifopt
  GIT_REPOSITORY https://github.com/ethz-adrl/ifopt.git
  GIT_TAG        2.1.4
  SOURCE_DIR     "${METAMATERIAL_DEPS_SOURCE_DIR}/ifopt-src"
  SOURCE_SUBDIR  _source-only
)
FetchContent_MakeAvailable(ifopt)

if(WIN32)
  set(IPOPT_BASH "C:/msys64/usr/bin/bash.exe")
  set(IPOPT_CYGPATH "C:/msys64/usr/bin/cygpath.exe")
  if(NOT EXISTS "${IPOPT_BASH}" OR
     NOT EXISTS "${IPOPT_CYGPATH}" OR
     NOT EXISTS "C:/msys64/usr/bin/make.exe")
    message(FATAL_ERROR "Ipopt requires the MSYS2 tools installed by setup.bat.")
  endif()
else()
  find_program(IPOPT_BASH bash REQUIRED)
endif()

set(_ipopt_build "${CMAKE_BINARY_DIR}/deps/ipopt-build")
set(_ipopt_install "${CMAKE_BINARY_DIR}/deps/ipopt-install")
set(_ifopt_build "${CMAKE_BINARY_DIR}/deps/ifopt-build")
file(MAKE_DIRECTORY
  "${_ipopt_install}/include/coin"
  "${_ifopt_build}/ifopt_core"
  "${_ifopt_build}/ifopt_ipopt"
)

set(_ipopt_tool_source "${ipopt_SOURCE_DIR}")
set(_ipopt_tool_build "${_ipopt_build}")
set(_ipopt_tool_install "${_ipopt_install}")
if(WIN32)
  function(_ipopt_short_path input output)
    set(short_path "${input}")
    if(short_path MATCHES " ")
      file(TO_CMAKE_PATH "$ENV{USERPROFILE}" long_profile)
      execute_process(
        COMMAND "${IPOPT_CYGPATH}" -ws "$ENV{USERPROFILE}"
        OUTPUT_VARIABLE short_profile
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
      )
      file(TO_CMAKE_PATH "${short_profile}" short_profile)
      string(REPLACE "${long_profile}" "${short_profile}"
        short_path "${short_path}")
    endif()
    if(short_path MATCHES " ")
      message(FATAL_ERROR
        "Ipopt's Autotools build requires a path without spaces: ${input}")
    endif()
    set(${output} "${short_path}" PARENT_SCOPE)
  endfunction()
  # Ipopt's Autotools configure rejects whitespace in srcdir, while this
  # repository lives below a Windows user directory containing a space.
  _ipopt_short_path("${ipopt_SOURCE_DIR}" _ipopt_tool_source)
  _ipopt_short_path("${_ipopt_build}" _ipopt_tool_build)
  _ipopt_short_path("${_ipopt_install}" _ipopt_tool_install)
endif()

set(_ipopt_configure
  "${CMAKE_COMMAND}"
  "-DIPOPT_SOURCE_DIR=${_ipopt_tool_source}"
  "-DIPOPT_BINARY_DIR=${_ipopt_tool_build}"
  "-DIPOPT_INSTALL_DIR=${_ipopt_tool_install}"
  "-DIPOPT_BASH=${IPOPT_BASH}"
  "-DIPOPT_DEBUG=$<CONFIG:Debug>"
)

set(_ipopt_dependencies)
if(WIN32)
  list(APPEND _ipopt_configure "-DIPOPT_WINDOWS=ON")
  set(_ipopt_library "${_ipopt_install}/lib/libipopt.lib")
else()
  if(NOT TARGET dmumps OR NOT TARGET mumps_common OR NOT TARGET pord)
    message(FATAL_ERROR "The Linux Ipopt source build requires the pinned MUMPS targets.")
  endif()

  set(_mumps_link_items
    "$<TARGET_FILE:dmumps>"
    "$<TARGET_FILE:mumps_common>"
    "$<TARGET_FILE:pord>"
  )
  set(_ipopt_dependencies dmumps mumps_common pord)
  if(METAMATERIAL_USE_MPI)
    list(APPEND _mumps_link_items
      ${MPI_Fortran_LIBRARIES}
      ${MPI_C_LIBRARIES}
    )
  else()
    list(APPEND _mumps_link_items "$<TARGET_FILE:mpiseq>")
    list(APPEND _ipopt_dependencies mpiseq)
  endif()
  list(APPEND _mumps_link_items
    ${LAPACK_LIBRARIES}
    ${BLAS_LIBRARIES}
  )
  foreach(_directory IN LISTS CMAKE_Fortran_IMPLICIT_LINK_DIRECTORIES)
    list(APPEND _mumps_link_items "-L${_directory}")
  endforeach()
  foreach(_library IN LISTS CMAKE_Fortran_IMPLICIT_LINK_LIBRARIES)
    if(IS_ABSOLUTE "${_library}")
      list(APPEND _mumps_link_items "${_library}")
    else()
      list(APPEND _mumps_link_items "-l${_library}")
    endif()
  endforeach()
  list(APPEND _mumps_link_items -lpthread -lm -ldl)
  list(JOIN _mumps_link_items " " _mumps_lflags)

  set(_mumps_cflags
    "-I${CMAKE_BINARY_DIR}/_deps/mumps_upstream-src/include -I${CMAKE_BINARY_DIR}/_deps/mumps_upstream-src/src")
  list(JOIN LAPACK_LIBRARIES " " _lapack_lflags)
  list(APPEND _ipopt_configure
    "-DIPOPT_WINDOWS=OFF"
    "-DIPOPT_LAPACK_LFLAGS=${_lapack_lflags}"
    "-DIPOPT_MUMPS_CFLAGS=${_mumps_cflags}"
    "-DIPOPT_MUMPS_LFLAGS=${_mumps_lflags}"
  )
  set(_ipopt_library "${_ipopt_install}/lib/libipopt.so")
endif()
list(APPEND _ipopt_configure
  -P "${CMAKE_CURRENT_LIST_DIR}/configure_ipopt.cmake")

ExternalProject_Add(ipopt_external
  SOURCE_DIR "${ipopt_SOURCE_DIR}"
  BINARY_DIR "${_ipopt_build}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND ${_ipopt_configure}
  BUILD_COMMAND
    "${CMAKE_COMMAND}" -E chdir "${_ipopt_tool_build}"
    "${IPOPT_BASH}" -lc "exec make -j${METAMATERIAL_DEPENDENCY_JOBS}"
  INSTALL_COMMAND
    "${CMAKE_COMMAND}" -E chdir "${_ipopt_tool_build}"
    "${IPOPT_BASH}" -lc "exec make install"
  BUILD_BYPRODUCTS "${_ipopt_library}"
  DEPENDS ${_ipopt_dependencies}
)

add_library(ipopt_imported SHARED IMPORTED GLOBAL)
add_library(Ipopt::ipopt ALIAS ipopt_imported)
set_target_properties(ipopt_imported PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_ipopt_install}/include/coin"
)
if(WIN32)
  set_target_properties(ipopt_imported PROPERTIES
    IMPORTED_IMPLIB "${_ipopt_library}"
  )
else()
  set_target_properties(ipopt_imported PROPERTIES
    IMPORTED_LOCATION "${_ipopt_library}"
  )
endif()
add_dependencies(ipopt_imported ipopt_external)

# IFOPT's README documents a normal CMake build against an installed Ipopt.
# Its own build stays isolated so its global RPATH/testing settings do not
# leak into this project.
set(_eigen_package "${CMAKE_BINARY_DIR}/deps/eigen-package")
file(MAKE_DIRECTORY "${_eigen_package}")
file(WRITE "${_eigen_package}/Eigen3Config.cmake"
"if(NOT TARGET Eigen3::Eigen)\n"
"  add_library(Eigen3::Eigen INTERFACE IMPORTED)\n"
"  set_target_properties(Eigen3::Eigen PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"${eigen3_SOURCE_DIR}\")\n"
"endif()\n"
"set(Eigen3_FOUND TRUE)\n")

ExternalProject_Add(ifopt_external
  SOURCE_DIR "${ifopt_SOURCE_DIR}"
  BINARY_DIR "${_ifopt_build}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env "IPOPT_DIR=${_ipopt_install}"
    "${CMAKE_COMMAND}"
      -S "${ifopt_SOURCE_DIR}"
      -B "${_ifopt_build}"
      -G "${CMAKE_GENERATOR}"
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_PREFIX_PATH=${_eigen_package}"
      -DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_IPOPT=ON
      -DBUILD_SNOPT=OFF
  BUILD_COMMAND
    "${CMAKE_COMMAND}" --build "${_ifopt_build}"
      --config "$<CONFIG>" --target ifopt_ipopt
      --parallel "${METAMATERIAL_DEPENDENCY_JOBS}"
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    "${_ifopt_build}/ifopt_core/${CMAKE_STATIC_LIBRARY_PREFIX}ifopt_core${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${_ifopt_build}/ifopt_ipopt/${CMAKE_STATIC_LIBRARY_PREFIX}ifopt_ipopt${CMAKE_STATIC_LIBRARY_SUFFIX}"
  DEPENDS ipopt_external
)

add_library(ifopt_core_imported STATIC IMPORTED GLOBAL)
add_library(ifopt::ifopt_core ALIAS ifopt_core_imported)
set_target_properties(ifopt_core_imported PROPERTIES
  IMPORTED_LOCATION
    "${_ifopt_build}/ifopt_core/${CMAKE_STATIC_LIBRARY_PREFIX}ifopt_core${CMAKE_STATIC_LIBRARY_SUFFIX}"
  INTERFACE_INCLUDE_DIRECTORIES "${ifopt_SOURCE_DIR}/ifopt_core/include"
  INTERFACE_LINK_LIBRARIES Eigen3::Eigen
)
add_dependencies(ifopt_core_imported ifopt_external)

add_library(ifopt_ipopt_imported STATIC IMPORTED GLOBAL)
add_library(ifopt::ifopt_ipopt ALIAS ifopt_ipopt_imported)
set_target_properties(ifopt_ipopt_imported PROPERTIES
  IMPORTED_LOCATION
    "${_ifopt_build}/ifopt_ipopt/${CMAKE_STATIC_LIBRARY_PREFIX}ifopt_ipopt${CMAKE_STATIC_LIBRARY_SUFFIX}"
  INTERFACE_INCLUDE_DIRECTORIES "${ifopt_SOURCE_DIR}/ifopt_ipopt/include"
  INTERFACE_LINK_LIBRARIES "ifopt::ifopt_core;Ipopt::ipopt"
)
add_dependencies(ifopt_ipopt_imported ifopt_external)

add_custom_target(optimizer_dependencies
  DEPENDS ifopt_external)
