# Metamaterial Solver

First, you input your input modal (A signal source, noise source)

Thenn you also input your desired frequency response of the material

which shows you the output signal as a plot

And you can select whether to run in CUDA or not (checkbox is gated behind auto CUDA support check)

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

Main author has an RTX 5060 GPU and thought it's a shame not to use it. CUDA is certainly an option here so we make use of the hardware as much as possible

## Building

Use the Visual Studio 2022 x64 toolchain from the repository root:

```bat
build.bat
```

That defaults to the serial MFEM backend and writes build files to:

```text
build/serial
```

Select a different MFEM backend by passing one argument:

```bat
build.bat serial
build.bat serial-cuda
build.bat parallel-cpu
build.bat parallel-cpu-cuda
```

CUDA presets target the GPU detected during configuration. Override the target
with CMake's standard architecture switch when building for another GPU:

```bat
cmake --preset serial-cuda -DCMAKE_CUDA_ARCHITECTURES=89
```

For example: `86` targets Ampere, `89` targets Ada, and `120` targets Blackwell.

Each backend gets its own build directory:

```text
build/serial
build/serial-cuda
build/parallel-cpu
build/parallel-cpu-cuda
```

Dependency source checkouts are shared and hackable under:

```text
build/deps/src
```

Examples:

```text
build/deps/src/fftw3-src
build/deps/src/mfem-src
build/deps/src/imgui-src
```

Configure dependencies only:

```bat
build.bat deps serial
build.bat deps serial-cuda
build.bat deps parallel-cpu
build.bat deps parallel-cpu-cuda
```

You can also configure CMake directly:

```bat
cmake -S . -B build/parallel-cpu-cuda -G "Visual Studio 17 2022" -A x64 -DMETAMATERIAL_MFEM_BACKEND=parallel-cpu-cuda -DMETAMATERIAL_DEPS_ONLY=OFF
cmake --build build/parallel-cpu-cuda --config Debug
```

Valid `METAMATERIAL_MFEM_BACKEND` values are:

```text
serial
serial-cuda
parallel-cpu
parallel-cpu-cuda
```

Backend meaning:

```text
serial              CPU, one host thread
serial-cuda         CPU host flow plus CUDA device backend
parallel-cpu        OpenMP CPU backend
parallel-cpu-cuda   OpenMP CPU backend plus CUDA device backend
```

For GLVis, build the matching MFEM backend first, then run:

```bat
scripts\install_glvis.bat serial
```

or replace `serial` with another backend. Native Windows GLVis builds need `vcpkg`; set `VCPKG_ROOT` if `vcpkg.exe` is not on `PATH`.
