# Vibroacoustic CutFEM Quick Reference

Paper-to-MFEM assembly notes for the immersed-boundary vibroacoustic filter.
Target: MFEM 4.9, 2D plane-stress duct first. 3D and Floquet-Bloch unit-cell
work are later extensions.

## 0. FEM Words You Need

| Word | Meaning in this code |
|---|---|
| Field | A physical unknown over the mesh, e.g. displacement $u(x)$, pressure $p(x)$, or level set $\phi(x)$. |
| Basis function | A local interpolation shape $\psi_i(x)$. The finite element field is a weighted sum of these. |
| Trial function | The unknown field being solved for inside a weak-form term. In matrix notation this is the column index. |
| Test function | The function multiplied against the governing equation before integration. In matrix notation this is the row index. |
| DOF | Degree of freedom: one stored unknown coefficient. |
| True DOF | MFEM's global conforming DOF after constraints/identifications. Use these for linear algebra. |
| Coefficient | A callable scalar/vector/tensor value at quadrature points, e.g. density, $1/K_a$, or $\phi_h(x)$. |
| Quadrature rule | A list of integration points and weights used to approximate integrals. |
| Local element matrix | A small dense matrix for one element, stored as `mfem::DenseMatrix`. |
| Global sparse matrix | The assembled matrix over all DOFs, stored as `mfem::SparseMatrix`. |
| Block matrix | A matrix built from submatrices, e.g. $M$, $C$, and $K$ over $[u,p]^T$, stored as `mfem::BlockMatrix`. |

Two indexing facts matter:

$$
p_h(x)=\sum_j P_j\psi_j(x),\qquad
u_h(x)=\sum_j U_j\Psi_j(x).
$$

For a bilinear form $a(\cdot,\cdot)$, MFEM assembles:

$$
A_{ij}=a(\text{trial basis }j,\text{ test basis }i).
$$

## 1. Inputs In Logical Order

Start from app-level data, not MFEM objects:

| Input group | Typical C++ owner | Needed values |
|---|---|---|
| Geometry | renderer/app settings | `lx`, `ly`, optional `lz`, `nx`, `ny`, optional `nz`, boundary labels |
| Level set | shared `LevelSet` | `design`, filtered physical `phi`, `origin`, `spacing`, grid size |
| Acoustic material | physics settings | `rho_a`, `c_a`, `K_a = rho_a*c_a*c_a` |
| Solid material | physics settings | `E`, `nu`, `rho_s`, plane-stress flag |
| CutFEM | solver settings | `epsilon_f`, `level_set_order`, `cut_integration_order` |
| Time run | solver settings | `dt`, final time `T`, Newmark beta/gamma |
| Source | run settings | white-noise seed, `p_in[n]`, `dot_p_in[n]` |
| Optimization | optimizer settings | filter radius `r`, pass band, stop band, stop target `b` |

Paper reproduction defaults:

| Parameter | Value |
|---|---|
| Element type | Q4, MFEM `mfem::H1_FECollection(1, 2)` |
| Element edge length | `h_e = 2e-3 m` |
| Total time | `T = 0.02 s` |
| Time step | `dt = 2e-5 s` |
| Air speed | `c_a = 343 m/s` |
| Air density | `rho_a = 1.21 kg/m^3` |
| Solid Young modulus | `E = 50e6 Pa` |
| Solid Poisson ratio | `nu = 0.4` |
| Solid density | `rho_s = 1000 kg/m^3` |
| Fictitious contrast | `epsilon_f = 1e-8` |
| Filter radius | `r = 8e-3 m` |
| Rayleigh damping ratio | `zeta = 0.1` |
| Rayleigh frequencies | `omega_1 = 1600*2*pi`, `omega_2 = 2200*2*pi` |
| Pass band example | `1000 Hz <= f <= 2500 Hz` |
| Stop band example | `2500 Hz < f <= 4000 Hz` |
| Stop target examples | `b = 1e-2`, `1e-3`, `1e-4` |

## 2. Mesh And Boundary Labels

The fixed background mesh is the duct $\Omega_h$. The level set cuts this mesh;
the mesh itself is not remeshed during optimization.

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
    nx, ny, mfem::Element::QUADRILATERAL, true, lx, ly);
