# Ipopt Transition Plan

Last updated: 2026-09-03

This is the rolling implementation contract for replacing the custom ParOpt
MMA path with COIN-OR Ipopt. It is deliberately backend-first: the solver and
optimizer define the data contract; the renderer adapts to that contract later.

## Status

- Branch: `ipopt-optimizer`.
- `App::Optimizer` now executes a private direct Ipopt `TNLP`; there is no
  second application optimizer type or backend factory.
- Official source pins selected for evaluation:
  - Ipopt `releases/3.14.20`.
  - IFOPT `2.1.4`.
- The sources live under the shared ignored dependency tree:
  `build/deps/src/ipopt-src` and `build/deps/src/ifopt-src`.
- The source transition through Phase C is present but intentionally unbuilt
  by Codex. `build.* deps <backend>` builds the pinned Ipopt and IFOPT
  libraries without building their examples/tests or the application.
- No fetched dependency will be edited in place.
- ParOpt, GKlib, and the ParOpt-only METIS target have been removed from the
  application build. Serial optimization no longer initializes MPI.
- The deterministic optimizer suite now exercises the production Ipopt path,
  including curated filter objectives, wall escape, epigraph publication,
  prepared and in-callback cancellation, common FFT scaling, and non-finite
  callback rejection. It also finite-differences the exact production TNLP
  Jacobian and proves that repeated callbacks and `z`-only changes reuse the
  cached physics. Its first user-run build and runtime results remain the next
  gate.

## Non-goals

- Do not change MFEM forward physics, Newmark integration, FFT normalization,
  or the discrete adjoint during the optimizer-library migration.
- Do not expose UI plot formats, dB drawing state, ImGui types, or renderer
  state to Ipopt.
- Do not give Ipopt the transient state vector as optimization variables. This
  remains a reduced-space PDE-constrained optimization.
- Do not make Ipopt and MFEM share a `MUMPSSolver` object or factorization.
  They may share the process-wide MPI runtime and linked numerical libraries,
  but each matrix owns its own solver state.
- Do not add a generic optimizer factory. One production optimizer is enough.
- Do not expect changing optimizer libraries to repair unverified acoustic
  power balance, a wall-like local minimum, or a nonsmooth topology model.

## 1. Stable application contracts

### 1.1 Design state

The canonical design remains `LevelSet`:

- `LevelSet::design` stores the app-wide scalar design field.
- `LevelSet::activeDesignDofs` selects the design-region entries that the
  optimizer may modify.
- Inlet and outlet entries remain fixed air and never enter Ipopt's vector.
- `LevelSet` outlives `Solver` and `Optimizer`; both continue to borrow it by
  reference.
- Every candidate must be copied into only the active entries before a solve.
  Inactive entries are never inferred from an optimizer vector.

For `N = activeDesignDofs.Size()`, the optimizer-facing design is

\[
x_d=(s_1,\ldots,s_N), \qquad 0\le s_i\le1.
\]

### 1.2 Forward and adjoint seam

Keep the existing `ForwardSolver` contract in `src/solver.hpp`:

- `setup()` prepares run-invariant mesh/source/reference data.
- `run()` performs one forward solve for the current `LevelSet::design`.
- `frequencyResponse()` returns the solver-native linear response data.
- `differentiateFrequencyResponses(...)` maps derivatives with respect to
  frequency response values back to the full design vector.
- `get_status()` publishes forward-solver outcome.

The optimizer may orchestrate these calls and cache their result. It must not
reimplement Newmark, FFT, or the adjoint. The fake solver in
`tests/OptimizerTestSuite.cpp` remains the fast deterministic implementation
of exactly this interface.

### 1.3 Objective contract

The optimizer owns the conversion from `FrequencyResponse` to objective
groups. The solver remains blind to pass bands, stop bands, freeform drawing,
and dB presentation.

For each active frequency sample `m`, let `S_m(x)` be linear transmission and
`T_m` the linear target. The current per-bin error is

\[
e_m(x)=\left(\frac{S_m(x)-T_m}{T_m}\right)^2.
\]

The app exposes at most two nonempty groups:

\[
\Phi_p(x)=\sum_{m\in P}e_m(x),\qquad
\Phi_s(x)=\sum_{m\in S}e_m(x).
\]

Band mode and freeform mode may choose different samples and aggregation, but
both must return the same backend structure:

```cpp
struct ObjectiveEvaluation {
    std::vector<double> groups;                 // one or two values
    std::vector<mfem::Vector> groupGradients;   // full LevelSet coordinates
};
```

This does not need to become a public class. It describes the data already
computed inside `Optimizer::Problem` and gives its replacement a testable
contract.

### 1.4 Status, cancellation, and publication

- `OptimizerStatus::Working` begins before Ipopt initialization.
- Cancellation is cooperative. `request_cancel()` sets the existing atomic;
  Ipopt's iteration callback observes it and requests termination.
- A cancelled or failed trial point is not published as an iteration.
- `iteration` counts accepted Ipopt major iterations, not callback calls,
  forward solves, line-search trials, or restoration steps.
- `pass_objective` and `stop_objective` describe the most recently accepted
  complete design.
- `is_exportable()` is true only when the current `LevelSet` has a complete
  forward evaluation at a converged or iteration-limited final design.
- If Ipopt stops at a point that is not the cached evaluated design, run one
  final forward evaluation before publication/export.

## 2. What ParOpt currently does

### 2.1 Build integration

`CMakeLists.txt` currently:

1. Finds MPI, BLAS, and LAPACK even for the serial MFEM backend because
   ParOpt requires them.
2. Fetches and manually compiles GKlib and METIS.
3. Fetches ParOpt 2.1.5 source-only.
4. Manually lists thirteen ParOpt translation units in target `paropt`.
5. Applies an MSVC-only symbol rename to `ParOptInteriorPoint.cpp`.
6. Links `ParOpt::paropt` publicly into `metamaterial_core`.

Why: upstream ParOpt is not supplied as a subproject-safe CMake dependency,
and its MMA implementation was the nearest implementation of the paper's
optimizer. The result makes the app maintain ParOpt's build and part of its
algorithm, which is precisely what this migration removes.

### 2.2 Source integration

All algorithm-specific code is in `src/optimizer.cpp`:

- Includes near the top pull in `ParOptOptimizer.h` and related headers.
- `EpigraphMMA` subclasses `ParOptMMA` to freeze and manually handle the
  epigraph coordinate.
- `MMAResult` and `runMMA()` reproduce ParOpt's outer MMA loop and termination
  bookkeeping.
- `Optimizer::Problem : ParOptProblem` adapts `LevelSet`, forward solves,
  group objectives, and gradients to ParOpt vectors.
- `Optimizer::optimize()` creates options, `EpigraphMMA`, and
  `ParOptInteriorPoint`, configures MMA asymptotes/tolerances, runs the custom
  loop, and translates the result to application state.

The current problem uses `MPI_COMM_SELF`. ParOpt itself therefore runs only on
rank zero. The expensive solver callbacks remain collective across the MFEM
communicator. Replacing ParOpt with rank-zero Ipopt does not remove existing
optimizer-level MPI parallelism because there is none.

### 2.3 Current mathematical program

The current ParOpt vector is

\[
x=(x_d,z)\in\mathbb{R}^{N+1}.
\]

It minimizes the epigraph variable

\[
\min f(x)=z
\]

subject to one constraint for every nonempty group `g`:

\[
g_g(x)=z-\frac{\Phi_g(x_d)}{\alpha}\ge0,
\]

with bounds `0 <= x_d <= 1`. The current scaling `alpha` is frozen from the
initial worst objective. Its Jacobian row is

\[
\nabla g_g=
\left[-\frac{\nabla\Phi_g}{\alpha},\ 1\right].
\]

This is a standard minimax epigraph. The unusual part is not the math; it is
the custom MMA subclass and hand-copied outer loop used to make ParOpt honor
it.

### 2.4 ParOpt names that leak into app contracts

These must become backend-neutral when Ipopt becomes production:

