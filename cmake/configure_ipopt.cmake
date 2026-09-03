if(NOT DEFINED IPOPT_SOURCE_DIR OR
   NOT DEFINED IPOPT_BINARY_DIR OR
   NOT DEFINED IPOPT_INSTALL_DIR)
  message(FATAL_ERROR "Ipopt source, binary, and install directories are required.")
endif()

file(MAKE_DIRECTORY "${IPOPT_BINARY_DIR}")

set(_configure
  "${IPOPT_SOURCE_DIR}/configure"
  "--prefix=${IPOPT_INSTALL_DIR}"
  "--includedir=${IPOPT_INSTALL_DIR}/include/coin"
  --without-asl
  --disable-java
  --disable-sipopt
  --disable-static
  --enable-shared
  --disable-mpiinit
  --disable-dependency-tracking
  --with-precision=double
  --with-intsize=32
)
set(_ipopt_binary_directory "${IPOPT_BINARY_DIR}")

if(IPOPT_WINDOWS)
  if(NOT DEFINED IPOPT_CYGPATH)
    message(FATAL_ERROR "The MSYS2 cygpath executable is required on Windows.")
  endif()
  execute_process(
    COMMAND "${IPOPT_CYGPATH}" -u "${IPOPT_SOURCE_DIR}/configure"
    OUTPUT_VARIABLE _ipopt_configure_script
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  execute_process(
    COMMAND "${IPOPT_CYGPATH}" -u "${IPOPT_INSTALL_DIR}"
    OUTPUT_VARIABLE _ipopt_install_prefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  execute_process(
    COMMAND "${IPOPT_CYGPATH}" -u "${IPOPT_BINARY_DIR}"
    OUTPUT_VARIABLE _ipopt_binary_directory
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  list(POP_FRONT _configure)
  list(POP_FRONT _configure)
  list(POP_FRONT _configure)
  list(PREPEND _configure
    "${_ipopt_configure_script}"
    "--prefix=${_ipopt_install_prefix}"
    "--includedir=${_ipopt_install_prefix}/include/coin"
  )
  set(ENV{MSYS2_PATH_TYPE} inherit)
  set(ENV{CC} cl)
  set(ENV{CXX} cl)
  list(APPEND _configure
    --enable-msvc
    --disable-f77
    --enable-pardisomkl
    "--with-lapack-lflags=-lmkl_rt"
  )
else()
  list(APPEND _configure
    "--with-lapack-lflags=${IPOPT_LAPACK_LFLAGS}"
    "--with-mumps-cflags=${IPOPT_MUMPS_CFLAGS}"
    "--with-mumps-lflags=${IPOPT_MUMPS_LFLAGS}"
  )
endif()

if(IPOPT_DEBUG)
  list(APPEND _configure --enable-debug)
endif()

if(IPOPT_WINDOWS)
  list(JOIN _configure " " _configure_command)
  execute_process(
    COMMAND "${IPOPT_BASH}" -c
      "export PATH=/usr/bin:$PATH && cd ${_ipopt_binary_directory} && exec ${_configure_command}"
    COMMAND_ERROR_IS_FATAL ANY
  )
else()
  execute_process(
    COMMAND "${IPOPT_BASH}" -lc "exec \"$@\"" _ ${_configure}
    WORKING_DIRECTORY "${IPOPT_BINARY_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()