```

Use $h_e=lx/nx=ly/ny$ for the paper-style uniform mesh.

Default duct convention:

| Boundary | Geometric test | Meaning |
|---|---|---|
| Inlet | $x=0$ | absorbing boundary plus incident white-noise source |
| Outlet | $x=L_x$ | absorbing boundary plus pressure measurement |
| Hard walls | $y=0$ and $y=L_y$ | acoustic hard wall |
| Clamp | configured structural exterior segments | essential displacement condition |
| Interface | $\phi=0$ inside elements | acoustic-structure coupling, not a mesh boundary |

Implementation rule: classify `mesh.bdr_attributes` by boundary element centers.
Do not assume a boundary attribute number means "left" or "right" until you
check its coordinates.

## 3. Fields, Spaces, And Level-Set Adapter

The level-set sign convention is:

$$
\phi(x)>0:\Omega_s,\qquad \phi(x)=0:\Gamma_{as},\qquad
\phi(x)<0:\Omega_a.
$$

MFEM spaces:

```cpp
mfem::H1_FECollection fec(1, mesh.Dimension());

mfem::FiniteElementSpace pressure_fes(&mesh, &fec);
mfem::FiniteElementSpace level_set_fes(&mesh, &fec);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &fec, mesh.Dimension(), mfem::Ordering::byVDIM);

mfem::GridFunction phi_h(&level_set_fes);
mfem::GridFunction pressure(&pressure_fes);
mfem::GridFunction displacement(&displacement_fes);
```

Data path from the shared app structure into cut integration:

```text
LevelSet::phi
  -> mfem::GridFunction phi_h
  -> mfem::GridFunctionCoefficient phi_coeff
  -> mfem::AlgoimIntegrationRules
  -> solid volume, acoustic volume, and interface quadrature
