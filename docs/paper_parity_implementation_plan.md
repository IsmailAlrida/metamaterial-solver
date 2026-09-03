# Paper-Parity Implementation Plan

> Historical implementation log. The active optimizer contract is
> [ipopt_transition_plan.md](ipopt_transition_plan.md); its Ipopt decisions
> supersede the ParOpt sections below.

Last updated: 2026-08-29

This is the rolling implementation plan for the 2D paper-parity milestone. Update this document as decisions, blockers, and verification results change.

## Target

Deliver the configurable 2D vibroacoustic duct from the golden paper:

1. Generate one deterministic broadband inlet signal.
2. Solve the empty and designed ducts with the cut-aware transient formulation.
3. Integrate pressure at the outlet and publish the normalized FFT response.
4. Differentiate the band objective with the fully discrete adjoint.
5. Optimize only design-region variables with MMA.
6. Display the live design, target bands, FFT response, objectives, and iteration state.

Direct 3D optimization, Bloch-Floquet unit cells, export implementation, and GLVis transport changes remain deferred.

## Decisions

- MFEM remains unmodified. Do not patch its MMA API or any dependency source.
- ParOpt v2.1.5 is the selected MMA implementation: <https://github.com/smdogroup/paropt/releases/tag/v2.1.5>. NLopt v2.11.0 is also available as a mature fallback for experiments that do not need ParOpt's MMA implementation.
- ParOpt is fetched source-only and its C++ core is built by the root CMake project; Python/Cython and examples remain excluded.
- Follow the paper's explicit level-set parameterization: bounded nodal mathematical variables are mesh-scaled, interpolated to cell centers, PDE-filtered, and interpolated back to the physical nodal level set. Do not replace that chain with an invented regularized-Heaviside parameterization.
- The current MFEM cut-volume path remains Algoim. MFEM `ex38.cpp` requires LAPACK for its alternative moment-fitting path, not for Algoim: <https://github.com/mfem/mfem/blob/v4.9/examples/ex38.cpp>.
- ParOpt independently requires MPI, BLAS/LAPACK, and METIS. It may run this desktop problem with `MPI_COMM_SELF`; MFEM does not need to become an MPI build just to use ParOpt.
- Keep the existing top-level ownership and direct references. Add no optimizer interface, factory, or snapshot layer.
- Solver and Optimizer define the scientific application contract; Renderer adapts to that contract later. Solver publishes canonical double-precision complex FFT data and accepts an objective derivative with respect to that spectrum. Optimizer owns band semantics, validation, objective values, and design gradients. Neither backend depends on plot samples, dB presentation, freeform drawing, or other Renderer formats.
- Implement the ParOpt problem adapter privately in `optimizer.cpp` unless the actual API forces another file.
- Represent the paper's min-max objective explicitly with one epigraph variable `z`: minimize `z` subject to `z - PhiPass >= 0` and `z - PhiStop >= 0`. This avoids private/internal MMA patches.
- Keep the serial 2D forward solve in MFEM with restarted FGMRES and its existing Gauss-Seidel block preconditioner. Do not convert it to Eigen SparseLU; lower peak RAM is preferred over lower solve time.
- Use a 50-vector FGMRES restart, relative tolerance `1e-10`, absolute tolerance `1e-12`, and at most 1500 iterations. Warm-start each Newmark step from the previous displacement and independently reject a solve whose relative algebraic residual exceeds `1e-9`.
- Retain the complete `U^n = [v^n, vdot^n, vddot^n]` history needed by the adjoint. In normal runs retain the relative norms of all three paper residuals only; materialize complete `R^n = [r1^n, r2^n, r3^n]` vectors only in the coarse verification executable.
- The `parallel-cpu` backend uses MFEM `ParMesh`, `ParFiniteElementSpace`,
  `ParBilinearForm`, Hypre matrices, and one MPI collective solve. FGMRES uses
  a displacement/pressure block-diagonal BoomerAMG preconditioner; MUMPS
  5.9.1 is the higher-memory runtime alternative.
- MUMPS is compiled and linked as a normal required dependency of every
  `parallel-cpu` executable. The runtime option selects an already-linked
  solver; it never conditionally builds, downloads, or installs MUMPS.
- MPI ranks are separate processes. Each process constructs one `App::Solver`
  object containing its local `ParMesh`/true-DOF state; together those local
  objects participate in one collective global solve. There is no separate
  `ParSolver` class or second backend object on rank zero.
