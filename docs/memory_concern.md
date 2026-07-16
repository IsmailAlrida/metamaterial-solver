## First, separate the three pieces

The paper contains three different numerical jobs that are easy to conflate:

1. **MUMPS answers the physics question:**
   “For this geometry, how do pressure and displacement evolve?”

2. **The discrete adjoint computes the gradient:**
   “Which parts of the geometry helped or hurt the filter response?”

3. **MMA updates the design:**
   “Given that gradient, how far should each design variable move?”

The important finding is:

> **MMA is not the main memory problem.**
> The expensive memory comes from the MUMPS factorization and from retaining the transient solution needed by the adjoint.

Changing MMA to Ipopt, CCSAQ, or another optimizer does not remove the need for that transient information.

---

# 1. What one optimization iteration actually does

Imagine that every node in your design mesh has a dial:

[
s_i\in[0,1].
]

Those dials collectively describe the level-set geometry. The optimizer does not directly draw a solid. It moves perhaps hundreds of thousands or millions of these dials.

For one proposed design, the code performs this pipeline:

```text
design variables s
       |
       v
filter and construct the level-set geometry
       |
       v
assemble M, C, K and the acoustic-structure coupling
       |
       v
factor the effective Newmark matrix with MUMPS
       |
       v
run the forward transient simulation
       |
       v
record outlet pressure versus time
       |
       v
FFT -> broadband filter objective
       |
       v
run the adjoint backward through time
       |
       v
gradient with respect to every design variable
       |
       v
MMA or another optimizer proposes the next design
```

The paper uses approximately

[
N_t=\frac{0.02}{2\times10^{-5}}=1000
]

time steps. At every step, Newmark tracks the coupled state

[
\mathbf v^n=
\begin{bmatrix}
\mathbf u^n\
\mathbf p^n
\end{bmatrix},
]

together with its velocity and acceleration:

[
\mathbf U^n=
\begin{bmatrix}
\mathbf v^n\
\dot{\mathbf v}^n\
\ddot{\mathbf v}^n
\end{bmatrix}.
]

The paper then runs the adjoint backward through these same time steps. Its final gradient expression uses forward states from time (n) and (n-1), so those states must either be retained or regenerated. The authors explicitly identify checkpointing as the lower-memory alternative. 

---

# 2. The “movie” analogy

The easiest mental model is that the forward simulation records a movie.

* One movie frame contains displacement and pressure everywhere.
* The paper produces roughly 1000 frames.
* The adjoint watches the movie backward.
* At reverse frame (n), it needs the original forward frame (n).

You therefore have two choices.

### Store the whole movie

This is fast during the reverse pass, but memory-intensive:

[
M_{\text{history}}
\approx
N_t\times
N_{\text{state per step}}\times
8\ {\rm bytes}.
]

If you store displacement-pressure, velocity and acceleration independently,

[
M_{\text{history}}
\approx
3N_tN_{\text{dof}}\times8.
]

For one million coupled DOFs and 1000 steps:

[
3(1000)(10^6)(8)
================

24\ {\rm GB}.
]

For four million DOFs:

[
96\ {\rm GB}.
]

That is just the forward-state history, before MUMPS, matrices, gradients, communication buffers and cut-element data.

### Store selected frames and recreate the missing ones

For example, store every 50th frame. During the backward adjoint, rerun short forward segments whenever an intermediate frame is needed.

This is **checkpointing**:

```text
More checkpoints:
    more RAM
    less recomputation

Fewer checkpoints:
    less RAM
    more forward re-simulation
```

Checkpointing is the highest-impact memory intervention available here. Modern schedules such as Revolve and CAMS optimize this storage-versus-recomputation tradeoff; PETSc currently exposes both schedule families for discrete-adjoint trajectory storage. ([petsc.org][1])

---

# 3. Why this particular formulation has unusually large frames

In a conventional body-fitted fluid-structure model:

* displacement exists only in the structure;
* pressure exists only in the acoustic region.

This paper instead solves both fields over the whole fixed background mesh.

In 3D, every background node effectively carries:

[
u_x,\quad u_y,\quad u_z,\quad p.
]

Even a node physically located in air still carries structural displacement variables through a very soft fictitious material. A node physically located in solid still carries acoustic pressure through a fictitious rigid acoustic phase.