```

Key types:

| C++ type | What it stores |
|---|---|
| `mfem::Mesh` | vertices, elements, exterior boundary attributes |
| `mfem::H1_FECollection` | basis family and polynomial order |
| `mfem::FiniteElementSpace` | global DOF layout for one field |
| `mfem::GridFunction` | field values in MFEM DOF order |
| `mfem::GridFunctionCoefficient` | evaluator for a `GridFunction` at quadrature points |

Lifetime rule: `mfem::Mesh` and `mfem::H1_FECollection` must outlive all spaces;
spaces must outlive their `mfem::GridFunction`s and coefficients.

## 4. Strong Form

Structural displacement:

$$
\rho_s(x)\ddot u-\nabla\cdot\sigma
+\alpha_d\rho_s(x)\dot u-\nabla\cdot(\beta_d\dot\sigma)=0
\quad\text{in }\Omega_h.
$$

Structural boundaries:

$$
u=0\text{ on }\Gamma_{sd},\qquad
\sigma n_s=0\text{ on }\Gamma_{sn},\qquad
\sigma n_s=p n_a\text{ on }\Gamma_{as}.
$$

Paper 2D plane-stress kinematics:

$$
\epsilon(u)=
\begin{bmatrix}
\partial u_1/\partial x\\
\partial u_2/\partial y\\
\partial u_1/\partial y+\partial u_2/\partial x
\end{bmatrix},
\qquad
\sigma=\mathcal D(E_s,\nu)\epsilon.
$$

Plane-stress material matrix:

$$
\mathcal D(E,\nu)=\frac{E}{1-\nu^2}
\begin{bmatrix}
1&\nu&0\\
\nu&1&0\\
0&0&(1-\nu)/2
\end{bmatrix}.
$$

MFEM coefficient choice for the paper:

$$
\lambda_{ps}=\frac{E\nu}{1-\nu^2},\qquad
\mu=\frac{E}{2(1+\nu)}.
$$

Use `mfem::ElasticityIntegrator(lambda, mu)` with those plane-stress values.

Acoustic pressure:

$$
\frac{1}{K_a(x)}\ddot p-\frac{1}{\rho_a(x)}\nabla^2p=0
\quad\text{in }\Omega_h.
$$

Acoustic boundaries:

$$
n_a\cdot\nabla p=0\text{ on }\Gamma_{ad},
$$

$$
n_a\cdot\nabla p
=\rho_a\frac{\partial^2(n_s\cdot u)}{\partial t^2}
\text{ on }\Gamma_{as},
$$

$$
n_a\cdot\nabla p+\frac{1}{c_a}\dot p
=\frac{2}{c_a}\dot p_{in}
\text{ on }\Gamma_{ar,in}.
$$

Fictitious material scaling:

$$
\alpha_s=1\text{ in }\Omega_s,\quad \alpha_s=\epsilon_f\text{ in }\Omega_a,
$$

$$
\alpha_a=1\text{ in }\Omega_a,\quad \alpha_a=\epsilon_f\text{ in }\Omega_s.
$$

## 5. Weak-Form Matrix Types

The semi-discrete coupled system is:

$$
M\ddot v+C\dot v+Kv=h,\qquad
v=\begin{bmatrix}u\\p\end{bmatrix},\qquad
h=\begin{bmatrix}0\\g\end{bmatrix}.
$$

Block layout:

$$
M=\begin{bmatrix}M_{uu}&0\\M_{pu}&M_{pp}\end{bmatrix},\qquad
C=\begin{bmatrix}C_{uu}&0\\0&C_{pp}\end{bmatrix},\qquad
K=\begin{bmatrix}K_{uu}&K_{up}\\0&K_{pp}\end{bmatrix}.
$$

Do not symmetrize this system. `Kup` and `Mpu` represent different physics.

Typed block map:

| Block | Math | C++ owner type | Builder |
|---|---|---|---|
| `Muu` | $\int\rho_s w\cdot u\,d\Omega$ | `std::unique_ptr<mfem::SparseMatrix>` | `mfem::BilinearForm` + `mfem::VectorMassIntegrator` |
| `Kuu` | $\int\epsilon(w):\mathcal D:\epsilon(u)\,d\Omega$ | `std::unique_ptr<mfem::SparseMatrix>` | `mfem::BilinearForm` + `mfem::ElasticityIntegrator` |
| `Cuu` | $\alpha_d M_{uu}+\beta_d K_{uu}$ | `std::unique_ptr<mfem::SparseMatrix>` | sparse matrix addition |
| `Mpp` | $\int(1/K_a)qp\,d\Omega$ | `std::unique_ptr<mfem::SparseMatrix>` | `mfem::BilinearForm` + `mfem::MassIntegrator` |
| `Kpp` | $\int(1/\rho_a)\nabla q\cdot\nabla p\,d\Omega$ | `std::unique_ptr<mfem::SparseMatrix>` | `mfem::BilinearForm` + `mfem::DiffusionIntegrator` |
| `Cpp` | $\int_{\Gamma_{ar}}(1/(\rho_a c_a))qp\,d\Gamma$ | `std::unique_ptr<mfem::SparseMatrix>` | `mfem::BilinearForm` + `mfem::BoundaryMassIntegrator` |
| `Kup` | $-\int_{\Gamma_{as}}(w\cdot n_a)p\,d\Gamma$ | `std::unique_ptr<mfem::SparseMatrix>` | custom cut-surface loop or `mfem::MixedBilinearForm` pattern |
| `Mpu` | $-\int_{\Gamma_{as}}q(n_s\cdot u)\,d\Gamma$ | `std::unique_ptr<mfem::SparseMatrix>` | custom cut-surface loop or `mfem::MixedBilinearForm` pattern |
| `g` | $\int_{\Gamma_{ar,in}}2/(\rho_a c_a)q\dot p_{in}\,d\Gamma$ | `mfem::Vector` | `mfem::LinearForm` or direct boundary vector |
| `h` | $[0,g]^T$ | `mfem::BlockVector` | block copy |
| `M`, `C`, `K` | coupled block matrices | `mfem::BlockMatrix` or owned monolithic `mfem::SparseMatrix` | `SetBlock` or `CreateMonolithic` |
| `Khat` | $K+a_6M+a_3C$ | `std::unique_ptr<mfem::SparseMatrix>` | sparse matrix addition |
| `hhat` | Newmark effective RHS | `mfem::Vector` or `mfem::BlockVector` | vector expression |

Element-local types:

| Type | Use |
|---|---|
| `mfem::DenseMatrix` | local element matrices before insertion into sparse blocks |
| `mfem::Vector` | local shapes, local RHS pieces, global true-DOF vectors |
| `mfem::Array<int>` | element DOF lists, true DOF lists, boundary markers, block offsets |

Recommended sparse ownership pattern:

```cpp
mfem::ConstantCoefficient rho_s_coeff(rho_s);