| Current location | Current name | Replacement |
|---|---|---|
| `src/optimizer.hpp` | `paroptSeconds` | `optimizerSeconds` |
| `src/optimizer.hpp` | `mma_bound` | `epigraph_bound` |
| `src/optimizer.hpp` | `get_mma_bound()` | `get_epigraph_bound()` |
| `src/dispatcher.hpp` | `get_mma_bound()` | `get_epigraph_bound()` |
| `src/renderer.cpp` | `MMA bound` | `Worst scaled objective` |
| `src/exporter.cpp` | `mma_bound` JSON | `epigraph_bound` |
| `tests/paper_optimizer_miniapp.cpp` | `paropt_seconds` | `optimizer_seconds` |
| `tests/paper_optimizer_report.html` | ParOpt/MMA labels | Ipopt/optimization labels |

Persistence should read legacy MMA keys for one compatibility release but
write only backend-neutral keys.

## 3. Ipopt translation

### 3.1 NLP shape

Keep the same epigraph formulation for both one- and two-group problems. One
extra scalar and one constraint are negligible beside a PDE solve, while a
single shape avoids conditional callback code and preserves existing exported
semantics.

- Variables: `n = N + 1`.
- Constraints: `m = number of nonempty objective groups`, currently 1 or 2.
- Design bounds: `[0,1]`.
- Epigraph bound: `[0,+infinity)` because every group error is nonnegative.
- Objective: `f(x)=z`.
- Constraint bounds: `0 <= g_g(x) <= +infinity`.
- Jacobian nonzeros: `m*(N+1)`. Each group gradient is generally dense after
  filtering and the adjoint, even if the underlying FE matrices are sparse.
- Hessian: not supplied initially. Use Ipopt's limited-memory approximation.

Freeze a positive run scale

\[
\alpha=\max(1,\max_g\Phi_g(x^0)).
\]

The lower bound of one avoids amplifying already-small objectives. Do not
update this scale during line search; changing the NLP while Ipopt is solving
invalidates its model.

Initialize

\[
z^0=\max_g\Phi_g(x^0)/\alpha+\delta
\]

with a small positive feasibility margin `delta`. The first point is then
feasible instead of forcing an unnecessary restoration phase.

### 3.2 Exact `TNLP` callback map

Implement one private `Optimizer::Problem final : public Ipopt::TNLP` in
`src/optimizer.cpp`.

```cpp
bool get_nlp_info(
    Ipopt::Index& n,
    Ipopt::Index& m,
    Ipopt::Index& nnz_jac_g,
    Ipopt::Index& nnz_h_lag,
    IndexStyleEnum& index_style) override;

bool get_bounds_info(
    Ipopt::Index n,
    Ipopt::Number* x_l,
    Ipopt::Number* x_u,
    Ipopt::Index m,
    Ipopt::Number* g_l,
    Ipopt::Number* g_u) override;

bool get_starting_point(... ) override;
bool eval_f(... ) override;
bool eval_grad_f(... ) override;
bool eval_g(... ) override;
bool eval_jac_g(... ) override;
bool eval_h(... ) override;
bool intermediate_callback(... ) override;
void finalize_solution(... ) override;
```

Callback behavior:

- `get_nlp_info`: C-style zero-based indexing, `nnz_h_lag=0`.
- `get_bounds_info`: fill design and epigraph bounds plus lower-bounded
  epigraph constraints.
- `get_starting_point`: copy active `LevelSet::design`; seed feasible `z`.
- `eval_f`: return `z`; no solver call.
- `eval_grad_f`: all zero except the epigraph entry equals one; no solver call.
- `eval_g`: ensure the design-prefix cache is current, then return
  `z-Phi_g/alpha`.
- `eval_jac_g`: on the structure call, emit dense row/column indices. On the
  value call, ensure objective gradients are current and return
  `[-gradPhi/alpha,1]`.
- `eval_h`: return `false` only if unexpectedly called; the configured
  limited-memory Hessian means Ipopt should not request it.
- `intermediate_callback`: publish accepted-iteration diagnostics and return
  `false` when cancellation is requested.
- `finalize_solution`: copy the final design/status, but publish it only after
  confirming a complete cached forward evaluation.

### 3.3 Cache contract