That fixed-grid strategy avoids remeshing and makes topology changes much easier, but the price is that all four fields exist everywhere. The cut elements themselves do not introduce enriched global DOFs, so they mainly increase quadrature and assembly work rather than global state-vector size. 

---

# 4. The other major memory consumer: MUMPS

At each time step, Newmark requires a solve with the effective matrix

[
\widehat{\mathbf K}
===================

\mathbf K+a_6\mathbf M+a_3\mathbf C.
]

MUMPS first factorizes this sparse matrix. A sparse matrix may initially contain mostly zero entries, but direct factorization creates additional nonzero entries called **fill-in**.

Think of it as follows:

```text
Original matrix:
mostly empty filing cabinet

Factorized matrix:
many formerly empty drawers become occupied
```

In 3D, this fill-in can become much larger than the original FEM matrix. The factorization is often the largest single resident-memory object.

There is one important advantage: within a single design iteration, (\widehat{\mathbf K}) remains constant across the 1000 time steps. You can therefore:

* factor it once;
* reuse the factors for all forward time-step right-hand sides;
* reuse the factors for the transposed adjoint solves.

MFEM’s current MUMPS wrapper supports transpose solves, multiple right-hand sides, symbolic-reordering reuse, several ordering methods and optional block low-rank factorization controls. ([MFEM Code Documentation][2])

Once the optimizer modifies the geometry, the matrix values change, so a new numerical factorization is normally required. However, because the background mesh and matrix graph remain fixed, symbolic ordering reuse may still help.

---

# 5. What is **not** consuming much memory

## The FFT objective

The objective only needs the integrated outlet pressure:

[
\hat p(t)=\int_{\Gamma_{\rm out}}p(t),d\Gamma.
]

That is one scalar, or a small number of outlet values, per time step. You do **not** need to FFT the full 3D state field.

For 1000 time samples, the outlet signal occupies kilobytes, not gigabytes.

The workflow should be:

```cpp
std::vector<double> outlet_pressure(num_steps);

for (int n = 0; n < num_steps; ++n)
{
    SolveNewmarkStep(...);
    outlet_pressure[n] = IntegrateOutletPressure(...);
}

ComputeFFT(outlet_pressure);
```

The FFT is not the memory concern.

## The outer optimizer

With (n_d) design variables and two nonlinear constraints, an MMA implementation generally needs:

* several design vectors;
* previous iterates;
* lower and upper asymptotes;
* objective gradient;
* two constraint gradients;
* temporary arrays.

This is normally (O(n_d)), or (O(mn_d)) with (m=2), which is small relative to storing 1000 coupled physical states.

---

# 6. Two fundamentally different optimization formulations

Before comparing algorithms, there is a more consequential choice.

## A. Reduced-space, or nested optimization

This is what the paper does.

The optimizer controls only

[
\mathbf s\quad\text{and possibly }z.
]

Whenever it requests an objective or gradient, your code completely solves the transient physics.

```text
Optimizer proposes geometry
        ->
physics solve
        ->
objective and adjoint gradient
        ->
optimizer update
```

Advantages:

* optimizer dimension is approximately the number of design variables;
* PDE equations are satisfied at every accepted design;
* works naturally with your MUMPS/Newmark implementation;
* much lower optimizer memory.

Disadvantage:

* every objective or gradient evaluation is expensive.

## B. Full-space, or SAND

SAND means Simultaneous Analysis and Design. The optimizer treats the design variables **and every physical state** as optimization variables.

For your transient case, that would approximately mean

[
\left{
\mathbf s,
\mathbf U^0,
\mathbf U^1,
\dots,
\mathbf U^{N_t}
\right}.
]

This places the entire movie inside the nonlinear optimizer.

Some structural-topology benchmarks found that exact-Hessian interior-point SAND formulations can be robust and can obtain good designs in relatively few major iterations. However, the same studies found substantially greater memory and computational requirements than nested formulations. ([Springer Nature Link][3])

For your 3D transient problem, I would rule SAND out. You already have a large time history and a large direct factorization. Promoting the time history into optimizer variables would move in exactly the wrong direction.

---

# 7. Literature review of outer optimizer strategies

Your mathematical problem is well suited to a particular optimizer structure:

[
\min_{\mathbf s,z} z
]

subject to

[
\Phi_{\rm pass}(\mathbf s)-z\leq0,
]