mfem::BilinearForm muu_form(&displacement_fes);
muu_form.AddDomainIntegrator(new mfem::VectorMassIntegrator(rho_s_coeff));
muu_form.Assemble();
muu_form.Finalize();

std::unique_ptr<mfem::SparseMatrix> Muu(muu_form.LoseMat());
```

`LoseMat()` transfers ownership of the assembled matrix out of the
`mfem::BilinearForm`. Use one owning `std::unique_ptr<mfem::SparseMatrix>` per
block.

Block container pattern:

```cpp
mfem::Array<int> offsets(3);
offsets[0] = 0;
offsets[1] = displacement_fes.GetTrueVSize();
offsets[2] = offsets[1] + pressure_fes.GetTrueVSize();

mfem::BlockMatrix M(offsets);
M.SetBlock(0, 0, Muu.get());
M.SetBlock(1, 0, Mpu.get());
M.SetBlock(1, 1, Mpp.get());
M.Finalize();
```

`mfem::BlockMatrix::SetBlock` stores raw pointers. Keep the owning
`std::unique_ptr<mfem::SparseMatrix>` objects alive longer than the
`mfem::BlockMatrix`. Do not set `owns_blocks` if `std::unique_ptr` owns them.

For a first Newmark implementation, it is often simpler to convert each
`mfem::BlockMatrix` to one monolithic `mfem::SparseMatrix`:

```cpp
std::unique_ptr<mfem::SparseMatrix> M_mono(M.CreateMonolithic());
```

## 6. What Runs And What Only Builds Data

Categories: Builds only, Runs a linear solve, Runs MFEM time stepping, Runs the
paper time stepping.

| Operation | Runs computation? | What it does |
|---|---|---|
| `AddDomainIntegrator` | Builds only | Registers a volume integrator with a form. |
| `AddBoundaryIntegrator` | Builds only | Registers a boundary integrator with a form. |
| `Assemble` | Builds data | Loops elements and accumulates local contributions. |
| `Finalize` | Builds data | Finalizes sparse matrix storage. |
| `SpMat` | Builds nothing | Returns the assembled sparse matrix reference. |
| `LoseMat` | Builds nothing | Transfers matrix ownership out of a form. |
| `SetBlock` | Builds container | Inserts a raw matrix pointer into `mfem::BlockMatrix`. |
| `GetEssentialTrueDofs` | Builds data | Finds true DOFs constrained by a boundary marker. |
| `FormLinearSystem` | Builds data | Creates an eliminated linear system; does not solve it. |
| `RecoverFEMSolution` | Copies data | Maps solved true vector back into FE DOF storage. |
| `UMFPackSolver::Mult` | Runs a solve | Direct sparse solve after `SetOperator`. |
| `GMRESSolver::Mult` | Runs a solve | Iterative nonsymmetric solve after `SetOperator`. |
| `CGSolver::Mult` | Runs a solve | Iterative SPD solve after `SetOperator`; not default for this coupled system. |
| `NewmarkSolver::Init` | Initializes runner | Attaches a `SecondOrderTimeDependentOperator`. |
| `NewmarkSolver::Step` | Runs one time step | Calls the operator's `Mult`/`ImplicitSolve`. |
| `NewmarkSolver::Run` | Runs many steps | Repeatedly calls `Step`. |
| custom Newmark loop | Runs paper method | Forms `Khat`, forms `hhat`, solves, updates rates, stores history. |

## 7. Cut Integration

MFEM/Algoim integrates where its level-set coefficient is positive. With
`phi > 0` as solid:

```cpp
mfem::GridFunctionCoefficient phi_coeff(&phi_h);
mfem::AlgoimIntegrationRules solid_rules(
    cut_order, phi_coeff, level_set_order);
