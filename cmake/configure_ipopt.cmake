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

if(IPOPT_WINDOWS)
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

execute_process(
  COMMAND "${IPOPT_BASH}" -lc "exec \"$@\"" _ ${_configure}
  WORKING_DIRECTORY "${IPOPT_BINARY_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
