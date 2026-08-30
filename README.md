# Metamaterial Solver

First, you input your input modal (A signal source, noise source)

Thenn you also input your desired frequency response of the material

which shows you the output signal as a plot

CUDA acceleration is available in Linux builds. Windows builds use the CPU backends.

And let's say you can also select the medium properties as well,

and a menu for customizing the material property so each voxel has a material property

We will have one 2D voxel face as the start of the sound source with the sound source at the center

then the rectangular 3D unit cell of metamaterial

periodic boundary conditions

then a 2D surface with the virtual microphone exactly at the center

optimization happens on an error reduction

we try to reduce the error from the desired frequency response of the material and the actual frequency response collected at the centerpoint of the 2D face on the other side

the error will guide our topology optimizer 

Our topology optimizer will then update the topology to be a continous, periodic shape for the unit cell


## Preliminary Candidates for the wave equation solvers

- Helmholtz single frequency with GMRES
- FDTD with staircase effects taken into consideration for realism
- Shifted krylov shared subspace with indiviudal GMRES thread solvers in multiple parallel branches
- what else?

## Candidates for topology optimization

- SIMP

I dont know what else. 

Constraints: Periodic unit cell, solid geometry must be continous starting from the unit cell surface; no floating solid islands

## Notes

CUDA development targets Linux and WSL2. Native Windows builds intentionally remain CPU-only.

## Building

| Platform | `serial` | `parallel-cpu` | `serial-cuda` | `parallel-cpu-cuda` |
|---|---:|---:|---:|---:|
| Windows 11 | Yes | Yes | No | No |
| Linux / WSL2 | Yes | Yes | Yes | Deferred |

CUDA support is Linux-only for now. Native Windows builds intentionally reject
CUDA backends instead of applying local patches to MFEM.

`parallel-cpu` builds the MPI `ParMesh`/HYPRE path with OpenMP inside each rank.
The runtime solver can use either iterative FGMRES or direct MUMPS.

### Windows 11: CPU builds

From the repository root, install the native prerequisites and then open a new
terminal:

```bat
setup.bat
setup.bat check
```

The setup wrapper installs or verifies Visual Studio C++ tools, Git, CMake,
Ninja, [Microsoft MPI](https://www.microsoft.com/en-us/download/details.aspx?id=105289),
[Intel oneMKL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl-download.html),
and the Intel Fortran Compiler. It accepts Visual Studio's bundled CMake and
Ninja instead of downloading duplicate copies. The parallel build checks the
exact oneMKL ScaLAPACK and MS-MPI BLACS libraries required by MUMPS.

If WinGet cannot install the Intel components, install the
[Intel Fortran Compiler](https://www.intel.com/content/www/us/en/developer/tools/oneapi/fortran-compiler-download.html)
and [oneMKL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl-download.html)
from Intel, or use the complete
[oneAPI Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/oneapi-toolkit-download.html).
Keep the ScaLAPACK and BLACS components selected, then rerun `setup.bat check`.

Build an optimized Release configuration:

```bat
build.bat serial
build.bat parallel-cpu
```

Debug is opt-in and uses a separate build directory:

```bat
build.bat serial debug
build.bat parallel-cpu debug
```

Configure dependencies and write `compile_commands.json` for the LSP without
building the application:

```bat
build.bat deps serial
build.bat deps parallel-cpu
```

Launch the parallel app through MPI. Only rank zero creates the SDL/ImGui
window; the other ranks remain solver workers:

```bat
set OMP_NUM_THREADS=4
mpiexec -n 2 build\parallel-cpu\metamaterial_app.exe
```

Keep `MPI ranks x OMP_NUM_THREADS` at or below the machine's logical processor
count. Prove the parallel linear algebra and forward Solver before running the
optimizer:

```bat
mpiexec -n 2 build\parallel-cpu\mumps_smoke_check.exe
mpiexec -n 1 build\parallel-cpu\parallel_solver_check.exe
mpiexec -n 2 build\parallel-cpu\parallel_solver_check.exe
mpiexec -n 4 build\parallel-cpu\parallel_solver_check.exe
```

After building, the test wrapper runs the registered serial and/or MPI checks
without configuring or compiling anything and merges the results into
`test-results/tests.json`:

```bat
test.bat serial
test.bat parallel-cpu
test.bat all
test.bat parallel-cpu debug
```

The optimizer miniapp uses FGMRES by default and accepts `--mumps` for the
direct-solver comparison:

```bat
mpiexec -n 2 build\parallel-cpu\paper_optimizer_miniapp.exe
mpiexec -n 2 build\parallel-cpu\paper_optimizer_miniapp.exe --mumps
```

### Linux / WSL2

Install the C++, Fortran, OpenGL, OpenMPI, BLAS/LAPACK, and ScaLAPACK
prerequisites. `setup.sh` is idempotent and currently targets Ubuntu/Debian:

```bash
chmod +x setup.sh build.sh
./setup.sh
./setup.sh check
```

CPU builds:

```bash
./build.sh serial
./build.sh parallel-cpu
./build.sh serial debug
./build.sh deps serial
```

Run the MPI backend with the same rank/worker model:

```bash
OMP_NUM_THREADS=4 mpiexec -n 2 build/linux/parallel-cpu/metamaterial_app
mpiexec -n 2 build/linux/parallel-cpu/mumps_smoke_check
mpiexec -n 1 build/linux/parallel-cpu/parallel_solver_check
mpiexec -n 2 build/linux/parallel-cpu/parallel_solver_check
mpiexec -n 4 build/linux/parallel-cpu/parallel_solver_check
./test.sh parallel-cpu
./test.sh all
```

CUDA builds additionally require a working Linux CUDA toolkit with `nvcc` on
`PATH` and a compatible NVIDIA driver:

```bash
./build.sh serial-cuda
```

An optional CUDA architecture can be supplied after the backend; Blackwell is
`120`:

```bash
./build.sh serial-cuda 120
```

For WSL2, install the Windows NVIDIA driver and follow NVIDIA's
[CUDA on WSL guide](https://docs.nvidia.com/cuda/wsl-user-guide/index.html);
do not install a separate Linux display driver inside WSL.

Windows build directories live under `build/<backend>`. Linux and WSL2 build
directories live under `build/linux/<backend>`. Both platforms reuse dependency
sources under `build/deps/src` and the GLVis checkout under `extern/glvis`;
compiled objects remain platform-specific inside their respective build directories.