- OpenMP is disabled. MPI distributes MFEM/HYPRE/MUMPS work across ranks;
  application code performs cut-sensitivity perturbations serially because the
  current MFEM build does not provide thread-safe finite-element scratch state.

## Current State

### Dependency setup

- [x] Register NLopt v2.11.0 and ParOpt v2.1.5 in the shared CMake dependency source tree.
- [x] Inspect ParOpt's C++ source list and exact Windows requirements.
- [x] Add the smallest static-library target for the C++ core only; exclude Python/Cython and examples.
- [x] Fetch METIS reproducibly and resolve MPI plus BLAS/LAPACK through MS-MPI and oneMKL.
- [x] Add `setup.bat` to install/check the Windows native prerequisites without building the app.
- [x] Add idempotent `setup.sh` checks for the Linux MPI/BLAS/LAPACK/Fortran prerequisites.
- [x] Fetch the unmodified MUMPS superbuild at its real `v5.9.1.0` release tag and expose it to MFEM through `MUMPS::MUMPS`.
- [x] Build MUMPS as an ordinary linked dependency of `parallel-cpu`; runtime
  FGMRES/MUMPS selection does not alter compilation or dependency loading.
- [ ] Run `setup.bat`, reopen the terminal, and confirm `setup.bat check` passes.
- [ ] Run `build.bat deps serial` and confirm NLopt, ParOpt, and METIS configure cleanly.

### Source integration status

- [x] Remove the nonexistent MFEM-MMA calls and leave MFEM unpatched.
- [x] Implement the private ParOpt epigraph problem using public ParOpt APIs.
- [x] Finish the renderer's compile-facing migration from removed `Bandgap`/freeform fields to canonical `FrequencyBand::targetTransmission`; dB conversion now stays in Renderer.
- [x] Qualify Dispatcher callback members with `this->`.
- [x] Replace pointers into temporary slider ranges with named local values.
- [x] Initialize MPI with serialized thread support and destroy the dispatcher/backend objects before `MPI_Finalize()`.
- [x] Keep FE spaces, region classification, node/cell maps, filter data, boundary functionals, clamped DOFs, and the seeded source alive across every design evaluation in one run.
- [x] Add `paper_optimizer_miniapp`, which runs the real Solver/adjoint/ParOpt loop and writes the objective history, final FFT, and final physical level-set grid to ignored JSON under `test-results/paper-optimizer/`.
- [x] Add one static Plotly-CDN HTML report driven by the miniapp JSON. It contains paired paper/implementation geometry and transmission plots, a numerical settings/objective comparison table, and an execution-time table while truthfully marking paper runtime as unreported. No Python plotting wrapper is required.
- [x] Share the Newmark recurrence, inverse-FFT/Newmark adjoint, and cut-element sensitivity between serial and MPI backends in `newmark.hpp`, `adjoint.hpp`, and `cut_sensitivity.hpp`.
- [x] Add the true MPI forward/adjoint path in `par_solver.cpp`, including distributed outlet reduction, gradient reduction, block-preconditioned FGMRES, and runtime MUMPS forward/transpose solves.
- [x] Keep ParOpt on rank zero with `MPI_COMM_SELF`; its callbacks invoke one collective Solver operation while nonzero ranks remain in `Solver::parallelWorkerLoop()`.
- [x] Consolidate the real MFEM forward, slab-coupling, adjoint finite-difference,
  FFT, and rank-invariance checks in `SolverTestSuite.cpp`; keep FGMRES and
  MUMPS rank comparisons as CTest invocations of that one executable.
- [x] Consolidate deterministic low/high/band-pass, freeform, scaling,
  cancellation, and wall-escape cases in `OptimizerTestSuite.cpp`; every case
  drives the real `App::Optimizer` against one curated `ForwardSolver` fake.
- [x] Register the consolidated suites with CTest and keep the build-free
  `test.bat`/`test.sh` wrappers that merge serial/MPI outcomes into the ignored
  `test-results/tests.json` report.
- [x] Make MPI worker ownership explicit on the one primary Solver per process;
  temporary serial comparison solvers no longer broadcast a second shutdown.
- [x] Use one top-level `App::Solver` construction path in `main.cpp`; non-root
  processes call that object's worker loop while rank zero passes the same
  object to Optimizer.
