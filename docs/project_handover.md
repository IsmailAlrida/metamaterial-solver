# Metamaterial Solver Project Handover

Snapshot date: 2026-09-06  
Branch: `paropt-optimizer`  
HEAD: `183bba4ae63babb65430c4784d2a31e9de7324a4`  
HEAD subject: `Funny attempt at overriding ParOpt classes`

## Immediate status

The project contains a substantial serial and MPI MFEM implementation, but the
current checkout is **not buildable as checked out**. It is halfway through a
source-directory reorganization:

- The old tracked files under `src/` are deleted in the working tree.
- Their replacements under `src/solver/`, `src/optimizer/`, `src/types/`,
  `src/renderer/`, `src/dispatcher/`, and `src/exporter/` are untracked.
- CMake still discovers only `src/*.cpp` and still names old flat paths.
- `tests/OptimizerTestSuite.cpp` is absent from the working tree while CMake
  still requires it. An older staged version remains recoverable from Git's
  index, but it is not the approved replacement suite.
- `src/optimizer/AageMMA.cpp` and `.hpp` are unfinished experiments and must
  not be compiled in their present form.

Do not commit, reset, clean, or broadly stage the tree until the move and the
index/worktree split have been reconciled. In particular, `git clean` would
delete the new source layout.

## What the application is intended to be

The application is a 2D transient vibroacoustic topology-optimization tool:

1. The user describes a duct, materials, time integration, and pass/stop or
   freeform frequency objectives.
2. The MFEM solver performs one broadband white-noise forward analysis.
3. The outlet signal is compared with an empty-duct reference through FFTW.
4. The solver differentiates the selected spectral objectives using a fully
   discrete Newmark adjoint and cut-element sensitivities.
5. ParOpt MMA updates the design-region level-set variables.
6. The latest geometry is streamed to the embedded GLVis panel.
7. Completed run data can be exported as JSON and a ZIP bundle.

The target baseline is the 2D duct problem described in
[`golden-paper.md`](golden-paper.md). Real-world cabin-wall predictions, 3D
optimization, Bloch-periodic unit cells, and printable-mesh generation are not
complete milestones.

## Sacred ownership and execution model

`main.cpp` owns the long-lived application data:

- `AppSettings`
- `SolverResult`
- `LevelSet`
- one selected `Solver`

The solver, optimizer, exporter, dispatcher, and renderer receive references
to that data. The level set is the shared design object and is expected to
outlive both the solver and optimizer. It is updated between optimizer
iterations and postprocessed by the filter; it is not recreated for every
solve.

The solver lifecycle is deliberately sequential:

```text
setMesh() -> assembleSolutionSpace() -> solve()
```

This order must not be converted into a CPU pipeline. Parallelism belongs
inside the MFEM operations and linear solvers.

For `parallel-cpu`:

- Every MPI rank owns one handle to the collective solver.
- Rank zero owns the UI, dispatcher, exporter, and ParOpt driver.
- Nonzero ranks remain in `Solver::parallelWorkerLoop()`.
- ParOpt itself runs on rank zero with `MPI_COMM_SELF`.
- A ParOpt callback commands a collective distributed forward or adjoint
  operation across the solver ranks.
- Rank zero currently executes the optimization workflow in one
  `std::async` worker so the ImGui frame loop remains responsive.

The MPI runtime is requested with `MPI_THREAD_SERIALIZED`. This is acceptable
only while rank zero has one MPI-calling worker at a time and the UI thread
does not independently enter MPI.

Cancellation is cooperative. It sets an atomic request, but it does not
interrupt a Newmark transient, factorization, or adjoint halfway through. The
request is observed at a coherent callback or iteration boundary.

## Current source map

The new, not-yet-integrated layout is:

| Area | Files | Responsibility |
|---|---|---|
| Entry point | `src/main.cpp` | MPI/device startup, top-level lifetimes, rank-zero UI |
| Shared data | `src/types/` | Settings, persisted app configuration, level set, coefficients, results |
| Solver | `src/solver/` | Serial/MPI assembly, Newmark solve, FFT, adjoint, cut sensitivity |
| Optimizer | `src/optimizer/` | Objective construction and ParOpt MMA callbacks |
| Dispatcher | `src/dispatcher/` | One asynchronous run/export operation and cooperative cancellation |
| Renderer | `src/renderer/` | ImGui/ImPlot UI, thread-safe log, embedded GLVis panel |
| Exporter | `src/exporter/` | Run-data JSON/ZIP export and future manufacturing path |
| Demo backend | `src/demo/` | Fake solver/optimizer/exporter selected at compile time |
| Tests | `tests/` | CTest executables, miniapp, report generator, lightweight Python math checks |

The old flat files are still the tracked paths in `HEAD`; the directories
above are currently untracked working-tree replacements.

## Forward solver: implemented state

Both serial and parallel sources contain the following machinery:

- Structured 2D duct mesh with inlet, design, and outlet regions.
- A persistent design-region level set and the paper's filtered cosine initial
  design.
- MFEM bilinear/mixed forms for the coupled displacement-pressure system.
- Algoim cut-volume rules through `ImplicitDomainIntegrator` for the solid and
  acoustic non-coupling blocks.
- `ImplicitSurfaceNormalIntegrator` for pressure-displacement coupling on
  `phi = 0`.
- Fixed-air inlet and outlet regions.
- Structural essential boundary DOFs on the configured walls.
- Absorbing pressure boundary contribution at the inlet/outlet.
- Rayleigh damping and Newmark time integration.
- Deterministic seeded white-noise input and its pressure derivative.
- Empty-duct reference solve cached within the run.
- Outlet pressure integration at every time step.
- FFTW real-to-complex postprocessing with a shared Hann-window convention.
- Atomic publication of time signals and FFT results through immutable
  `shared_ptr` snapshots.
- Full designed forward-state history for the discrete adjoint.
- Forward, equilibrium, velocity, and acceleration residual diagnostics.
- Serial FGMRES and distributed FGMRES or MUMPS solve paths.
- Fully discrete pass/stop Newmark adjoints and reduced design gradients.
- Timing and iteration diagnostics in `SolverPerformance`.

The serial and parallel implementations cache mesh-, source-, boundary-, and
reference-dependent data where their settings allow it. Design-dependent cut
blocks are reassembled for each accepted candidate, which is necessary because
the material interface has changed.

### What is actually proven

The newest preserved test report is
[`test-results/tests.md`](../test-results/tests.md), generated on 2026-09-04.
It reports five passes and three failures for a parallel Release build:

- `dispatcher_backend`: passed.
- `settings_persistence`: passed.
- `exporter_bundle`: passed.
- FGMRES solver suite at one rank: passed.
- FGMRES solver suite at two ranks: passed.
- Optimizer suite: failed before completing iteration one.
- MUMPS one-rank comparison: reported failed.
- MUMPS two-rank comparison: reported failed.

The MUMPS failures do **not** show rank disagreement. The relevant signature
values were:

```text
FGMRES reference: -0.029528392232428237
MUMPS rank 1:     -0.029528315248063888
MUMPS rank 2:     -0.029528315248063708
```

The two MUMPS runs agree to approximately `1.8e-16`. They were incorrectly
judged against an FGMRES gradient reference with an unnecessarily strict
cross-method tolerance. Rank invariance and cross-linear-solver equivalence
must be tested separately.

These results belong to the last built binaries, not the current unbuildable
source layout.

### What is not yet proven

The forward solver is not ready to support physical certification or claims
such as “this ULTEM wall provides 120 dB of real cabin attenuation.” The
following seams remain open:

- A complete acoustic/structural energy balance has not been validated.
- Existing plane-wave port-power diagnostics have produced suspicious
  reflected/transmitted/unaccounted values and need an independent oracle.
- A full-height solid slab must demonstrably develop nonzero displacement and
  transmit nonzero far-side pressure.