```

For the acoustic side, pass `-phi`:

```cpp
class NegatedCoefficient final : public mfem::Coefficient {
public:
    explicit NegatedCoefficient(mfem::Coefficient &source) : source_(source) {}

    mfem::real_t Eval(mfem::ElementTransformation &T,
                      const mfem::IntegrationPoint &ip) override {
        return -source_.Eval(T, ip);
    }

private:
    mfem::Coefficient &source_;
};
```

Element rule setup:

```cpp
NegatedCoefficient minus_phi(phi_coeff);
mfem::AlgoimIntegrationRules acoustic_rules(
    cut_order, minus_phi, level_set_order);

mfem::IntegrationRule solid_ir;
mfem::IntegrationRule acoustic_ir;
mfem::IntegrationRule interface_ir;
mfem::Vector surface_weights;

solid_rules.GetVolumeIntegrationRule(*T, solid_ir);
acoustic_rules.GetVolumeIntegrationRule(*T, acoustic_ir);
solid_rules.GetSurfaceIntegrationRule(*T, interface_ir);
solid_rules.GetSurfaceWeights(*T, interface_ir, surface_weights);
```

Per element:

1. Assemble structural physical terms on `solid_ir`.
2. Assemble structural fictitious terms on `acoustic_ir` scaled by `epsilon_f`.
3. Assemble acoustic physical terms on `acoustic_ir`.
4. Assemble acoustic fictitious terms on `solid_ir` scaled by `epsilon_f`.
5. Assemble `Kup` and `Mpu` on `interface_ir`.
6. Use `surface_weights[q]` for interface measure.

To reuse a standard integrator inside a custom element loop:

```cpp
mfem::DenseMatrix elmat;
integrator.SetIntRule(&solid_ir);
integrator.AssembleElementMatrix(fe, *T, elmat);
```

Interface normal:

$$
n_a=\frac{\nabla\phi}{\|\nabla\phi\|},\qquad n_s=-n_a.
$$

Compute it from `phi_h.GetGradient(*T, grad_phi)` after setting the integration
point on `T`. Reject or diagnose points where $\|\nabla\phi\|$ is near zero.

## 8. Boundary Conditions And Source

| Condition | Math | MFEM handling |
|---|---|---|
| Structural clamp | $u=0$ on $\Gamma_{sd}$ | `displacement_fes.GetEssentialTrueDofs(clamp_marker, ess_tdofs)` |
| Structural free | $\sigma n_s=0$ on $\Gamma_{sn}$ | natural, no matrix term |
| Acoustic hard wall | $n_a\cdot\nabla p=0$ on $\Gamma_{ad}$ | natural, no matrix term |
| Absorbing inlet/outlet | $(1/c_a)\dot p$ on $\Gamma_{ar}$ | `Cpp` via `mfem::BoundaryMassIntegrator` |
| Incident wave | $(2/c_a)\dot p_{in}$ on inlet only | `g` via `mfem::BoundaryLFIntegrator` |
| Outlet readout | $\int_{\Gamma_{out}}p\,d\Gamma$ | boundary quadrature loop, not a constraint |
| Acoustic-structure interface | $\phi=0$ | custom cut-interface loop |

Essential displacement DOFs:

```cpp
mfem::Array<int> clamp_marker(mesh.bdr_attributes.Max());
clamp_marker = 0;
clamp_marker[clamp_attr - 1] = 1;

mfem::Array<int> ess_u_tdofs;
displacement_fes.GetEssentialTrueDofs(clamp_marker, ess_u_tdofs);
```

White-noise source from the paper:

```cpp
std::mt19937 rng(seed);
std::uniform_real_distribution<mfem::real_t> dist(-1.0, 1.0);