Ipopt may evaluate the same design with different `z`, repeat callbacks in a
different order, and reject line-search candidates. Cache identity is
therefore the first `N` design values only—not the pointer, `new_x`, or `z`.

The private cache holds:

```cpp
std::vector<double> evaluatedDesign;
std::vector<double> groupValues;
std::vector<mfem::Vector> groupGradients;
bool valuesValid = false;
bool gradientsValid = false;
```

Rules:

1. A changed design invalidates values and gradients.
2. `eval_g` runs one forward solve and objective evaluation.
3. `eval_jac_g` reuses that forward state and performs one adjoint operation
   per nonempty group.
4. Changing only `z` reuses both values and gradients.
5. A callback failure returns `false`; it does not throw through Ipopt.
6. Store the exception/error message in the problem object so
   `Optimizer::optimize()` can log a useful terminal reason.
7. Do not publish a trial design from `eval_g` or `eval_jac_g` as an accepted
   optimizer iteration.

### 3.4 Ipopt construction and options

Use the Windows-safe factory and reference-counted pointer API:

```cpp
Ipopt::SmartPtr<Ipopt::IpoptApplication> app = IpoptApplicationFactory();
Ipopt::SmartPtr<Optimizer::Problem> problem = new Optimizer::Problem(*this);

const auto init = app->Initialize("");
const auto status = init == Ipopt::Solve_Succeeded
    ? app->OptimizeTNLP(problem)
    : init;
```

`Initialize("")` deliberately ignores an ambient `ipopt.opt` file so a file
in the process working directory cannot silently change application behavior.
Check every option setter result.

Initial production options:

```text
hessian_approximation = limited-memory
grad_f_constant = yes
bound_relax_factor = 0
honor_original_bounds = yes
max_iter = OptimizerSettings::maxIterations
tol = explicit app setting/default
acceptable_tol = explicit app setting/default
print_level = mapped into LogFunction policy
```

The limited-memory nonlinear variable list should exclude the linear
epigraph variable `z`. Do not enable Ipopt's derivative checker on the paper
mesh; use it only in fake/coarse deterministic tests because each check costs
extra solver calls.

Map terminal statuses explicitly:

- `Solve_Succeeded` and `Solved_To_Acceptable_Level` -> `Converged`.
- `Maximum_Iterations_Exceeded` -> `MaximumIterations` if the final design is
  completely evaluated.
- `User_Requested_Stop` -> `Cancelled` only when our cancel flag caused it.
- Invalid-number, restoration, infeasibility, initialization, and internal
  failures -> `Error` with the Ipopt status and stored callback error.

### 3.5 Optimizer settings after migration

Delete these ParOpt-only controls after the Ipopt path is proven:

- `mmaInitialAsymptote`.
- `mmaDecreaseAsymptote`.
- `mmaIncreaseAsymptote`.
- `mmaConstraintPenalty`.

Keep `maxIterations`. Add only settings that users can meaningfully tune:

- desired/acceptable convergence tolerance;
- optional maximum wall time;
- Ipopt print verbosity only if console noise needs a user control.

Do not expose dozens of Ipopt knobs in the renderer. Advanced experiments can
use code defaults until a real case proves a setting belongs in the app.

## 4. Source-level Ipopt and IFOPT integration

### 4.1 Upstream facts

- Ipopt 3.14.20 has no root `CMakeLists.txt`; its official build is Autotools
  (`configure`, `make`, `make install`). CMake must orchestrate it with
  `ExternalProject` rather than pretending `FetchContent_MakeAvailable`
  can compile it.
- IFOPT 2.1.4 is a CMake project. Its README documents a standalone CMake
  build/install followed by `find_package(ifopt)` and linking
  `ifopt::ifopt_ipopt`; it does not document `FetchContent_MakeAvailable`.
- Both sources can still be downloaded by `FetchContent` into
  `build/deps/src`. Download mechanism and build mechanism are separate.

### 4.2 Build order

The CMake dependency chain is:

```text
Eigen / BLAS / sparse linear solver
              -> ipopt_external
              -> ifopt_external
              -> imported ifopt::ifopt_ipopt
              -> metamaterial_core
```