[
\Phi_{\rm stop}(\mathbf s)-z\leq0,
]

[
0\leq s_i\leq1.
]

That means:

* potentially millions of bounded design variables;
* only two global nonlinear constraints;
* exact first derivatives from the adjoint;
* strongly nonconvex dynamics;
* each new design evaluation is extremely expensive.

## Comparison

| Strategy                     | Strengths for this problem                                                                                             | Weaknesses                                                                          | C++ options                                                            | Verdict                          |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ---------------------------------------------------------------------- | -------------------------------- |
| **MMA**                      | Designed for many variables and few constraints; small optimizer memory; move limits suppress violent geometry changes | Sensitive to scaling and asymptote settings; may oscillate                          | ParOpt, MFEM, TopOpt-in-PETSc, NLopt                                   | Best baseline                    |
| **GCMMA / conservative MMA** | More cautious and globally convergent model updates; useful when local models are inaccurate                           | May request more objective evaluations                                              | NLopt MMA/CCSAQ-style conservative updates; standalone implementations | Strong robustness option         |
| **CCSAQ**                    | Similar separable conservative structure; supports a quadratic/preconditioned local model                              | NLopt implementation is not MPI-distributed                                         | NLopt `LD_CCSAQ`                                                       | Excellent small/medium reference |
| **Interior point**           | Strong treatment of nonlinear constraints and KKT convergence                                                          | More optimizer work and often line-search evaluations; larger internal systems      | HiOp, Ipopt, ParOpt IP, TAO PDIPM                                      | Good second benchmark            |
| **Augmented Lagrangian**     | General constraint handling; inner solver can remain bound-constrained                                                 | Penalty parameters may create difficult scaling; potentially many PDE calls         | PETSc TAO ALMM, NLopt AUGLAG                                           | Useful challenger                |
| **SQP**                      | Often strong local convergence                                                                                         | Conventional dense BFGS/QP implementations are unsuitable at million-variable scale | SNOPT, NLopt SLSQP                                                     | Not my first 3D choice           |
| **Trust region**             | Handles imperfect models or mildly noisy gradients better than aggressive line searches                                | Trial points may require extra physics evaluations                                  | ParOpt trust region, ROL                                               | Valuable if MMA oscillates       |
| **Derivative-free/global**   | No adjoint needed                                                                                                      | Completely impractical for millions of variables and costly PDE evaluations         | NLopt global methods, CMA-ES                                           | Reject for field optimization    |

Topology-optimization benchmark studies comparing OC, MMA, GCMMA, Ipopt and SNOPT have not found one universal winner. GCMMA often improves robustness over ordinary MMA, while exact-Hessian interior-point approaches can achieve strong final optimality but at substantially greater memory and computational cost, especially in SAND form. ([Springer Nature Link][3])

---

# 8. MMA

MMA builds a simple separable approximation to the objective and each constraint. “Separable” means that the approximate subproblem avoids a giant dense Hessian coupling every design variable to every other variable.

It also maintains moving lower and upper asymptotes:

```text
small stable movement -> asymptotes gradually open
oscillation or direction reversal -> asymptotes contract
```

This is particularly appropriate for level-set nodal variables because unrestricted steps could move the interface by multiple elements in one iteration.

The paper uses:

* initial asymptote factor: (0.5);
* contraction: (0.7);
* expansion: (1.2);
* constraint penalty: (1000).

Aage and Lazarov’s parallel MMA framework was specifically developed for large 3D and multiphysics topology optimization, with C++ and distributed data structures. ([Springer Nature Link][4])

### Why MMA remains the natural primary choice

Your problem has the regime MMA likes:

[
n_{\rm design}\gg n_{\rm constraints}.
]

You may have millions of design variables but only two global band-response constraints. The expensive intelligence is already in your adjoint gradient. MMA can turn that information into an update using relatively little additional memory.

---

# 9. NLopt MMA and CCSAQ

NLopt gives you two especially relevant algorithms:

```cpp
nlopt::LD_MMA
nlopt::LD_CCSAQ
```

NLopt’s documentation actually recommends trying CCSAQ before its MMA variant because CCSAQ often behaves better while showing broadly similar convergence rates. CCSAQ also permits a positive-semidefinite user-supplied Hessian approximation as a preconditioner. ([NLopt][5])