std::vector<mfem::real_t> p_in(num_steps);
for (auto &value : p_in) { value = dist(rng); }
```

Use a fixed finite difference rule for `dot_p_in`:

$$
\dot p_{in}^n\approx\frac{p_{in}^{n+1}-p_{in}^{n-1}}{2\Delta t}.
$$

At time step $n$:

$$
g_i^n=\int_{\Gamma_{ar,in}}
\frac{2}{\rho_a c_a}\dot p_{in}^n\psi_i\,d\Gamma.
$$

Use the same `seed`, `p_in`, `dot_p_in`, `dt`, duration, window, and FFT
normalization for the empty duct and every design evaluation.

## 9. Newmark Time Stepping

The paper uses average-acceleration Newmark:

$$
\tilde\beta=\frac14,\qquad \tilde\gamma=\frac12.
$$

### MFEM Built-In Runner

MFEM can run a second-order ODE if you provide a
`mfem::SecondOrderTimeDependentOperator`:

```cpp
class CoupledOperator final : public mfem::SecondOrderTimeDependentOperator {
public:
    CoupledOperator(int size) : mfem::SecondOrderTimeDependentOperator(size) {}

    void Mult(const mfem::Vector &x, const mfem::Vector &dxdt,
              mfem::Vector &d2xdt2) const override;

    void ImplicitSolve(mfem::real_t fac0, mfem::real_t fac1,
                       const mfem::Vector &x,
                       const mfem::Vector &dxdt,
                       mfem::Vector &d2xdt2) override;
};

CoupledOperator op(total_true_dofs);
mfem::NewmarkSolver newmark(0.25, 0.5);
newmark.Init(op);

mfem::Vector v(total_true_dofs);
mfem::Vector v_dot(total_true_dofs);
mfem::real_t t = 0.0;
mfem::real_t dt = time_step;
newmark.Step(v, v_dot, t, dt);
```

`NewmarkSolver::Step` calls `Mult` for the initial acceleration, then calls
`ImplicitSolve(beta*dt*dt, gamma*dt, ...)`. This path is useful for smoke tests
against MFEM Example 23-style operators.

### Paper-Faithful Runner

For the paper and adjoint, keep an explicit loop. Newmark constants:

$$
\begin{aligned}
a_1&=1-\tilde\gamma/\tilde\beta,&
a_2&=(1-\tilde\gamma/(2\tilde\beta))\Delta t,&
a_3&=\tilde\gamma/(\tilde\beta\Delta t),\\
a_4&=1/(\tilde\beta\Delta t),&
a_5&=1/(2\tilde\beta)-1,&
a_6&=1/(\tilde\beta\Delta t^2).
\end{aligned}
$$

Each step solves:

$$
\widehat K v^n=\widehat h^n,
\qquad
\widehat K=K+a_6M+a_3C.
$$

Effective RHS:

$$
\widehat h^n=h^n
+M(a_4\dot v^{n-1}+a_5\ddot v^{n-1}+a_6v^{n-1})
+C(-a_1\dot v^{n-1}-a_2\ddot v^{n-1}+a_3v^{n-1}).
$$

Recover rates:

$$
\dot v^n=a_1\dot v^{n-1}+a_2\ddot v^{n-1}
+a_3(v^n-v^{n-1}),
$$

$$
\ddot v^n=-a_4\dot v^{n-1}-a_5\ddot v^{n-1}
+a_6(v^n-v^{n-1}).
$$

Typed state:

```cpp
mfem::BlockVector v(offsets);
mfem::BlockVector v_dot(offsets);
mfem::BlockVector v_ddot(offsets);
mfem::BlockVector h(offsets);
mfem::BlockVector hhat(offsets);
```

Sparse solve skeleton:

```cpp
std::unique_ptr<mfem::SparseMatrix> tmp(mfem::Add(1.0, *K_mono, a6, *M_mono));
std::unique_ptr<mfem::SparseMatrix> Khat(mfem::Add(1.0, *tmp, a3, *C_mono));