Use IFOPT's own CMake project in `ifopt_external`; do not re-list its `.cc`
files and do not patch its source. Disable SNOPT and project tests. IFOPT's
upstream CMake currently creates one example unconditionally; build the two
library targets rather than the all target so that example stays out of the
normal application build.

### 4.3 Platform rules

Windows:

- `setup.bat` checks/installs the basic MSYS2 tools required by official
  Ipopt: `binutils diffutils git grep make patch pkg-config`.
- `build.bat` activates MSVC and oneMKL for both serial and parallel builds.
- Ipopt is compiled with MSVC using `--enable-msvc`; do not link a MinGW-built
  Ipopt into the MSVC application.
- Use oneMKL LP64 and `pardisomkl` for Ipopt's KKT systems. Never mix ILP64
  with the app's 32-bit index contract.
- Copy the installed Ipopt DLL(s) beside executables at build/install time.

Linux/WSL:

- `setup.sh` provides Autotools basics, Fortran, BLAS/LAPACK, and MPI.
- Reuse one source-built MUMPS configuration rather than introducing a second
  independently versioned MUMPS copy into the same process.
- If parallel MUMPS is linked, configure Ipopt with `--disable-mpiinit`; main
  remains the only owner of `MPI_Init`/`MPI_Finalize`.
- Preserve runtime lookup with an application-relative RPATH or install the
  Ipopt shared library beside the executable.

`build.bat deps <backend>` and `build.sh deps <backend>` configure the project,
refresh `compile_commands.json`, and build the Ipopt and IFOPT library targets;
a deps command that merely downloads source is not complete enough for the LSP.

### 4.4 Why IFOPT remains optional at the source layer

IFOPT is useful syntax sugar for variables, costs, constraints, and Eigen
Jacobian blocks. The production migration should first implement the direct
`TNLP` adapter because this app needs precise control over:

- accepted-iterate publication;
- expensive PDE callback caching;
- callback failures and cancellation;
- the rank-zero/MPI worker protocol;
- final-design re-evaluation.

Build IFOPT now so small comparison programs can be written. Do not insert a
second production optimizer abstraction merely to use it. If an IFOPT trial
can express the cache and MPI lifecycle more clearly than the direct adapter,
that evidence can reverse this decision before ParOpt is removed.

## 5. Exact code changes

### Phase A: source dependencies, no optimizer behavior change

`CMakeLists.txt`

- Include `ExternalProject`.
- Fetch pinned Ipopt and IFOPT source into `METAMATERIAL_DEPS_SOURCE_DIR`.
- Add ordered source build/install targets.
- Expose `Ipopt::ipopt`, `ifopt::ifopt_core`, and `ifopt::ifopt_ipopt` targets.
- Keep `ParOpt::paropt` linked until Phase C so the branch stays runnable.
- Do not build dependency tests/examples.

`setup.bat`, `setup.sh`, `build.bat`, `build.sh`

- Add only the native build tools required by the official source builds.
- Make deps mode compile/install both libraries and remain idempotent.

### Phase B: direct Ipopt adapter beside ParOpt

`src/optimizer.cpp`

- Add Ipopt headers.
- Implement the private `TNLP` form of `Optimizer::Problem` from section 3.
- Keep existing objective and gradient helper code; move no solver math.
- Add a temporary compile-time selection only if required to compare results.
  Prefer one short CMake option over a factory or virtual backend interface.

`src/optimizer.hpp`

- Rename timing/bound publication to backend-neutral names.
- Keep constructor, top-level references, statuses, and public run methods.

`tests/OptimizerTestSuite.cpp`

- Run the exact production `Optimizer` against the existing deterministic
  `FakeSolver`.
- Add direct checks of epigraph values/Jacobian, callback caching, cancellation,
  a rejected trial, final accepted design, and nonfinite callback handling.

### Phase C: make Ipopt production and delete ParOpt

`src/optimizer.cpp`

- Delete `EpigraphMMA`, `MMAResult`, `runMMA`, all `ParOpt*` types, and all MMA
  option wiring.
- Keep only the Ipopt `TNLP` adapter and application status translation.

`CMakeLists.txt`

