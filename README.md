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
| Linux / WSL2 | Yes | Yes | Yes | Yes |

CUDA support is Linux-only for now. Native Windows builds intentionally reject
CUDA backends instead of applying local patches to MFEM.

`parallel-cpu` currently means MFEM/OpenMP inside one process. MPI is used by
ParOpt with `MPI_COMM_SELF`; a distributed `ParMesh`/HYPRE solver is not yet
implemented.

### Windows 11: CPU builds

From the repository root, install the native prerequisites and then open a new
terminal:

```bat
setup.bat
setup.bat check
```

The setup wrapper installs or verifies Visual Studio C++ tools, Git, CMake,
Ninja, [Microsoft MPI](https://www.microsoft.com/en-us/download/details.aspx?id=105289),
and [Intel oneMKL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl-download.html).

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

### Linux / WSL2

Install the ordinary C++/OpenGL/MPI prerequisites. Package names below target
Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git curl pkg-config \
  gfortran openmpi-bin libopenmpi-dev libopenblas-dev liblapack-dev \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev mesa-utils
chmod +x build.sh
```

CPU builds:

```bash
./build.sh serial
./build.sh parallel-cpu
./build.sh serial debug
./build.sh deps serial
```

CUDA builds additionally require a working Linux CUDA toolkit with `nvcc` on
`PATH` and a compatible NVIDIA driver:

```bash
./build.sh serial-cuda
./build.sh parallel-cpu-cuda
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