- Coupling-enabled and deliberately coupling-disabled cases need to be
  distinguishable.
- The empty duct must produce unit transmission at every meaningfully excited
  bin.
- Mesh/time-step/refinement convergence has not been established.
- Structural wall boundary conditions have not been independently compared
  with a body-fitted reference problem.
- Both FGMRES and MUMPS gradients need their own finite-difference validation.
- Multi-rank physical outputs need comparison beyond the existing aggregate
  signature.
- No exact paper-default result has passed an end-to-end fidelity review.

Ghost penalties may eventually improve small-cut conditioning, but they do not
replace these checks and should not be used to conceal an incorrect energy or
coupling formulation.

## Optimizer: implemented state

The current optimizer uses the paper-style epigraph formulation. Its ParOpt
design vector is:

```text
[active level-set design variables..., z]
```

It minimizes normalized `z` subject to one constraint for each nonempty
objective group:

```text
z - Phi_pass / objective_scale >= 0
z - Phi_stop / objective_scale >= 0
```

The active design variables are bounded to `[0, 1]`. A solver callback
assembles and solves a candidate once; the adjoint then differentiates the
stored forward result rather than rerunning the transient solely for FFT
differentiation.

ParOpt is pinned to revision
`eae156b756e517fb7fd4a5abb9a203050357b815`. The application now uses its
unmodified `ParOptMMA` implementation. The normalized external epigraph
coordinate is an ordinary MMA design variable, so it receives ParOpt's regular
reciprocal approximation, asymptotes, and move limits.

The pinned upstream `ParOptMMA::optimize` driver declares
`computeKKTError(l1, linfinity, infeasibility)` but passes those output pointers
in the wrong order. `src/optimizer/optimizer.cpp` contains the short public
stepping loop and calls the function in its declared order. It changes only
convergence reporting/testing, not the upstream MMA mathematics.

The former repository-owned paper-specific `EpigraphMMA` is no longer built or
used. This deliberately gives up exact iteration-path parity with the golden
paper in exchange for the mature upstream ParOpt implementation.

### Optimizer behavior still needing proof

- Regular ParOptMMA must pass analytical minimax, bound-escape, flat-gradient,
  scale-invariance, and callback-failure checks using production's external
  normalized `z` formulation.
- Objective and constraint callback derivatives need central-difference
  checks.
- The real `App::Optimizer` needs a small MFEM low-pass integration test using
  the actual forward solve and adjoint.
- Cancellation before a run must produce no state history and remain
  non-exportable.
- A blocked-wall design is a possible local MMA minimum. The current optimizer
  has no global or memetic search and cannot guarantee escape.
- A full paper benchmark is required before claiming that the optimizer
  reproduces the published geometry or final objectives.

## Exact paper and miniapp evidence

The existing `paper_optimizer_miniapp` has quick, paper, high-pass,
high-pass-20dB, Desperado, and MUMPS options. It writes JSON/ZIP artifacts for
the static Plotly report.

The preserved exact-paper artifact records:

```text
mesh:              250 x 50
duration / dt:     0.02 / 2e-5 s
requested MMA:     400 iterations
optimizer status:  diverged
wall time:         295.37 s
history entries:   0
frequency samples: 0
geometry samples:  0
```

That run is a failure, not a paper comparison.

The miniapp log parser now recognizes `Completed MMA iteration`, matching the
optimizer's current message and preserving iteration history in reports.

An older reduced quick run completed 20 iterations and substantially reduced
its worst objective, but it used a coarse, shortened problem. It is evidence
that the callback loop once executed, not evidence of paper fidelity.

The paper publishes final objective values `Phi_pass = 5.06428` and
`Phi_stop = 4.99466`, plus figures. It does not publish raw spectral arrays,
raw geometry, runtime, or the white-noise seed. Pointwise or timing equivalence
must not be claimed from unavailable data.

## UI, publication, and exporter state

Implemented or present in source:

- ImGui/ImPlot desktop UI.
- Band and freeform objective editing.
- Settings persistence.
- Dispatcher state used to gate mutable controls while work is active.
- One background future for optimization or export.
- Thread-safe renderer log access through a mutex.
- Atomic immutable publication of the latest FFT and time-domain signals.
- Embedded GLVis host patched into the pulled GLVis library.
- Localhost stream transport for geometry visualization.
- JSON/ZIP run-data export with Python and MATLAB response templates.

Important limits:

- The latest UI/source reorganization has not been rebuilt.
- The GLVis view is principally the filtered level-set field; the proposed 2D
  frequency-pressure heatmap is not a finished feature.
- Manufacturing mesh generation is still a plan. Current export focuses on
  run data, not a validated watertight printable mesh.
- `SolverResult::U` and `residualNorms` are mutable vectors, not atomic
  snapshots. They must not be read concurrently while the solver is writing.
  Current UI/export gating is part of the safety contract.
- Full forward and adjoint histories are intentionally memory-heavy. Storage
  scales as `O(time_steps * state_size)`; peak paper-case memory has not been
  measured reliably enough to state as a guarantee. Checkpointing is deferred.

## Build and dependency state

The current branch uses:

- MFEM for finite elements and serial/parallel operators.
- Algoim through MFEM for implicit-domain quadrature.
- HYPRE for distributed iterative algebra.
- MUMPS as the optional distributed direct solver.
- ParOpt for MMA.
- FFTW for spectral postprocessing.
- Eigen, SDL2, Dear ImGui, ImPlot, Assimp, and GLVis for app support.

Ipopt and ifopt are not part of this branch's active optimizer path.

On Windows, `build.bat` is a CMake/Ninja wrapper. It activates MSVC, and for
`parallel-cpu` it also activates oneAPI and MS-MPI. Tests are excluded unless
the `tests` argument is supplied. `build.bat deps ...` configures dependencies
and updates `compile_commands.json` for the LSP; it does not build the app.

Expected commands after the source move is repaired:

```bat
build.bat serial tests -j 4
test.bat serial release

build.bat parallel-cpu tests -j 4
test.bat parallel-cpu release
```

The user owns compilation and runtime testing. Source work should not invoke a
different generator or raw Visual Studio build path.

The requested explicit paper tier, for example
`test.bat parallel-cpu release paper 8`, is not implemented in the current
test wrapper.

## Current build blockers in exact order

1. **CMake source discovery**

   `file(GLOB sources "src/*.cpp")` cannot see any nested implementation file.
   Replace it with a short explicit list of the intended subsystem `.cpp`
   files. Explicit listing also keeps unfinished `AageMMA.cpp` out.

2. **Generated exporter template path**

   CMake still reads `src/export_templates.hpp.in`; the working file is now
   `src/exporter/export_templates.hpp.in`.

3. **Include directories**

   Sources still use local flat includes such as `"solver.hpp"`,
   `"global_types.hpp"`, and `"logging.hpp"`, while the target only exposes
   `src/`. Either expose the six subsystem directories or qualify the includes
   consistently. Do one approach, not both.

4. **Settings test source path**

   CMake still adds `src/settings.cpp`; the working file is
   `src/types/settings.cpp`.

5. **Missing optimizer test source**

   `tests/OptimizerTestSuite.cpp` is missing from the working tree. The staged
   old suite can be inspected with `git show :tests/OptimizerTestSuite.cpp`,
   but it uses the superseded fake-solver validation and should not simply be
   accepted as the final suite.

6. **Unfinished MMA duplicate**

   Exclude or remove `src/optimizer/AageMMA.*` before using recursive source
   discovery. It is syntactically and symbolically inconsistent.

7. **Index/worktree split**

   CMake, the ParOpt patch, an old optimizer edit, an old optimizer test edit,
   a paper PDF, and an export-plan edit are staged. The directory move and
   deletion of the old paths are not. A commit in this state would not capture
   the intended project layout.