- Delete ParOpt, GKlib, and METIS blocks if no remaining target uses them.
- Remove `ParOpt::paropt` from `metamaterial_core`.
- Serial builds no longer need MPI merely for the optimizer. Keep MPI only
  where the selected MFEM/Ipopt linear-solver backend actually requires it.

`src/settings.hpp`, `src/settings.cpp`, `src/renderer.cpp`

- Delete MMA-only controls and present the small backend-neutral set.
- Preserve legacy read compatibility for existing settings files.

`src/dispatcher.hpp`, `src/exporter.cpp`, tests and report files

- Apply the backend-neutral publication renames from section 2.4.
- Fix the miniapp progress parser to consume the optimizer's actual iteration
  message rather than hardcoding `Completed optimization iteration` while the
  source logs `Completed MMA iteration`.
- Let the miniapp accept either convergence or a valid iteration limit instead
  of demanding only `MaximumIterations`.

## 6. Verification gates

### 6.1 Fast optimizer contract tests

Use the real `Optimizer` with the existing fake solver. Curated cases must
cover:

1. One group with a known interior optimum.
2. Two groups whose minimax optimum is known.
3. A wall-like initial point with a supplied opening gradient.
4. Common FFT scaling invariance.
5. Prepared cancellation before the first solve.
6. Cancellation from Ipopt's iteration callback.
7. Repeated `eval_g`/`eval_jac_g` at the same design performs one forward and
   one gradient evaluation, not one per callback.
8. Changing only `z` performs no new physics.
9. Nonfinite objective/gradient yields an explicit `Error`, never an access
   violation or silently accepted design.
10. Final published design/objectives refer to the same evaluated point.

Add a finite-difference check for the production epigraph Jacobian on the fake
problem. This catches sign, scaling, active-index, and epigraph-column errors
without an MFEM solve.

### 6.2 Solver/optimizer separation

Keep solver correctness tests independent of optimizer-library choice:

- FGMRES rank invariance.
- MUMPS rank invariance.
- FGMRES-versus-MUMPS agreement as a separate tolerance/report, not mixed
  into the rank-invariance verdict.
- Coupling-enabled versus coupling-disabled wall transfer.
- Energy/power balance and body-fitted reference when those fixtures exist.

Optimizer tests may use real solver fixtures, but a failure must report
whether the forward solve, adjoint, callback algebra, or nonlinear optimizer
failed.

### 6.3 Staged runtime checks

1. Run the fake optimizer suite in serial.
2. Run a coarse real-solver optimization for a few iterations.
3. Run the same coarse case with one and two MPI ranks.
4. Compare direct TNLP and IFOPT toy problems only; do not run both production
   backends indefinitely.
5. Run the paper miniapp after the fast gates pass.
6. Treat the long paper benchmark as opt-in, not a normal CTest.

## 7. Adversarial pass

### 7.1 Line-search cost

Ipopt is a line-search interior-point method. One major iteration can request
several trial designs and therefore several complete forward solves. It may be
more robust than the custom MMA path yet more expensive per accepted design.
The console/report must distinguish:

- accepted optimizer iterations;
- forward callback count/time;
- adjoint callback count/time;
- total optimizer wall time.

Do not disable line search to imitate MMA. First measure the coarse fixtures.

### 7.2 Smoothness mismatch

Ipopt assumes sufficiently smooth first derivatives. Current numerical risks
violate that ideal:

- `S=|P|/|P0|` is nondifferentiable at `P=0`;
- returning a zero derivative near zero transmission can make a sealed wall a
  stationary point;
- interface topology changes and cut-element finite differences can make the
  reduced objective noisy;
- tiny linear targets produced from large negative dB values create severe
  inverse-target scaling.

The migration must not hide these behind looser tolerances. The fake suite
proves callback algebra; coarse finite differences prove the real reduced
gradient. If the latter fails, fix the model/gradient before tuning Ipopt.

### 7.3 Restoration and callback failure

An infeasible initial epigraph point causes restoration work; seed a feasible
one. A failed PDE solve at a trial point must return `false`, preserve the last
accepted design, and provide a durable error. Do not throw across the C ABI or
publish the half-evaluated trial.

### 7.4 MPI lifecycle