mfem::GMRESSolver solver;
solver.SetOperator(*Khat);
solver.Mult(hhat, v);
```

Use `mfem::GMRESSolver` as the safe default for the coupled nonsymmetric system.
Use `mfem::CGSolver` only after proving the actual matrix is symmetric positive
definite. Use `mfem::UMFPackSolver` for small serial direct-solver smoke tests
when SuiteSparse is available.

Why keep this loop: it exposes `Khat`, `hhat`, residuals, and every
`v`, `v_dot`, `v_ddot` state needed for the FFT objective and fully discrete
adjoint. MFEM's built-in Newmark hides too much of that bookkeeping.

## 10. Outlet FFT

At every time step:

$$
\widehat p(t_n)=\int_{\Gamma_{out}}p(x,t_n)\,d\Gamma.
$$

This is an exterior boundary quadrature loop over the pressure field. It does
not need Algoim.

Then:

$$
P_m=\operatorname{FFT}\{w_n\widehat p(t_n)\},\qquad
S_m=\frac{|P_m|}{|P_{0,m}|}.
$$

`P0` is the empty-duct response and must use the same mesh, source history,
time step, outlet integral, window, and FFT normalization.

Paper objective examples:

$$
\Phi_1=\sum_{m=n_1}^{n_2}\frac{(S_m-a)^2}{a^2},\qquad a=1,
$$

$$
\Phi_2=\sum_{m=n_3}^{n_4}\frac{(S_m-b)^2}{b^2}.
$$

## 11. Optimizer Filter

Optimizer chain:

```text
LevelSet::design
  -> mapped design s_tilde in [-h_e/2, h_e/2]
  -> PDE-filtered physical phi
  -> LevelSet::phi
  -> mfem::GridFunction phi_h
  -> cut quadrature and physics solve
```

Paper filter:

$$
-r^2\nabla^2\bar s_c+\bar s_c=\tilde s_c,
$$

with homogeneous Neumann boundaries. The paper solves this as a cell-centered
finite-volume filter.

Simple MFEM H1 adaptation:

$$
\int_\Omega \bar s_h w_h\,d\Omega
+r^2\int_\Omega \nabla\bar s_h\cdot\nabla w_h\,d\Omega
=\int_\Omega \tilde s_h w_h\,d\Omega.
$$

```cpp
mfem::ConstantCoefficient one(1.0);
mfem::ConstantCoefficient r2_coeff(r * r);
mfem::GridFunctionCoefficient mapped_design_coeff(&mapped_design_h);

mfem::BilinearForm filter_a(&level_set_fes);
filter_a.AddDomainIntegrator(new mfem::MassIntegrator(one));
filter_a.AddDomainIntegrator(new mfem::DiffusionIntegrator(r2_coeff));

mfem::LinearForm filter_b(&level_set_fes);
filter_b.AddDomainIntegrator(new mfem::DomainLFIntegrator(mapped_design_coeff));
```

Homogeneous Neumann is natural, so no essential marker is needed. This is not
bit-for-bit the paper's finite-volume filter, but it keeps the first working
implementation inside MFEM.

## 12. 3D And Floquet-Bloch Extensions

This is an extension path, not the first validation target. The paper model is
2D plane stress; 3D must be validated separately.

Sources:

| Topic | Source |
|---|---|
| MFEM elasticity weak form and 3D stress component order | https://docs.mfem.org/html/classmfem_1_1ElasticityIntegrator.html |
| MFEM vector/elasticity integrator math | https://mfem.org/bilininteg/ |
| MFEM ordinary periodic meshes, i.e. zero phase shift | https://mfem.org/howto/periodic-boundaries/ |
| MFEM `Mesh::MakePeriodic` and `CreatePeriodicVertexMapping` | https://docs.mfem.org/4.9/classmfem_1_1Mesh.html |
| Floquet periodicity phase relation | https://doc.comsol.com/6.3/doc/com.comsol.help.sme/sme_ug_theory.06.075.html |
| Acoustic-structure coupling principle | https://doc.comsol.com/6.3/doc/com.comsol.help.aco/aco_ug_acousticstructure.07.03.html |
| Existing 3D vibroacoustic cut-element precedent | https://orbit.dtu.dk/en/publications/three-dimensional-vibroacoustic-topology-optimization-of-hearing-/ |

3D structural law:

$$
u=(u_1,u_2,u_3)^T,\qquad
\epsilon(u)=\frac12(\nabla u+\nabla u^T),
$$

$$
\sigma(u)=\lambda\operatorname{tr}(\epsilon(u))I+2\mu\epsilon(u),
$$

$$
\mu=\frac{E}{2(1+\nu)},\qquad
\lambda=\frac{E\nu}{(1+\nu)(1-2\nu)}.
$$

Minimal MFEM 3D mapping:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian3D(
    nx, ny, nz, mfem::Element::HEXAHEDRON, lx, ly, lz);

mfem::H1_FECollection fec(1, 3);
mfem::FiniteElementSpace pressure_fes(&mesh, &fec);
mfem::FiniteElementSpace level_set_fes(&mesh, &fec);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &fec, 3, mfem::Ordering::byVDIM);
```