- [x] Follow MFEM's MUMPS lifecycle from `ex25p`: perform fresh symbolic
  analysis when the design changes the sparse graph, then retain each designed
  numerical factorization across its complete Newmark transient and adjoint.
- [x] Make parallel validation and command completion collective before any
  rank returns, following MFEM's `MPI_Allreduce` pattern in `ex16p`.
- [x] Count completed outer ParOpt MMA steps independently of whether the new
  design moved, and require every configured pass/stop group to retain at least
  one meaningfully excited FFT bin.
- [x] Cache `Cpp`, inlet/outlet functionals, and essential displacement DOFs
  once per serial mesh or `ParMesh`; the design-dependent cut blocks still
  assemble once per evaluated geometry.
- [x] Match paper Eq. (29) exactly by transforming outlet states
  `U^0, ..., U^(N-1)` and mapping the inverse-FFT derivative back to those
  same Newmark states.
- [x] Convert private distributed pass/stop adjoint histories to element-local
  DOFs in place; retain one local forward copy because the public true-DOF
  history remains part of `SolverResult`.
- [x] Retain the four designed block AMGs and reuse them for the transposed
  adjoint solve; the symmetric diagonal blocks are unchanged by transposition.
- [ ] Confirm all of the above with the user's Ninja build; no application build has been run in this implementation pass.
- [ ] Run `solver_test_suite` through the registered one/two-rank FGMRES and
  MUMPS CTests, then run `optimizer_test_suite`. Do not diagnose the paper
  optimizer until these isolated Solver and deterministic Optimizer gates pass.
- [ ] Run the miniapp's quick mode and inspect `quick.json` in `tests/paper_optimizer_report.html`.
- [ ] After the coarse run passes, run `--paper` and compare the resulting topology and response with the paper.

### Known numerical risk

- [x] Construct fresh Algoim volume and surface rules for every element assembly so future plus/minus level-set perturbations cannot reuse stale cut quadrature.
- [x] Match the initial design to paper Eqs. (62)-(63): evaluate the cosine field, threshold the mathematical variables to zero/one, then apply the mesh scaling and finite-volume filter.
- [x] Keep the filter diffusion graph inside the design cells; omitted cross-region faces impose the paper's natural Neumann boundary on the design filter.
- [x] Stream the real solver's filtered physical level set to the localhost GLVis adapter without making visualization a solve requirement.
- [x] Reject undefined implicit-interface normals instead of silently dropping
  their quadrature contributions in forward assembly or cut sensitivity.
- [x] Add cut-volume complementarity, uncut/full-element equivalence, and
  empty-duct transmission checks to the consolidated Solver suite.
- [x] Exercise the actual shared FFTW forward transform with a known sinusoid
  and verify its one-sided amplitude and phase convention.
- [ ] Run `solver_test_suite`, which compares the complete MFEM cut derivative
  against a centered design finite difference on a coarse runtime case.
- [x] Avoid ParOpt 2.1.5's swapped MMA convergence outputs with a small local
  driver over the public `ParOptMMA`/`ParOptInteriorPoint` classes. Its KKT
  call uses `(l1, linfty, infeasibility)` in the declared order.
- [x] Preserve the exact epigraph formulation without patching ParOpt:
  `EpigraphMMA` derives from `ParOptMMA`, reuses its geometry approximation,
  and overrides the virtual subproblem evaluations/bounds so only `z` remains
  exact-linear, unregularized, and free of geometry move limits.

## Implementation Order

## Verification Evidence

- [x] `git diff --check` passes after the MUMPS/MPI/FFT/memory corrections.
- [x] `py -B tests/test_paper_adjoint.py` passes all three objective,
  FFT-transpose, and scalar Newmark-adjoint regressions.
- [ ] The user's Ninja build confirms the modified C++ targets compile.
- [ ] `solver_test_suite` passes Algoim complementarity, uncut integration,
  FFT amplitude/phase, empty-duct transmission, slab coupling, adjoint finite
  differences, and one/two-rank FGMRES/MUMPS comparison.
- [ ] `optimizer_test_suite` passes its deterministic real-Optimizer cases,
  including scale invariance and escaping a curated wall with an exact gradient.
- [ ] The quick MUMPS miniapp finishes an optimization and writes a valid JSON
  geometry/report before the exact `--paper --mumps` run is attempted.