Only rank zero executes Ipopt. Worker ranks remain in the existing solver
command loop. A physics callback is collective and must finish before Ipopt's
rank-zero KKT solve begins.

If Ipopt is built with parallel MUMPS, its default communicator may be
`MPI_COMM_WORLD`. That would deadlock because workers are not inside Ipopt.
Set Ipopt's `mumps_mpi_communicator` to

```cpp
MPI_Comm_c2f(MPI_COMM_SELF)
```

only when that option exists in the selected build. Ipopt must be configured
with `--disable-mpiinit`; `main.cpp` remains sole owner of MPI initialization
and finalization.

The dispatcher may launch the optimization workflow off the UI thread only if
the process requests/provides an MPI thread level compatible with that call
site. Do not assume `MPI_THREAD_MULTIPLE`; the solver workflow remains one
serialized background operation.

### 7.5 Memory and scale

The TNLP adapter itself is small:

- candidate design: `O(N)`;
- cached design: `O(N)`;
- one or two dense group gradients: `O(mN)`;
- dense constraint Jacobian values: `O(mN)`;
- limited-memory history: approximately `O(kN)` for history length `k`.

The transient forward/adjoint history remains the dominant allocation. Avoid
copying full MFEM histories into Ipopt or IFOPT types.

## 8. Scalability heads-up

Stock Ipopt is not an MPI-distributed nonlinear optimizer. Our current useful
parallelism is still inside each MFEM forward/adjoint callback:

```text
rank-zero Ipopt trial
    -> collective distributed MFEM solve/adjoint
    -> reduced objectives/gradients returned to rank zero
    -> rank-zero Ipopt KKT step
```

This is appropriate while the PDE solve dominates and the reduced design plus
limited-memory vectors fit comfortably on rank zero. It stops scaling when:

- millions of design variables make rank-zero vectors/history significant;
- Ipopt's KKT solve becomes comparable to or slower than the PDE callbacks;
- line-search trial count dominates runtime;
- the design vector itself must be distributed.

At that point, profile first. Possible successors include a distributed TAO,
PETSc-based reduced-space method, or another optimizer designed for distributed
vectors. Do not build that abstraction now. The stable `ForwardSolver` and
active-design contracts above are the seam that makes a later replacement
possible.

## 9. Official references

- Ipopt 3.14.20 source:
  <https://github.com/coin-or/Ipopt/tree/releases/3.14.20>
- Ipopt installation:
  <https://coin-or.github.io/Ipopt/INSTALL.html>
- Ipopt C++ interface:
  <https://coin-or.github.io/Ipopt/INTERFACES.html>
- `TNLP` source/API:
  <https://coin-or.github.io/Ipopt/IpTNLP_8hpp_source.html>
- Ipopt options:
  <https://coin-or.github.io/Ipopt/OPTIONS.html>
- Ipopt derivative checker and limited-memory notes:
  <https://coin-or.github.io/Ipopt/SPECIALS.html>
- Ipopt return codes:
  <https://coin-or.github.io/Ipopt/IpReturnCodes__inc_8h.html>
- Ipopt official C++ example:
  <https://github.com/coin-or/Ipopt/blob/stable/3.14/examples/Cpp_example/cpp_example.cpp>
- IFOPT source and README:
  <https://github.com/ethz-adrl/ifopt/tree/2.1.4>

## Decision log

- 2026-09-02: Verified the new branch still executes the existing ParOpt
  implementation; branch naming alone changed no backend behavior.
- 2026-09-02: Preserved `ForwardSolver` as the deterministic test seam; no new
  optimizer interface/factory is planned.
- 2026-09-02: Selected a reduced-space Ipopt `TNLP` with the existing epigraph
  math and limited-memory Hessian.
- 2026-09-02: Kept Ipopt rank-zero-only and MFEM callbacks collective.
- 2026-09-02: Pinned official Ipopt 3.14.20 and IFOPT 2.1.4 source for build
  integration; neither source tree will be patched.
- 2026-09-02: Corrected the IFOPT integration note after reading its pinned
  README: upstream documents standalone CMake install plus `find_package`, not
  `FetchContent_MakeAvailable`. CMake will orchestrate that build rather than
  reproduce IFOPT's library sources.