Floquet-Bloch is for a later unit-cell runner, not the duct runner. For lattice
translation $R$ and Bloch wave vector $k$:

$$
p(x+R,t)=\exp(i k\cdot R)p(x,t),\qquad
u(x+R,t)=\exp(i k\cdot R)u(x,t).
$$

The geometry is periodic without phase shift:

$$
\phi(x+R)=\phi(x).
$$

The $k=0$ case reduces to ordinary periodic DOF identification:

```cpp
mfem::Mesh box = mfem::Mesh::MakeCartesian3D(
    nx, ny, nz, mfem::Element::HEXAHEDRON, lx, ly, lz);

std::vector<mfem::Vector> translations = {
    mfem::Vector({lx, 0.0, 0.0})
};

mfem::Mesh mesh = mfem::Mesh::MakePeriodic(
    box, box.CreatePeriodicVertexMapping(translations));
```

For nonzero $k$, do not use `MakePeriodic` alone. The phase factor is complex,
so the implementation needs complex algebra or a doubled real/imag system plus
explicit phase constraints between paired boundary DOFs.

## 13. Syntax Appendix

| Object/API | Arguments | Role |
|---|---|---|
| `mfem::Mesh::MakeCartesian2D` | element counts, type, lengths | background 2D duct mesh |
| `mfem::Mesh::MakeCartesian3D` | element counts, type, lengths | background 3D mesh |
| `mfem::Mesh::CreatePeriodicVertexMapping` | translations | zero-phase periodic vertex map |
| `mfem::Mesh::MakePeriodic` | mesh, vertex map | zero-phase periodic mesh |
| `mfem::H1_FECollection` | order, dimension | basis family |
| `mfem::FiniteElementSpace` | mesh, basis, optional vector dimension | DOF layout |
| `mfem::GridFunction` | FE space | field values |
| `mfem::ConstantCoefficient` | scalar | constant quadrature value |
| `mfem::FunctionCoefficient` | callback | function quadrature value |
| `mfem::GridFunctionCoefficient` | grid function pointer | evaluates FE field at quadrature points |
| `mfem::BilinearForm` | one FE space | square matrix assembly |
| `mfem::MixedBilinearForm` | trial and test spaces | rectangular matrix assembly |
| `mfem::LinearForm` | test space | RHS/load vector assembly |
| `mfem::DenseMatrix` | rows, columns | local element matrix |
| `mfem::SparseMatrix` | sparse entries | global assembled matrix |
| `mfem::BlockVector` | offsets | coupled vector views |
| `mfem::BlockMatrix` | offsets | coupled block matrix |
| `mfem::IntegrationRule` | points and weights | quadrature rule |
| `mfem::AlgoimIntegrationRules` | cut order, level-set coefficient, level-set order | cut quadrature generator |
| `SetIntRule` | integration rule pointer | force an integrator to use custom quadrature |
| `GetEssentialTrueDofs` | boundary marker, output array | constrained true DOF list |
| `FormLinearSystem` | essential DOFs and FE vectors | eliminated system builder, not solver |
| `RecoverFEMSolution` | solved vector and FE vector | copies solution back to FE DOFs |
| `mfem::NewmarkSolver` | beta, gamma | built-in second-order time-stepper |
| `mfem::SecondOrderTimeDependentOperator` | vector size | operator interface for built-in Newmark |
| `mfem::GMRESSolver` | operator via `SetOperator` | nonsymmetric linear solve |
| `mfem::CGSolver` | operator via `SetOperator` | SPD linear solve only |
| `mfem::UMFPackSolver` | operator via `SetOperator` | serial direct solve if available |