### 1. Restore a buildable baseline

- Keep all unrelated TODOs and WIP comments.
- Correct only the compile blockers listed above.
- Remove the unusable MFEM-MMA calls; do not substitute an unverified hand-written optimizer.
- Keep real optimization visibly WIP until the ParOpt target is linked.

Gate: demo and real serial targets compile through `build.bat serial`.

### 2. Prove the forward solver

- Confirm inlet/design/outlet attributes and boundary markers.
- Confirm fixed-air inlet/outlet regions and design-only level-set variables.
- Correct the paper cosine initialization and cell-centered finite-volume filter.
- Assemble solid/acoustic cut volumes with fresh Algoim element rules.
- Assemble and verify `M`, `C`, `K`, `Kup`, `Mpu`, inlet load, and outlet summer.
- Reuse one configured MFEM FGMRES operator and preconditioner for every Newmark step, warm-start from the previous state, and fail on either non-convergence or an independently calculated relative residual above `1e-9`.
- Store each complete state `U^n`; calculate paper Eqs. (23)-(25) on the unconstrained DOFs and retain the three relative residual norms per step.
- Publish immutable double-precision inlet, empty-duct outlet, and designed-duct outlet time histories only after a complete solve.
- Apply the same Hann window to both outlet signals and use FFTW's real-to-complex transform. Publish a properly normalized one-sided amplitude spectrum, complex-ratio phase, a validity mask for under-excited reference bins, and `S(f) = |P(f)| / |P0(f)|`.
- Keep solver verification in the existing lightweight test scripts until a dedicated forward-check executable is requested again.

Gate checks:

- Solid and acoustic cut volumes sum to each element volume.
- Uncut elements match ordinary MFEM integration.
- `Mpu + Kup^T` is near zero.
- Fixed structural DOFs remain zero.
- The same seed produces identical histories.
- A known sinusoid lands in the expected FFT bin.
- Empty duct transmission is approximately one.
- Outlet-vector evaluation matches direct boundary quadrature.
- Every stored `r1`, `r2`, and `r3` relative norm is at most `1e-9`; the coarse check also constructs the full residual vectors before discarding them.
- A paper-default 250-by-50, 1000-step run completes without FGMRES failure or non-finite state/output values.

### 3. Prove the objective and gradient

- Retain the designed and empty-duct complex spectra in double precision as Solver's canonical optimizer-facing output; the existing float plotting result is not an optimization input.
- Build the active-bin pass/stop objective inside Optimizer from domain-level `FrequencyBand` targets expressed as linear transmission.
- Give the shared boundary frequency to the band ending there.
- Reject overlapping bands and bins without meaningful reference excitation.
- Keep dB conversion, drawing interpolation, and plot sampling in Renderer only; remove or ignore those presentation fields in the backend contract.
- Differentiate transmission through the FFT and Hann window.
- Solve the fully discrete Newmark adjoint backward.
- Differentiate only cut-element matrices with perturbation `1e-4 * he`.
- Reverse the node/cell mappings and finite-volume filter.
- Reuse forward and adjoint storage between iterations.
- Reuse the solver-owned forward history, adjoint history, and transposed Newmark matrices; pass only the spectrum derivative and output gradient by reference.

Gate: compare the adjoint gradient with central design finite differences on a coarse mesh; relative error must be at most 0.1% before ParOpt is connected.

### 4. Connect ParOpt

- Add one private `ParOptProblem` implementation backed directly by `Optimizer`, `Solver`, and the active design vector.
- Use `MPI_COMM_SELF` for the local desktop optimizer.
- Expose the active design variables with bounds `[0, 1]` plus an effectively unboxed epigraph variable `z`; its constraints bound it from below, as in the paper.
- Return the objective, the nonempty pass/stop epigraph constraints, and their gradients.
- Configure ParOpt's public MMA iteration and convergence options only; record any paper parameter that ParOpt cannot expose rather than patching it.
- Run iteration zero as the initial design, then gradient, MMA update, and a complete forward solve per accepted iteration.
- Check cancellation only at completed iteration boundaries.

Gate: a short coarse run respects bounds, lowers the worst nonempty band loss, returns a fully evaluated exportable design, and does not accumulate memory per iteration.

### 5. Finish the app surface