- 2026-09-02: Added Phase A source targets. `FetchContent` only pins/populates
  each official tree; Ipopt builds through its Autotools flow and IFOPT through
  its own CMake project. Windows uses MSVC + oneMKL Pardiso behind MSYS2;
  Linux reuses the selected serial/parallel MUMPS source build.
- 2026-09-02: Kept dependency examples and tests out of the default build.
  `optimizer_dependencies` builds only the libraries, while ParOpt remains the
  active application backend until the TNLP contract tests exist.
- 2026-09-03: Replaced the custom ParOpt MMA subclass and copied outer loop
  with one private `Optimizer::Problem : Ipopt::TNLP`. The production NLP is
  the normalized reduced-space epigraph with active design bounds, exact dense
  first derivatives, and Ipopt limited-memory curvature.
- 2026-09-03: Removed ParOpt, GKlib, and its private METIS build from CMake;
  linked `metamaterial_core` directly to `Ipopt::ipopt`; kept IFOPT available
  only as an optional source-built comparison library.
- 2026-09-03: Removed ParOpt-only settings and publication names, made serial
  mode independent of MPI, and added Windows Ipopt DLL deployment beside the
  application and optimizer-bearing test executables.
- 2026-09-03: Preserved rank-zero nonlinear optimization for the parallel
  app. Distributed MFEM forward/adjoint callbacks still use all ranks, while
  Ipopt's own Linux MUMPS KKT solve is confined to `MPI_COMM_SELF`.
- 2026-09-03: Added explicit forward/gradient callback counts beside their
  timings in the miniapp JSON and HTML report. This separates Ipopt line-search
  trial growth from slower MFEM solves without adding another diagnostics path.
- 2026-09-03: Made the Windows test wrapper activate oneMKL for serial tests as
  well as parallel tests because the serial Ipopt DLL uses PardisoMKL.
- 2026-09-03: Added an explicit production-TNLP derivative/cache diagnostic to
  the deterministic fake-solver suite. It checks every design column, the
  epigraph column, repeated callback reuse, and `z`-only reuse without exposing
  another optimizer implementation.
- 2026-09-03: Staged the Ipopt shared library beside optimizer-bearing Linux
  and Windows executables; Linux uses an application-relative `$ORIGIN` RPATH.
- 2026-09-03: Made the Windows MSYS2 root overridable and changed dependency
  validation to report the exact missing base tool. UCRT64 on `PATH` does not
  replace the `/usr/bin/make` package required by Ipopt's MSVC build.
- 2026-09-03: Converted Ipopt's shell-facing Windows paths with `cygpath` so
  Autotools does not parse the drive-letter colon as a source-path separator.
  Added bounded Pacman retries for transient MSYS2 mirror resets.
- 2026-09-03: Made every Ipopt Autotools phase explicitly enter its requested
  build directory and inherit the MSVC environment. This prevents MSYS2 login
  Bash from building under its home directory or losing `cl.exe` between the
  configure and build ExternalProject steps.
- 2026-09-03: Removed Windows positional-argument quoting from the Ipopt build
  driver after `cmd.exe` swallowed Bash's `$1`. Windows now follows Ipopt's
  documented out-of-tree sequence with an explicit non-login MSYS2 shell,
  `/usr/bin/make`, and a configure-driver content hash that invalidates stale
  ExternalProject configure stamps when the driver changes.
- 2026-09-03: Adapted IFOPT 2.1.4 to CMake 4's policy floor and modern Ipopt's
  MSVC install names/layout entirely from the parent build. Neither fetched
  dependency is patched.
- 2026-09-03: Scoped MSVC's forced `<cassert>` include to IFOPT's isolated
  build and imported target. IFOPT 2.1.4 and current upstream use `assert`
  without including its standard header; the dependency source remains clean.
- 2026-09-03: Restored IFOPT's normal MSVC exception/RTTI platform flags after
  the first compatibility argument accidentally replaced `CMAKE_CXX_FLAGS`;
  added the independently missing `<iostream>` include for its Ipopt adapter.