Both algorithms perform conservative inner iterations. This has an implementation consequence: your callback may be called repeatedly, sometimes at the same design vector. NLopt documents an `inner_gradients` option and explicitly recommends caching results when the same point is requested again. ([NLopt][5])

A correct wrapper should resemble:

```cpp
struct EvaluationCache
{
    std::vector<double> x;
    double objective;
    std::array<double, 2> constraints;
    std::vector<double> objective_gradient;
    std::array<std::vector<double>, 2> constraint_gradients;
    bool has_values = false;
    bool has_gradients = false;
};

EvaluationResult Evaluate(const std::vector<double>& x, bool need_gradient)
{
    if (SameDesign(x, cache.x))
    {
        if (!need_gradient || cache.has_gradients)
        {
            return FromCache(cache);
        }

        // Reuse the forward result/checkpoints and run only the missing adjoint work.
    }

    // Assemble, factorize, forward solve, FFT, and optionally adjoint.
}
```

### NLopt limitation

NLopt itself is not a distributed-design optimizer. The practical MPI arrangement is:

```text
rank 0:
    owns NLopt and its full design vector

all ranks:
    receive design by MPI_Bcast
    assemble and run distributed MFEM/MUMPS solve
    compute local gradient contribution

rank 0:
    gathers or reduces the gradient
    returns values to NLopt
```

That is perfectly reasonable for smaller tests. A few million doubles are only tens of megabytes on rank 0. It is less elegant at larger scales because the optimizer does not naturally operate on distributed vectors.

**Use NLopt CCSAQ and MMA as reference implementations, not necessarily as the final exascale optimizer.**

---

# 10. ParOpt

ParOpt is the most attractive additional dependency for your case.

It is:

* implemented in C++;
* MPI-parallel;
* designed for distributed design variables;
* frequently used for topology and multimaterial optimization;
* equipped with MMA, interior-point and (l_\infty) trust-region algorithms. ([GitHub][6])

Most importantly, its current MMA options expose:

[
\texttt{mma_init_asymptote_offset}=0.5,
]

[
\texttt{mma_asymptote_contract}=0.7,
]

[
\texttt{mma_asymptote_relax}=1.2,
]

which directly match the paper. ([SMDOGROUP][7])

It also exposes explicit move limits and KKT tolerances. This makes it easier to reproduce the paper initially, then systematically change one parameter at a time.

### Why I prefer it over rank-0 NLopt for production

Your MFEM design vector is already partitioned across MPI ranks. ParOpt can preserve that ownership:

```text
rank 0 owns its local design portion
rank 1 owns its local design portion
...
rank P owns its local design portion
```

No complete design or gradient needs to be permanently centralized.

**ParOpt MMA is my primary recommendation for the production 3D optimizer.**

---

# 11. Important warning about MFEM’s built-in MMA

MFEM 4.9 includes serial and MPI MMA interfaces, and its documentation presents MMA as supporting objectives, inequality constraints, bounds and distributed design variables. ([MFEM Code Documentation][8])

However, the current implementation deserves a code audit before use on a million-variable problem.

In its subproblem data allocation, it allocates:

```cpp
AA = allocArray((nvar + 1) * (nvar + 1));
```

even though the common case with far fewer constraints than design variables subsequently solves a small constraint-space system. It also allocates several (n_{\rm var}n_{\rm con}) arrays, which are acceptable when (n_{\rm con}=2), but the square `AA` array is potentially fatal. ([GitHub][9])

For perspective:

* 20,000 local design variables:

[
20{,}001^2\times8\approx3.0\ {\rm GiB}
]

per rank;

* 50,000 local variables:

[
50{,}001^2\times8\approx18.6\ {\rm GiB}
]

per rank.

The array may not be used in your few-constraint branch, but it is still allocated.

Therefore:

> **Do not use unmodified `mfem::MMA` as your large-3D production optimizer until that allocation is removed or made conditional.**

It remains useful for:

* tiny verification problems;
* comparing update formulas;
* a patched local fork.

MFEM itself is still an excellent choice for your FEM stack. This warning concerns that particular MMA implementation, not MFEM as a whole.

---

# 12. TopOpt-in-PETSc MMA

The DTU TopOpt-in-PETSc repository is highly relevant because it contains a fully parallel C++/PETSc MMA implementation based on Aage and Lazarov’s parallel framework. Its own header identifies it as tailored for parallel performance. ([GitHub][10])