- Finish editable duct, material, damping, timestep, source, filter, FFT, and MMA controls using the paper values as defaults.
- Support multiple non-overlapping pass/stop bands with editable dB targets.
- Lock every setting and target editor while Dispatcher is not Idle.
- Show live FFT, target, iteration, `PhiPass`, `PhiStop`, and `z`.
- Keep renderer reads limited to data that is safely published while the worker runs.

Gate: the complete Start/Cancel/Export state flow remains responsive while a real optimization runs.

## Change Log

- 2026-08-29: Chose ParOpt over patched MFEM MMA and third-party hand-rolled MMA code.
- 2026-08-29: Corrected the LAPACK assumption: current Algoim integration does not require MFEM LAPACK; ParOpt has its own BLAS/LAPACK dependency.
- 2026-08-29: Registered pinned ParOpt v2.1.5 as a source-only FetchContent dependency.
- 2026-08-29: Selected MFEM restarted GMRES over Eigen SparseLU for the forward solve to prioritize lower peak memory; full residual histories remain verification-only.
- 2026-08-29: Added the ParOpt C++ core, NLopt v2.11.0, and fetched METIS to CMake without patching MFEM.
- 2026-08-29: Added `setup.bat` for the Visual C++/Ninja/MS-MPI/oneMKL Windows prerequisites and kept `build.bat` as the CMake wrapper.
- 2026-08-29: Recorded the paper's explicit node-to-center/filter-to-node level-set parameterization; no substitute Heaviside model will be introduced.
- 2026-08-29: Made the four bulk blocks cut-aware through one MFEM-delegating `ImplicitDomainIntegrator`; MFEM still owns the element loops, local operator mathematics, and sparse assembly.
- 2026-08-29: Established backend-first ownership: Solver exposes canonical spectral/adjoint primitives, Optimizer defines objectives, and Renderer later adapts dB/freeform controls to those APIs.
- 2026-08-29: Implemented one private ParOpt MMA problem over active design DOFs and an explicit normalized epigraph variable; objective/gradient callbacks reuse the latest complete forward solve.
- 2026-08-29: Added the fully discrete Newmark/FFT adjoint and a dependency-free scalar regression. The FFT transpose identity and scalar adjoint finite difference checks pass; the MFEM cut-gradient runtime gate remains open.
- 2026-08-29: Removed the accidental external `[0,1]` box on `z`; only the mathematical level-set variables retain the paper's box bounds.
- 2026-08-29: Migrated the renderer seam to store linear transmission targets and convert to/from dB only in its controls and plots.
- 2026-08-29: Reused solver-owned forward/adjoint buffers and added `solver_adjoint_check`; the check is source-complete but remains unrun until the user's Ninja build.
- 2026-08-29: Stopped rebuilding run-invariant MFEM spaces, mappings, filter data, boundary functionals, and white-noise excitation for every ParOpt design.
- 2026-08-29: Added a real optimizer miniapp with quick and paper modes. It writes pretty, gitignored JSON; a static Plotly report compares the result side by side with the paper's Fig. 6(a,d), published objectives, and available runtime metrics without a Python wrapper.
- 2026-08-30: Replaced METIS 5.2.1's non-subproject-safe CMake entry point with direct static GKlib/METIS targets; fetched dependency sources remain unmodified and the obsolete top-level `conf/check_thread_storage.c` assumption is gone.
- 2026-08-30: Resolved ParOpt's MSVC-only duplicate `min2`/`max2` symbols with per-source compile-time renaming; no fetched ParOpt file is patched.
- 2026-08-30: Switched FFTW 3.3.10 from its codelet-free Git tree to the official release tarball so its generated DFT/RDFT solver tables link on Windows.
- 2026-08-30: Replaced `EliminateRowCol` with MFEM's bulk `EliminateBC` for the nonsymmetric coupled systems after the first miniapp run exposed the former routine's symmetric-sparsity requirement.
- 2026-08-30: The exact paper run reached the designed transient but failed its independent Newmark residual gate at step 18 before FFT or ParOpt. Added physical, linear, velocity, acceleration, preconditioned-residual, and GMRES-iteration diagnostics; failed miniapp reports now retain the actual error instead of displaying missing objectives as zero.
- 2026-08-30: Switched the four forward/adjoint Newmark linear solves from left-preconditioned GMRES to MFEM FGMRES so the solver's own convergence norm is the unpreconditioned residual. The existing low-memory Gauss-Seidel preconditioner and numerical tolerances remain unchanged for an isolated comparison.
- 2026-08-30: Made Release the default build and added separate opt-in Debug presets/directories; CUDA builds now initialize MFEM's CUDA device before constructing solver data.
- 2026-08-30: Added forward, adjoint, cut-differentiation, filter, ParOpt, and Krylov timing/counter publication to the miniapp report.
- 2026-08-30: Combined the pass/stop adjoint differentiation pass, reused the designed Newmark matrices, and parallelized the independent cut-element perturbations with thread-local MFEM state in parallel-CPU builds.
- 2026-08-30: Kept one persistent localhost GLVis stream for the Solver lifetime; repeated optimization iterations reuse it and connection attempts stop after three consecutive failures.
- 2026-08-30: Extracted the previously inline cut sensitivity from `Solver::differentiateFrequencyResponses()` into one shared serial/MPI implementation. The unavoidable loop differentiates the moving Algoim cut rule; MFEM integrators still supply all element operators.
- 2026-08-30: Added the true MPI ParMesh/Hypre forward and adjoint backend. FGMRES now uses a displacement/pressure BoomerAMG block preconditioner, with MFEM MUMPS as a runtime direct-solver alternative.
- 2026-08-30: Added MPI worker commands around complete Solver calls, collective failure agreement, rank-zero ParOpt callbacks, and early worker shutdown before miniapp report generation.
- 2026-08-30: Corrected the MUMPS superbuild pin from nonexistent `v5.9.1.1` to upstream `v5.9.1.0`; no dependency source is patched.
- 2026-08-30: Added build-free CTest wrappers for serial and 1/2/4-rank MPI
  checks with one shared JSON report, and fixed command/shutdown paths so an
  invalid top-level parallel call or temporary comparison Solver cannot strand
  worker ranks.
