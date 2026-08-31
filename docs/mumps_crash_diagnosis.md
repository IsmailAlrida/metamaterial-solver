# MUMPS crash diagnosis (rolling)

Status: the MUMPS smoke check passes at multiple ranks. The application-only
rank-two `0xc0000005` is now traced to distributed matrix addition during
reference assembly, before MUMPS is called. The source correction has not yet
been rebuilt or runtime-tested; the user owns the Ninja build/run step.

## 2026-08-31 rank-count root cause

The last complete entries on both ranks were `reference_Muu`, `reference_Kuu`,
`reference_Mpp`, and `reference_Kpp`. The next statement formed Rayleigh
damping with `mfem::Add(alpha, Muu, beta, Kuu)`.

MFEM documents that distributed `HypreParMatrix::Add` requires both operands
to have identical row/column partitions **and identical `col_map_offd`
arrays**. Separately assembled mass and stiffness forms do not guarantee the
same off-process sparsity: elasticity can couple off-rank vector components
that vector mass does not. With one rank there is no off-process column map,
so the invalid assumption is invisible; with two or more ranks it can reach
native HYPRE memory with incompatible maps. The processor model is unrelated.

The parallel solver no longer adds separately assembled Hypre matrices.
Rayleigh damping and the Newmark effective operators are assembled directly
with MFEM `ParBilinearForm`s, and the coupled systems are merged with
`HypreParMatrixFromBlocks`, whose contract permits the blocks' independent
off-process maps. The only remaining `Add` in `par_solver.cpp` is the local
serial `SparseMatrix` coupling check.

The adjoint also no longer asks MFEM to gather into the original serial finite
element space, which violated `GetSerialGridFunction`'s requirement that its
space belong to `ParMesh::GetSerialMesh`. Each rank now maps its owned true
design DOFs to the canonical root design and returns them with `MPI_Gatherv`.
This is O(global design DOFs) only on rank zero, matching the current
root-owned optimizer. Forward states and adjoints remain partition-local.

## Applied correction

- `MUMPS_openmp` and ScaLAPACK are disabled. MUMPS remains MPI-parallel and
  uses the sequential oneMKL BLAS/LAPACK layer selected by
  `BLA_VENDOR=Intel10_64lp_seq`.
- HYPRE and MFEM OpenMP are disabled; MPI remains the only parallel execution
  layer inside the numerical backend.
- Parallel entrypoints use `mfem::Mpi::Init()` followed by
  `mfem::Hypre::Init()` and leave finalization to MFEM's singleton lifetimes.
- Forced PORD ordering was removed from the real solver and smoke check; both
  now use MUMPS automatic ordering like MFEM's own example.
- The smoke check constructs MUMPS from the matrix communicator.
- The two existing solver objects remain for now: retaining two factors costs
  memory, but is legal and was not proven to cause error `-53`.
- MFEM exceptions are enabled so a MUMPS failure returns control to the app
  instead of immediately calling `MPI_Abort`.
- Every MPI rank appends factorization diagnostics to
  `logs/mumps_rank_<rank>.log`. Entries identify the reference/designed and
  initial/effective matrix, its dimensions and nonzero count, and whether
  factorization completed or threw. MUMPS error printing is enabled at level 1.

## Latest runtime evidence

The rebuilt `--desperado --mumps` run reached both reference factorizations:

```text
event=factor_begin matrix=reference_initial ranks=1
    global_rows=23103 global_columns=23103 local_rows=23103 global_nnz=214503
event=factor_complete matrix=reference_initial
event=factor_begin matrix=reference_effective ranks=1
    global_rows=23103 global_columns=23103 local_rows=23103 global_nnz=359846
event=factor_complete matrix=reference_effective
```

It then exited without `MPI_Finalize` with `0xc0000005`. Windows Error
Reporting recorded:

```text
Fault module: paper_optimizer_miniapp.exe
Exception code: 0xc0000005
Exception offset: 0x505e7b
```

Disassembling that exact offset identifies
`mfem::SparseMatrix::AddMult`. The fault is the read corresponding to MFEM's:

```cpp
d += d_A[j] * d_x[d_J[j]];
```

In other words, the observed exception is an invalid CSR matrix/vector read,
not a reported `dmumps_c` error. The missing call stack means we do not yet
know which caller supplied the bad matrix/vector pair. Candidate locations are
the first post-solve residual multiply, a transient matrix multiply, or the
cell-centred filter on the following assembly. Solve and phase boundary
markers are still needed to distinguish them.