This gives you two possible uses:

1. adopt PETSc as a dependency and integrate its MMA class;
2. study/vendor the distributed MMA implementation into your own optimizer adapter.

Advantages:

* close lineage to the paper’s authors and software ecosystem;
* proven use in large 3D topology optimization;
* C++ and MPI;
* lightweight compared with a general NLP framework.

Disadvantages:

* more of a research-code component than a polished standalone optimizer package;
* its integration API is less general than ParOpt;
* you must carefully test ownership, restart state and convergence criteria.

I would rank it as an excellent reference or vendored option, just behind ParOpt as a clean dependency.

---

# 13. Interior-point methods: HiOp and Ipopt

Interior-point methods solve a sequence of barrier problems and track primal variables, constraint multipliers and complementarity conditions.

They generally give you better explicit measures of:

* feasibility;
* dual feasibility;
* complementarity;
* KKT convergence.

That is useful because the paper stops after a fixed number of iterations, partly due to oscillating objectives. A general nonlinear optimizer may offer a more principled stopping assessment.

## HiOp

MFEM already contains a `HiopNlpOptimizer` interface. HiOp targets large-scale nonlinear optimization and can exploit application-level data parallelism. ([MFEM Code Documentation][11])

Why it is attractive:

* MFEM integration already exists;
* distributed-memory orientation;
* general nonlinear constraints;
* serious HPC implementation.

Why it is not my first choice:

* more internal optimizer machinery than MMA;
* potentially more trial evaluations;
* each trial evaluation can mean another full MUMPS factorization and transient solve;
* less direct correspondence to the paper.

**HiOp is the strongest second production benchmark after ParOpt MMA.**

## Ipopt

Ipopt is a mature open-source C++ interior-point optimizer for general large-scale nonlinear programs. ([coin-or.github.io][12])

It can use limited-memory Hessian approximations, but its standard integration does not naturally align with a distributed MFEM design vector as cleanly as ParOpt or HiOp.

For this problem, I would use Ipopt only in the **reduced-space** form:

```text
optimizer variables:
    design s and epigraph z

not optimizer variables:
    every pressure/displacement value at every time step
```

It is a useful research benchmark, but not the first dependency I would add.

---

# 14. PETSc TAO

TAO provides large-scale optimization algorithms for bound-constrained, generally constrained and PDE-constrained problems using MPI-distributed PETSc vectors. ([petsc.org][13])

Two relevant choices are:

### TAO ALMM

An augmented-Lagrangian method that turns the constrained problem into a sequence of simpler subproblems. ([petsc.org][14])

It is appealing because your two constraints can be included naturally, while the inner subproblem can use a scalable bound-constrained method.

Potential weakness: poor constraint scaling can cause the penalty parameter to grow and make progress slow.

### TAO primal-dual interior point

A more direct general constrained approach, but with a larger KKT system and more optimizer state.

TAO becomes especially attractive if you already add PETSc for:

* distributed vectors;
* checkpointing infrastructure;
* solver diagnostics;
* experimenting with alternate optimization algorithms.

MFEM can coexist with PETSc, but adding PETSc solely for TAO is a larger dependency decision than adding ParOpt.

---

# 15. Trust-region methods

Your objective is not mathematically noisy if the forward and adjoint solves are accurate, but it may behave *as though* it is noisy:

* tiny geometry changes can move resonances;
* cut-element classification can change;
* the filter response is highly nonconvex;
* pass-band and stop-band constraints can oscillate.

Trust-region methods ask:

> “How far do I trust the current local gradient model?”

They shrink the permitted step if actual improvement differs significantly from predicted improvement.

ParOpt includes an (l_\infty) trust-region method, and ROL provides broad C++ support for simulation-based large-scale optimization. ([SMDOGROUP][15])

A trust region may be more robust than MMA when:

* adjoint tolerances are relaxed;
* checkpoint reconstruction introduces numerical discrepancies;
* the design repeatedly flips around an interface;
* MMA asymptotes oscillate.

The downside is that rejected trial steps are particularly expensive here. A rejected point is not a cheap scalar function evaluation; it is another transient MUMPS calculation.

I would keep ParOpt’s trust-region algorithm as a fallback experiment rather than the initial production method.

---

# 16. Approaches I would reject