- 2026-08-30: Made the MPI object model explicit: every process owns one local
  `App::Solver` handle to one collective solve, and MUMPS is an ordinary
  build-time dependency rather than a runtime-installed or selectively
  compiled feature.
- 2026-08-30: Removed unsafe MUMPS symbolic-reordering reuse after the empty
  duct/design graph transition reproduced `INFOG(1)=-53`; MFEM's own MUMPS
  example performs a fresh analysis for `SetOperator` and keeps the resulting
  factorization for repeated solves.
- 2026-08-30: Made distributed assembly/solve/adjoint failure gates collective,
  validated coupling on final `HypreParMatrix` objects, rejected degenerate
  interface normals, tightened FFT excitation validity per objective group,
  and removed the discarded initial designed assembly.
- 2026-08-30: Aligned FFT samples with paper Eq. (29), using Newmark states
  `U^0` through `U^(N-1)` instead of the shifted `U^1` through `U^N` sequence;
  the shared FFTW implementation now has an amplitude-and-phase regression.
- 2026-08-30: Reduced parallel-adjoint peak memory by converting its private
  histories in place and reused the designed forward AMGs for the transpose
  solve. Fixed-air block matrices remain recomputed to avoid four additional
  persistent sparse matrices in the RAM-first configuration.
- 2026-08-31: Reinstated the sacred ownership model: persistent plain
  `LevelSet` data, Solver-owned MFEM borrowers, guarded
  `setMesh -> assemble -> solve -> differentiate` stages, solver-local
  reference matrices, one MPI command per stage, one Dispatcher future, and
  explicit collective MUMPS teardown. This was a source-only pass; the
  multi-rank verification ladder remains open.
- 2026-09-02: Added a six-call `ForwardSolver` contract at the existing
  Solver/Optimizer seam so one deterministic fake can exercise the production
  Optimizer and ParOpt workflow without MFEM cost. Replaced four overlapping
  solver/optimizer smoke sources with one `SolverTestSuite.cpp` and one
  `OptimizerTestSuite.cpp`; no fetched MFEM or ParOpt source is patched.
- 2026-09-02: Applied the public-API ParOpt conditioning pass from the optimizer
  repair plan: raw/normalized epigraph reporting, nonzero MMA convergence
  tolerances, and the recommended interior-point settings. A local
  `EpigraphMMA` subclass keeps `z` exact-linear, while a short public-API MMA
  driver corrects KKT output ordering; fetched ParOpt remains unmodified.