8. **Documentation drift**

   [`paper_parity_implementation_plan.md`](paper_parity_implementation_plan.md)
   describes several mutually incompatible optimizer states: no dependency
   patch, a local derived MMA class, and a fake `ForwardSolver` test seam. The
   current code instead uses a build-time ParOpt patch, direct `Solver&`, and
   is awaiting the real-solver optimizer suite. Treat this handover and the
   actual source as authoritative until that rolling plan is reconciled.

## Approved trustworthy test direction

Testing should be divided into two layers.

### Core tier

Fast enough for ordinary development and designed to answer whether the
implementation works:

1. Analytical MMA minimax:
   `min z`, `z >= (x - 1)^2`, `z >= (x + 1)^2`, with oracle `x = 0`, `z = 1`.
2. Analytical objective/constraint gradients checked by central differences.
3. Real reduced MFEM low-pass optimizer run:
   `nx=20`, `ny=4`, `duration=0.002`, `dt=0.0001`, five MMA iterations.
4. Optimizer cancellation before starting.
5. Algoim positive/negative complementarity and uncut equivalence.
6. Known FFT sinusoid amplitude, bin, and phase.
7. Empty-duct unit transmission.
8. Full-height solid-slab vibration and far-side pressure.
9. Plane-wave power diagnostics.
10. Pass and stop adjoints against finite design differences.
11. Seeded reproducibility.

Every `App::Optimizer` test should use the real MFEM solver and adjoint. Direct
analytical tests are for ParOpt's MMA machinery, not a fake physical solver.

### Linear-solver and rank comparisons

Run independent comparisons:

- FGMRES rank one versus FGMRES rank two.
- MUMPS rank one versus MUMPS rank two.
- Each method's gradient versus finite differences.
- FGMRES versus MUMPS using physical states, FFT, objectives, and infill with
  justified numerical tolerances.

Do not compare a complete MUMPS gradient signature bit-for-bit with an FGMRES
reference and call the result rank invariance.

### Paper tier

The exact 250-by-50, 1000-step, 400-iteration low-pass benchmark must be
explicit and opt-in. Its first successful result should report correctness and
`review_required` fidelity separately. Only after independent review should
accepted numerical ranges become regression gates.

## Recommended recovery sequence

1. Preserve the current tree and inspect both `git diff` and
   `git diff --cached`; do not clean it.
2. Finish the directory move as one coherent change.
3. Update the minimal CMake paths/source list/include directories.
4. Exclude the unfinished `AageMMA` copy and keep one MMA implementation.
5. Recreate `OptimizerTestSuite.cpp` according to the core plan above.
6. Build the serial test configuration through `build.bat`.
7. Fix compile errors caused only by the move.
8. Run the analytical optimizer tests before any MFEM optimization.
9. Run the serial solver tests, then the reduced real low-pass optimizer test.
10. Build and run the parallel suite.
11. Correct the rank/cross-solver test topology and verify FGMRES and MUMPS
    independently.
12. Add the explicit paper test tier and repair miniapp history parsing.
13. Run the expensive paper benchmark only after every core gate passes.
14. Resume UI pressure visualization or manufacturing export only after the
    numerical baseline is trustworthy.

## Definition of the next stable milestone

The next milestone is reached when all of the following are true:

- The source reorganization is tracked and CMake names only the new layout.
- Serial and parallel Release configurations build through the project
  wrappers.
- The analytical MMA test reaches `x=0`, `z=1` with finite KKT values.
- The reduced real low-pass optimizer finishes five bounded iterations and at
  least one evaluated iterate improves the initial worst objective.
- FGMRES and MUMPS each pass their own one/two-rank and gradient checks.
- Empty-duct, slab-coupling, FFT, residual, reproducibility, and power tests
  report measured values and tolerances.
- The exact paper run is still opt-in and makes no unearned real-world claims.

Until then, the honest project status is:

> The numerical architecture is substantially implemented, distributed
> FGMRES has passed limited one/two-rank checks, and MUMPS has demonstrated
> rank agreement. The current checkout needs mechanical source-layout repair,
> and neither the MMA correction nor paper/physical fidelity is yet verified.