## NLopt SLSQP for the full 3D design

SLSQP uses BFGS-type second-order approximations and sequential quadratic subproblems. ([NLopt][5])

For modest design dimensions it can be useful. For a distributed million-variable field, it is not the implementation I would choose because the QP and quasi-Newton machinery are not designed around your MPI-distributed topology vector.

## Optimality Criteria

OC is excellent for classical compliance minimization with one volume constraint. Your two broadband response constraints and epigraph variable are considerably more general. MMA is the more natural topology-specific method.

## Finite-difference gradients

One finite-difference gradient would require approximately one complete optimization analysis per design variable:

[
n_d+1
]

forward analyses.

For (n_d=10^6), that is impossible. The discrete adjoint is indispensable.

## Genetic algorithms, CMA-ES, particle swarm and DIRECT

These require many complete designs per generation or iteration. They are appropriate for perhaps 5–100 geometric parameters, not a nodal level-set field with millions of variables.

They could still be used for an outer hyperparameter study involving a handful of values such as:

* filter radius;
* damping ratio;
* target stop-band level;
* initial seed spacing.

They should not optimize the full topology.

---

# 17. The stack I would build

## Core simulation

```text
C++20
MPI
MFEM 4.9
MUMPS
METIS or ParMETIS
PT-Scotch as an alternative ordering
FFTW3
```

MFEM’s MUMPS wrapper lets you test METIS, ParMETIS, Scotch and PT-Scotch reorderings, symbolic-factorization reuse and transpose solves without changing the underlying formulation. ([MFEM Code Documentation][16])

## Optimization

```text
Primary:
    ParOpt MMA

Reference:
    NLopt LD_CCSAQ
    NLopt LD_MMA

Second scalable challenger:
    HiOp through MFEM

Optional research challenger:
    PETSc TAO ALMM
```

## Adjoint memory

```text
Custom fully discrete Newmark adjoint
+
Revolve/CAMS-style checkpoint schedule
+
optional HDF5 or ADIOS2 disk checkpoint backend
```

I would not initially replace the paper’s hand-derived discrete adjoint with a black-box time-adjoint package. The FFT chain rule and exact Newmark discretization are central to the paper’s gradient consistency. PETSc’s TSAdjoint does solve discrete adjoints and provides mature trajectory machinery, but adopting it safely would require verifying that its state definition and time-discretization derivatives exactly reproduce your Newmark residual. ([petsc.org][17])

Use its checkpointing ideas or schedules first; preserve your explicitly derived adjoint equations.

---

# 18. Suggested software architecture

Keep the optimizer completely decoupled from the physics:

```cpp
struct OptimizationEvaluation
{
    double objective;
    std::array<double, 2> constraints;

    mfem::Vector objective_gradient;
    std::array<mfem::Vector, 2> constraint_gradients;
};

class VibroacousticDesignProblem
{
public:
    OptimizationEvaluation Evaluate(
        const mfem::Vector& design,
        bool compute_gradient);

private:
    void FilterDesign();
    void AssembleSystem();
    void FactorizeMumps();

    void ForwardSolve();
    void ComputeOutletFFT();
    void ReverseAdjoint();

    CheckpointManager checkpoints;
    EvaluationCache cache;
};
```

Then write thin adapters:

```text
ParOptAdapter
NLoptAdapter
HiOpAdapter
TaoAdapter
```

This lets every optimizer see exactly the same:

* objective;
* constraints;
* gradients;
* bounds;
* starting point;
* scaling.

Without such an adapter boundary, optimizer comparisons often accidentally compare different physics tolerances or different gradient implementations.

---

# 19. Benchmark the number of physics evaluations, not optimizer iterations

An “iteration” is not comparable across algorithms.

One MMA iteration might use:

* one forward solve;
* one adjoint solve.

An interior-point or conservative method might evaluate several trial points before accepting one.

Record:

1. MUMPS numerical factorizations;
2. forward transient runs;
3. adjoint transient runs;
4. total Newmark right-hand-side solves;
5. checkpoint recomputation steps;
6. peak resident memory per rank;
7. final epigraph value (z);
8. maximum constraint violation;
9. projected-gradient or KKT norm;
10. wall time.

A useful headline metric is:

[
\text{quality achieved per MUMPS factorization}.
]

That is more meaningful than quality per optimizer iteration.

---