The already-built `mumps_smoke_check` also exits with `0xc0000005` on a single
rank, both with `MKL_BLACS_MPI` unset and explicitly set to `MSMPI`. This is a
small independent MFEM matrix/MUMPS program, so the full vibroacoustic matrix,
Newmark implementation, optimizer, and UI are not required to reproduce the
native crash. The smoke binary currently has no flushed phase markers, so it
has not yet proved whether it dies in its preliminary Hypre matrix multiply,
MUMPS factorization/solve, or MUMPS destruction.

The build is `Release` and has no application PDB. Windows retained the WER
metadata but not a readable minidump, so the machine-code offset is the
strongest stack evidence available from this run.

## Observed failure

The previously captured one-rank run completed the empty-duct MUMPS transient,
assembled the designed system, and then failed during designed numerical
factorization:

```text
On return from DMUMPS, INFOG(1)= -53
On return from DMUMPS, INFOG(2)= 0
MFEM abort: Error during MUMPS numerical factorization
```

MUMPS documents error `-53` as reflecting a matrix-structure change between
the analysis and factorization phases. This is not a generic convergence error.
Source: [MUMPS release history](https://mumps-solver.org/index.php?page=dwnld).

The first captured executable was older than the solver source. That is no
longer true for the latest run:

```text
build/parallel-cpu/paper_optimizer_miniapp.exe 2026-08-30 23:23:23
```

The new factor-complete log entries therefore belong to the rebuilt source.

## What MFEM itself does

`build/deps/src/mfem-src/examples/ex25p.cpp` is the only numbered MFEM example
in this checkout using `MUMPSSolver`. Its complete direct-solver seam is:

```cpp
HypreParMatrix *A = Ah.As<ComplexHypreParMatrix>()->GetSystemMatrix();
MUMPSSolver mumps(A->GetComm());
mumps.SetPrintLevel(0);
mumps.SetMatrixSymType(MUMPSSolver::MatType::UNSYMMETRIC);
mumps.SetOperator(*A);
mumps.Mult(B, X);
delete A;
```

MFEM's direct-solver unit test is similarly small:

```cpp
MUMPSSolver mumps(MPI_COMM_WORLD);
mumps.SetPrintLevel(0);
mumps.SetOperator(*A.As<HypreParMatrix>());
mumps.Mult(B, X);
mumps.ArrayMult(BB, XX);
```

Important properties of the upstream pattern:

- one `HypreParMatrix`;
- one `MUMPSSolver`;
- communicator taken from the matrix (example) or known identical world
  communicator (unit test);
- default automatic ordering;
- no symbolic-reordering reuse;
- factor once with `SetOperator`, solve one or more RHS with `Mult`/`ArrayMult`;
- operator remains alive through the solve.

MFEM's `MUMPSSolver::SetOperator` requires a `HypreParMatrix`. With reordering
reuse disabled (the default), every `SetOperator` destroys the previous MUMPS
instance, initializes a new one, performs analysis (`JOB=1`), then numerical
factorization (`JOB=2`). Any negative MUMPS status other than the two handled
workspace cases (`-8`, `-9`) triggers `MFEM_ABORT`.

## Confirmed deviations/hazards in this repository

### 1. MFEM errors abort the whole MPI application

The built MFEM configuration has:

```text
MFEM_USE_EXCEPTIONS = OFF
```

Consequently, `MFEM_ABORT` uses `abort()`/`MPI_Abort()`. The app's C++
`try/catch` cannot catch a MUMPS factorization failure. This explains the
apparently ungraceful force quit; it does not itself explain why factorization
failed.

### 2. The old oneMKL threading mix is removed, but MPI dispatch is unresolved

After forcing `MUMPS_openmp=OFF`, the rebuilt link no longer directly contains
both sequential and Intel-threaded oneMKL libraries. The current executable
loads:

```text
mkl_sequential.3.dll
mkl_scalapack_lp64.2.dll
mkl_blacs_lp64.2.dll
msmpi.dll
```

The BLACS DLL is the generic dispatcher, while the process uses Microsoft MPI.
Intel documents that the generic Windows BLACS dispatcher defaults to Intel MPI
unless `MKL_BLACS_MPI=MSMPI` is set. That variable is currently empty in the
launch environment. The CMake cache is also internally inconsistent:

```text
MKL_MPI=intelmpi
MKL_MPI_WRAPPER_LIB=mkl_blacs_mpich_lp64
MPI_GUESS_LIBRARY_NAME=MSMPI
```

An Intel report describes this exact wrong-BLACS selection with MS-MPI and
confirms `MKL_BLACS_MPI=MSMPI` as the fix. Explicitly setting the variable did
not stop the tiny smoke test's access violation, so this is a real build/runtime
defect but not yet proven to be the only crash cause.

Sources:

- [Intel: Using oneMKL DLLs](https://www.intel.com/content/www/us/en/docs/onemkl/developer-guide-windows/2026-0/using-dlls.html)
- [Intel: static libraries and MS-MPI BLACS](https://www.intel.com/content/www/us/en/docs/onemkl/developer-guide-windows/2026-0/static-libraries-in-the-lib-directory.html)
- [Intel Community: wrong BLACS DLL with MS-MPI](https://community.intel.com/t5/Intel-oneAPI-Math-Kernel-Library/Bug-MKL-trying-to-load-incorrect-blacs-DLL-when-using-MS-MPI/td-p/1719902)

### 3. Our runtime seam is more complex than MFEM's example

`ParallelState` owns two long-lived MUMPS instances:

```cpp
std::unique_ptr<MUMPSSolver> initial_mumps;
std::unique_ptr<MUMPSSolver> effective_mumps;
```

Each analysis factors both the initial mass system and the Newmark effective
system. This is legal in principle, but it differs from MFEM's proven seam,
retains two direct-solver factorizations simultaneously, and approximately
doubles the largest MUMPS memory component. It is unnecessary: Newmark uses
the initial matrix once, then the effective matrix for every timestep; the
adjoint uses the effective transpose solves before the final initial transpose
solve. One MUMPS object can therefore be refactored sequentially.

### 4. Forced PORD has been removed

The rebuilt source now matches MFEM's example and unit test by leaving MUMPS on
automatic ordering. Both latest reference factorizations complete, so forced
PORD is no longer part of the observed failure.

### 5. Symbolic reuse was hazardous and is now absent from source

An older implementation enabled:

```cpp
SetReorderingReuse(true)
```

That is invalid across the empty and designed duct matrices because implicit
interface coupling changes the sparse graph. Current source no longer calls it.
Because `-53` means analysis/factorization structure mismatch, runtime evidence
must confirm that the rebuilt executable really contains the no-reuse path.

### 6. One-rank failure narrows the immediate cause

The captured `-53` occurred with one MPI rank. Therefore rank-divergent worker
commands are not the immediate explanation for that run. Collective failure
agreement remains important for multi-rank robustness, but should not distract
from the matrix/build/lifecycle failure reproduced at one rank.

### 7. Worker-command return values are still discarded

`parallelWorkerLoop()` invokes `setMesh(true)`, `assembleSolutionSpace(true)`,
`solve(true)`, and differentiation without checking their returned values.
Most current internal gates are collective, but an unexpected exception or an
unsynchronized early return can still let worker ranks leave the expected MPI
call sequence. This is a multi-rank shutdown/hang hazard, not the leading
one-rank `-53` cause.

### 8. A cumulative memory leak is not the leading explanation

A reboot is a useful sanity check for stale MPI/oneMKL process state. However,
the evidence does not currently look like exhaustion from accumulated solver
histories:

- Windows reports an access violation, not allocation failure or out-of-memory;
- the exact instruction is an indexed CSR read;
- the tiny smoke executable also crashes immediately;
- the machine report shows 32 GB RAM;
- mumps-superbuild 5.9.1 includes the earlier Windows/oneAPI `/heap-arrays`
  memory-leak correction released in 5.9.0.3.

A reboot result should still be recorded because it can expose stale runtime
state, but a repeat after reboot would rule out that explanation quickly.

Sources:

- [mumps-superbuild releases](https://github.com/scivision/mumps-superbuild/releases)
- [Microsoft: access violations are invalid memory accesses](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/handling-exceptions)

### 9. No matching Stack Overflow report was found

Searches for MFEM `MUMPSSolver`, `DMUMPS`, Windows `0xc0000005`, distributed
RHS, `INFO(23)`, and `RedistributeSol` found no matching Stack Overflow case.
The closest useful public report is Intel's confirmed MS-MPI/BLACS dispatcher
issue above. The MUMPS user guide confirms the distributed-RHS contract used by
MFEM: `RHS_loc` supplies local values, `IRHS_loc` maps them to one-based global
rows, and `INFO(23)` sizes the distributed solution buffers.

Sources:

- [MUMPS 5.8.2 user guide](https://mumps-solver.org/doc/userguide_5.8.2.pdf)
- [MFEM 4.8 MUMPS wrapper source](https://docs.mfem.org/4.8/mumps_8cpp_source.html)

## Things that currently look correct

- The monolithic systems passed to MUMPS are `HypreParMatrix` objects.
- The matrix type is correctly declared `UNSYMMETRIC` for the coupled system.
- `SetOperator()` is called once per matrix and `Mult()` reuses the numerical
  factorization through the Newmark timesteps.
- The initial/effective matrices remain alive during their forward solve.
- Current source does not call `SetReorderingReuse(true)`.
- MUMPS 5.9.1 is built as static libraries; the executable is not accidentally
  loading a different `dmumps.dll` from `PATH`.
- The oneMKL interface is LP64 throughout; no visible LP64/ILP64 mix was found.

## Ranked diagnostic plan

1. **Reboot hypothesis ruled out.** The same failure remains after a full
   restart, so stale process state and accumulated leaked memory are not the
   cause.
2. **Add flushed phase markers to the tiny smoke check.** Mark MPI init,
   preliminary Hypre multiply, `SetOperator`, forward `Mult`, transpose
   `Mult`, MUMPS destruction, and `MPI_Finalize`. This is the shortest route to
   the exact failing API boundary.
3. **Set the Windows BLACS backend explicitly.** Ensure every MUMPS launch sees
   `MKL_BLACS_MPI=MSMPI`, or link the MS-MPI-specific BLACS layer. Re-run the
   smoke check before the application.
4. **Prefer the smaller dependency seam if BLACS remains unstable.** Build
   modern MUMPS with `MUMPS_scalapack=OFF`/`NOSCALAPACK` on Windows. MUMPS
   remains MPI-capable while removing oneMKL ScaLAPACK/BLACS from the process.
5. **Match MFEM's MUMPS seam.** Use automatic ordering and one local
   `MUMPSSolver` whose communicator is taken from the exact matrix being
   factored. Factor/solve the initial system, then refactor/solve the effective
   system. Keep it local to the analysis rather than retaining two solver
   instances in `ParallelState`.
6. **Prove the dependency independently.** Run the existing
   `mumps_smoke_check` at one and two ranks. Also run MFEM's own `ex25p` MUMPS
   case from this exact build. If either fails, stop changing solver code: the
   dependency/link stack is broken.
7. **Prove our two matrices independently.** Add a diagnostic check that
   assembles the empty/design initial/effective matrices and factors them one
   at a time with the exact upstream five-line pattern. Log global size, local
   nonzeros, matrix communicator/ranks, and which of the four factorizations
   fails. Do not run Newmark in this check.
8. **Only then restore the transient.** Reuse the single effective
   factorization for all Newmark RHS vectors and verify residuals against the
   existing FGMRES result.
9. **After one rank passes, harden multi-rank commands.** Check worker return
   values collectively, preserve identical MUMPS call order on every rank, and
   test one/two/four-rank parity plus clean shutdown.

## Evidence still needed

- Whether `MKL_BLACS_MPI=MSMPI` is present in the same shell that launches it.
- Flushed smoke-test stage markers identifying the first incomplete operation.
- The first incomplete `solve_begin`/`solve_complete` marker in the miniapp.
- Results of MFEM `ex25p --mumps-solver -no-vis` from this build tree.
## Confirmed non-MUMPS memory-safety causes

The crash is not specific to MUMPS. Source tracing matched the observed
`mfem::SparseMatrix::AddMult` access violation to this deterministic sequence:

1. `Optimizer::run()` called `setMesh()` and then `solve()` without assembly.
2. `solve()` saved the still-empty `LevelSet::design` vector.
3. Reference assembly created the level-set space and initialized the design.
4. The designed phase restored the saved empty vector.
5. The level-set filter passed that vector to a sparse matrix whose width was
   the full level-set size. Release MFEM omits the debug size assertion and
   read beyond the vector in `x[J[j]]`.

The cut-sensitivity path contained a second independent source of undefined
behavior: an explicit OpenMP loop called shared MFEM finite-element spaces and
elements while this dependency build has `MFEM_THREAD_SAFE=OFF`. Those calls
reuse mutable DOF-transformation and shape scratch storage.

The repair enforces `setMesh() -> assembleSolutionSpace() -> solve()`, adds
production dimension gates around the filter mappings, serializes cut
differentiation, destroys mesh borrowers before replacing the mesh, and clears
stale forward/adjoint histories at reassembly boundaries.

## Confirmed MFEM lifecycle mismatch

The standalone `mumps_smoke_check` contains no application settings, level set,
solver, optimizer, dispatcher, or renderer references. Its object lifetimes and
`MUMPSSolver::SetOperator()`/`Mult()` calls match MFEM's direct-solver tests.
The common difference was process initialization: our parallel entrypoints used
raw `MPI_Init[_thread]()` and never initialized HYPRE, whereas MFEM's examples
initialize both singletons in this order:

```cpp
mfem::Mpi::Init(argc, argv);
mfem::Hypre::Init();
```

`mumps_smoke_check`, `parallel_solver_check`, the paper miniapp, and the MPI app
now use that sequence. Their explicit `MPI_Finalize()` calls were removed so
automatic Solver/MUMPS and HYPRE destruction completes before MFEM finalizes
MPI. This also fixes a definite teardown error in `parallel_solver_check`, where
its stack `Solver` previously survived beyond `MPI_Finalize()`.

The generated dependency cache exposed a second independent mismatch:
`MPI_GUESS_LIBRARY_NAME=MSMPI`, but forced ScaLAPACK selected Intel-MPI/MPICH
BLACS (`MKL_MPI=intelmpi`, `mkl_blacs_mpich_lp64`). MUMPS 5.9 supports parallel
operation with `NOSCALAPACK`, so the project now sets `MUMPS_scalapack=OFF` and
`setup.bat` no longer requires the ScaLAPACK/BLACS libraries. This preserves
parallel MUMPS while removing the mismatched MPI layer.

No MUMPS executable was run after these changes. The next runtime check must be
performed from a reconfigured/rebuilt parallel dependency tree so the old
ScaLAPACK cache and binaries are not mistaken for the repaired configuration.

## Resolved crash address: HYPRE OpenMP transpose

The reported Windows fault RVA `0x8BCCF8` resolves to HYPRE's
`hypre_CSRMatrixTransposeHost`, at the reverse-fill write in
`seq_mv/csr_matop.c`. The call occurs while
`ParBilinearForm::ParallelAssemble()` forms `P^T A P`, before the smoke check
constructs its `MUMPSSolver`. This is why the MUMPS phase log could remain
empty: the executable had not reached MUMPS.

The built smoke artifact also loaded both Microsoft's `VCOMP140.DLL` and LLVM's
`libomp140.x86_64.dll`, while its HYPRE transpose used LLVM OpenMP entrypoints.
That stale mixed-runtime binary is unsafe. The project has no remaining OpenMP
application loops, so the minimum repair is to disable `HYPRE_ENABLE_OPENMP`
and `MFEM_USE_OPENMP`, remove direct OpenMP target links, and let MPI/MUMPS
provide operation-level parallelism. The edited smoke source guarantees its
stale object is rebuilt on the next ordinary Ninja build.

## 2026-08-31 application ownership repair

The multi-rank MUMPS smoke check now passes, so the dependency stack is no
longer the working diagnosis. The application-side refactor removes the
remaining unsafe orchestration patterns:

- `LevelSet` is persistent plain data; Solver alone owns every MFEM object that
  borrows a mesh or finite-element space.
- The only legal order is
  `setMesh -> assembleSolutionSpace -> solve -> differentiate`, guarded by
  explicit stage readiness.
- Rank zero broadcasts one command per stage; workers execute the matching
  local stage without recursion or nested command suppression.
- The empty-duct reference has solver-local matrices and never zeroes or
  restores the shared design.
- MUMPS objects are released collectively during explicit worker shutdown or
  reconfiguration, not implicitly during `Solver` destruction.
- Optimizer is the only runtime writer of design candidates, Dispatcher owns
  one future, and Renderer reads only atomic or immutable published results
  while work is active.

This pass was intentionally source-only. Multi-rank runtime correctness still
has to be established with the verification ladder after the user rebuilds.