# 20. Recommended experiment sequence

### Stage 1: reproduce the paper in 2D

Use:

* ParOpt MMA;
* paper’s (0.5/0.7/1.2) asymptote settings;
* full state storage;
* exact same objective;
* gradient verification against directional finite differences.

### Stage 2: validate checkpointing in 2D

Compare:

* full storage;
* 100 checkpoints;
* 50 checkpoints;
* 20 checkpoints.

Check both:

[
\frac{\left|\nabla\Phi_{\rm checkpoint}
-\nabla\Phi_{\rm full}\right|}
{\left|\nabla\Phi_{\rm full}\right|}
]

and wall time.

### Stage 3: small 3D optimizer comparison

Run:

* ParOpt MMA;
* NLopt CCSAQ;
* NLopt MMA;
* HiOp.

Give every method the same maximum number of MUMPS factorizations rather than the same iteration count.

### Stage 4: scale 3D

Take the best one or two optimizers and vary:

* MPI rank count;
* MUMPS ordering;
* checkpoint count;
* in-memory versus disk checkpoints;
* optional MUMPS BLR tolerance.

BLR is approximate, so verify objective and adjoint-gradient accuracy before using it in production.

---

# My call

Use this as the main build:

```text
MFEM
MUMPS
FFTW
ParOpt MMA
custom fully discrete adjoint
Revolve/CAMS-style checkpoint manager
```

Keep these for comparison:

```text
NLopt CCSAQ
NLopt MMA
HiOp
```

Most importantly:

> **Checkpointing and MUMPS ordering will affect your 3D memory far more than changing the outer optimizer.**

And one practical warning worth acting on immediately:

> **Do not take MFEM’s current built-in MMA straight into a million-variable 3D run without patching or auditing its square local allocation.** ParOpt MMA is the cleaner production starting point.

[1]: https://petsc.org/main/manualpages/TS/TSTrajectoryMemoryType/?utm_source=chatgpt.com "TSTrajectoryMemoryType — PETSc v3.25.3-348-g7e58aa9e17ad1 documentation"
[2]: https://docs.mfem.org/html/classmfem_1_1MUMPSSolver.html "https://docs.mfem.org/html/classmfem_1_1MUMPSSolver.html"
[3]: https://link.springer.com/article/10.1007/s00158-015-1250-z?utm_source=chatgpt.com "Benchmarking optimization solvers for structural topology ... - Springer"
[4]: https://link.springer.com/article/10.1007/s00158-012-0869-2?utm_source=chatgpt.com "Parallel framework for topology optimization using the method of moving ..."
[5]: https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/ "https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/"
[6]: https://github.com/smdogroup/paropt "https://github.com/smdogroup/paropt"
[7]: https://smdogroup.github.io/paropt/options.html "Options and generic interface for ParOpt optimziers — ParOpt  documentation"
[8]: https://docs.mfem.org/html/classmfem_1_1MMA.html "https://docs.mfem.org/html/classmfem_1_1MMA.html"
[9]: https://github.com/mfem/mfem/blob/master/linalg/mma.cpp "mfem/linalg/mma.cpp at master · mfem/mfem · GitHub"
[10]: https://github.com/topopt/TopOpt_in_PETSc/blob/master/ "https://github.com/topopt/TopOpt_in_PETSc/blob/master/"
[11]: https://docs.mfem.org/html/classmfem_1_1HiopNlpOptimizer.html?utm_source=chatgpt.com "MFEM: mfem::HiopNlpOptimizer Class Reference"
[12]: https://coin-or.github.io/Ipopt/?utm_source=chatgpt.com "Ipopt: Documentation - GitHub Pages"
[13]: https://petsc.org/release/manual/tao/ "https://petsc.org/release/manual/tao/"
[14]: https://petsc.org/release/manualpages/Tao/TAOALMM/ "https://petsc.org/release/manualpages/Tao/TAOALMM/"
[15]: https://smdogroup.github.io/paropt/ "https://smdogroup.github.io/paropt/"
[16]: https://docs.mfem.org/html/mumps_8hpp_source.html "https://docs.mfem.org/html/mumps_8hpp_source.html"
[17]: https://petsc.org/release/manualpages/Sensitivity/TSAdjointSolve/?utm_source=chatgpt.com "TSAdjointSolve — PETSc 3.25.3 documentation"
