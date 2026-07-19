# Vibroacoustic CutFEM Quick Reference

Paper-to-MFEM assembly notes for the immersed-boundary vibroacoustic filter.
Target: MFEM 4.9, the 2D plane-stress paper model first, with 3D treated only
as a later extension.

## 1. Geometry And Level Set

The fixed background mesh is the computational duct $\Omega_h$. The level set
defines the physical phases:

$$
\phi(x)>0:\Omega_s,\qquad \phi(x)=0:\Gamma_{as},\qquad
\phi(x)<0:\Omega_a.
$$

Data path:

```text
LevelSet::phi
  -> mfem::GridFunction phi_h
  -> mfem::GridFunctionCoefficient phi_coeff
  -> mfem::AlgoimIntegrationRules
  -> solid volume, acoustic volume, and interface quadrature
```

Use a Cartesian domain with measurable dimensions:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
    nx, ny, mfem::Element::QUADRILATERAL, true, lx, ly);
```

Use $h_e=lx/nx=ly/ny$ for the paper-style uniform mesh. Do not optimize on a
mesh whose physical dimensions, inlet face, and outlet face are implicit.

Default duct convention:

| Boundary | Geometric test | Meaning |
|---|---|---|
| Inlet | $x=0$ | absorbing boundary plus incident white-noise source |
| Outlet | $x=L_x$ | absorbing boundary plus pressure measurement |
| Hard walls | $y=0$ and $y=L_y$ | acoustic hard wall |
| Clamp | configured structural exterior segments | essential displacement condition |
| Interface | $\phi=0$ inside elements | acoustic-structure coupling, not a mesh boundary |

Implementation note: inspect/classify `mesh.bdr_attributes` by boundary element
centers. Do not assume an attribute number means "left" or "right" until it is
checked against coordinates.

## 2. Strong Form

Structural displacement in the full fictitious domain:

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

Paper 2D kinematics:

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

MFEM equivalent:

$$
\lambda_{ps}=\frac{E\nu}{1-\nu^2},\qquad
\mu=\frac{E}{2(1+\nu)}.
$$

Use `mfem::ElasticityIntegrator(lambda, mu)` with those plane-stress values for
the paper reproduction. In 3D, use
$\lambda=E\nu/((1+\nu)(1-2\nu))$ and the same $\mu$.

Acoustic pressure in the full fictitious domain:

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

On the outlet absorbing face, use the same left-hand absorbing term with no
incident source.

Fictitious material scaling:

$$
\alpha_s=1\text{ in }\Omega_s,\quad \alpha_s=\epsilon_f\text{ in }\Omega_a,
$$

$$
\alpha_a=1\text{ in }\Omega_a,\quad \alpha_a=\epsilon_f\text{ in }\Omega_s.
$$

Paper value: $\epsilon_f=10^{-8}$.

## 3. Weak Form And Matrix Blocks

Let $w$ be the displacement test function and $q$ the pressure test function.
The semi-discrete system is:

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

| Block | Paper math | Where | MFEM construction |
|---|---|---|---|
| `Muu` | $\int_{\Omega_h}\rho_s w\cdot u\,d\Omega$ | solid physical, acoustic fictitious | `BilinearForm(displacement_fes)` + `VectorMassIntegrator(rho_s_coeff)` |
| `Kuu` | $\int_{\Omega_h}\epsilon(w):\mathcal D:\epsilon(u)\,d\Omega$ | solid physical, acoustic fictitious | `BilinearForm(displacement_fes)` + `ElasticityIntegrator(lambda, mu)` |
| `Cuu` | $\alpha_d M_{uu}+\beta_d K_{uu}$ | structural damping | sparse matrix linear combination |
| `Mpp` | $\int_{\Omega_h}(1/K_a)qp\,d\Omega$ | acoustic physical, solid fictitious | `BilinearForm(pressure_fes)` + `MassIntegrator(inv_bulk)` |
| `Kpp` | $\int_{\Omega_h}(1/\rho_a)\nabla q\cdot\nabla p\,d\Omega$ | acoustic physical, solid fictitious | `BilinearForm(pressure_fes)` + `DiffusionIntegrator(inv_density)` |
| `Cpp` | $\int_{\Gamma_{ar}}(1/(\rho_a c_a))qp\,d\Gamma$ | inlet and outlet absorbing faces | `BilinearForm(pressure_fes)` + `BoundaryMassIntegrator(absorb_coeff)` |
| `Kup` | $-\int_{\Gamma_{as}}(w\cdot n_a)p\,d\Gamma$ | cut interface | custom rectangular element loop, displacement rows and pressure columns |
| `Mpu` | $-\int_{\Gamma_{as}}q(n_s\cdot u)\,d\Gamma$ | cut interface | custom rectangular element loop, pressure rows and displacement columns |
| `g` | $\int_{\Gamma_{ar,in}}2/(\rho_a c_a)q\dot p_{in}(t_n)\,d\Gamma$ | inlet source only | `LinearForm(pressure_fes)` or direct boundary load vector |
| `h` | $[0,g]^T$ | coupled RHS | `BlockVector` with zero displacement block |
| `M` | block matrix above | global coupled mass | `BlockMatrix` or `BlockOperator` |
| `C` | block matrix above | global coupled damping | `BlockMatrix` or `BlockOperator` |
| `K` | block matrix above | global coupled stiffness | `BlockMatrix` or `BlockOperator` |
| `Khat` | $K+a_6M+a_3C$ | Newmark effective system | sparse/block linear combination |
| `hhat` | $h^n+M(a_4\dot v^{n-1}+a_5\ddot v^{n-1}+a_6v^{n-1})+C(-a_1\dot v^{n-1}-a_2\ddot v^{n-1}+a_3v^{n-1})$ | Newmark RHS | assembled vector expression |

Integrator arguments:

| MFEM syntax | Main argument | Matrix entry |
|---|---|---|
| `MassIntegrator(q)` | scalar coefficient $q(x)$ | $A_{ij}=\int q\psi_j\psi_i\,d\Omega$ |
| `DiffusionIntegrator(q)` | scalar coefficient $q(x)$ | $A_{ij}=\int q\nabla\psi_j\cdot\nabla\psi_i\,d\Omega$ |
| `VectorMassIntegrator(q)` | scalar coefficient $q(x)$ | $A_{ij}=\int q\Psi_j\cdot\Psi_i\,d\Omega$ |
| `ElasticityIntegrator(lambda, mu)` | Lame coefficients | $A_{ij}=\int \lambda\nabla\cdot u_j\nabla\cdot w_i+2\mu\epsilon(u_j):\epsilon(w_i)\,d\Omega$ |
| `BoundaryMassIntegrator(q)` | boundary coefficient $q(x)$ | $A_{ij}=\int_\Gamma q\psi_j\psi_i\,d\Gamma$ |
| `BoundaryLFIntegrator(q)` | boundary coefficient $q(x,t_n)$ | $b_i=\int_\Gamma q\psi_i\,d\Gamma$ |

## 4. MFEM Spaces And Fields

Use one scalar H1 space for pressure and level set, and one vector H1 space for
displacement:

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

Mathematical meaning:

$$
p_h(x)=\sum_j P_j\psi_j(x),\qquad
\phi_h(x)=\sum_j\Phi_j\psi_j(x),\qquad
u_h(x)=\sum_j U_j\Psi_j(x).
$$

Arguments:

| Object | Arguments | Meaning |
|---|---|---|
| `H1_FECollection(1, dim)` | order, dimension | Q1/Q4 in 2D, Q1/Q8 in 3D |
| `FiniteElementSpace(&mesh, &fec)` | mesh, basis | scalar global DOF layout |
| `FiniteElementSpace(&mesh, &fec, dim, byVDIM)` | mesh, basis, vector dimension, ordering | vector displacement DOF layout |
| `GridFunction(&fes)` | owning FE space | values stored in MFEM DOF order |
| `GridFunctionCoefficient(&phi_h)` | grid function pointer | evaluator for $\phi_h(x)$ at quadrature points |

Lifetime rule: `Mesh` outlives `FiniteElementSpace`; `H1_FECollection` outlives
`FiniteElementSpace`; `FiniteElementSpace` outlives `GridFunction`.

## 5. Cut Integration Recipe

MFEM/Algoim integrates where its level-set coefficient is positive. With
`phi > 0` as solid:

```cpp
mfem::GridFunctionCoefficient phi_coeff(&phi_h);
mfem::AlgoimIntegrationRules solid_rules(cut_order, phi_coeff, level_set_order);
```

For the acoustic side, pass `-phi` through a tiny coefficient wrapper:

```cpp
class NegatedCoefficient final : public mfem::Coefficient {
public:
    explicit NegatedCoefficient(mfem::Coefficient &source) : source(source) {}

    mfem::real_t Eval(mfem::ElementTransformation &T,
                      const mfem::IntegrationPoint &ip) override {
        return -source.Eval(T, ip);
    }

private:
    mfem::Coefficient &source;
};
```

Then build element rules:

```cpp
NegatedCoefficient minus_phi(phi_coeff);
mfem::AlgoimIntegrationRules acoustic_rules(cut_order, minus_phi, level_set_order);

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

If reusing standard MFEM integrators inside a direct element loop, call:

```cpp
integrator.SetIntRule(&solid_ir);
integrator.AssembleElementMatrix(fe, *T, elmat);
```

`SetIntRule` overrides the default quadrature rule used by the integrator.

Interface normal:

$$
n_a=\frac{\nabla\phi}{\|\nabla\phi\|},\qquad n_s=-n_a.
$$

Compute it from `phi_h.GetGradient(*T, grad_phi)` after setting the integration
point on `T`. Reject or diagnose points where $\|\nabla\phi\|$ is near zero.

## 6. Boundary Conditions

Exterior boundary conditions:

| Condition | Math | MFEM handling |
|---|---|---|
| Structural clamp | $u=0$ on $\Gamma_{sd}$ | `displacement_fes.GetEssentialTrueDofs(clamp_marker, ess_tdofs)` |
| Structural free | $\sigma n_s=0$ on $\Gamma_{sn}$ | natural, no matrix term |
| Acoustic hard wall | $n_a\cdot\nabla p=0$ on $\Gamma_{ad}$ | natural, no matrix term |
| Absorbing inlet/outlet | $(1/c_a)\dot p$ on $\Gamma_{ar}$ | `Cpp` via `BoundaryMassIntegrator(1/(rho_a*c_a))` |
| Incident wave | $(2/c_a)\dot p_{in}$ on inlet only | `g` via `BoundaryLFIntegrator(2*dot_p_in/(rho_a*c_a))` |
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

Apply structural elimination consistently to `M`, `C`, `K`, all coupled blocks,
and RHS vectors before solving.

## 7. White-Noise Source

The paper uses incoming acoustic white noise at the inlet with random pressure
values between $-1$ Pa and $1$ Pa.

Minimal reproducible source state:

```cpp
std::mt19937 rng(seed);
std::uniform_real_distribution<mfem::real_t> dist(-1.0, 1.0);

std::vector<mfem::real_t> p_in(num_steps);
for (auto &value : p_in) { value = dist(rng); }
```

Build `dot_p_in` with a fixed finite difference rule, for example central
interior differences and one-sided end differences:

$$
\dot p_{in}^n\approx\frac{p_{in}^{n+1}-p_{in}^{n-1}}{2\Delta t}.
$$

At time step $n$, assemble:

$$
g_i^n=\int_{\Gamma_{ar,in}}
\frac{2}{\rho_a c_a}\dot p_{in}^n\psi_i\,d\Gamma.
$$

Use the same `seed`, `p_in`, `dot_p_in`, `dt`, duration, window, and FFT
normalization for the empty duct and every design evaluation.

## 8. Newmark Runner

Use average-acceleration Newmark:

$$
\tilde\beta=\frac14,\qquad \tilde\gamma=\frac12.
$$

Constants:

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

Build block offsets:

```cpp
mfem::Array<int> offsets(3);
offsets[0] = 0;
offsets[1] = displacement_fes.GetTrueVSize();
offsets[2] = offsets[1] + pressure_fes.GetTrueVSize();

mfem::BlockVector v(offsets);
mfem::BlockVector v_dot(offsets);
mfem::BlockVector v_ddot(offsets);
```

Each step solves:

$$
\widehat K v^n=\widehat h^n,
\qquad
\widehat K=K+a_6M+a_3C,
$$

$$
\widehat h^n=h^n
+M(a_4\dot v^{n-1}+a_5\ddot v^{n-1}+a_6v^{n-1})
+C(-a_1\dot v^{n-1}-a_2\ddot v^{n-1}+a_3v^{n-1}).
$$

Then recover:

$$
\dot v^n=a_1\dot v^{n-1}+a_2\ddot v^{n-1}
+a_3(v^n-v^{n-1}),
$$

$$
\ddot v^n=-a_4\dot v^{n-1}-a_5\ddot v^{n-1}
+a_6(v^n-v^{n-1}).
$$

Initial conditions in the paper are $v^0=0$ and $\dot v^0=0$, then:

$$
M\ddot v^0=h^0.
$$

Runner checklist:

1. Assemble fixed `M`, `C`, `K` for the current design.
2. Apply essential displacement constraints.
3. Build or factor `Khat`.
4. For every time step, assemble `h[n]` from `dot_p_in[n]`.
5. Solve `Khat * v[n] = hhat[n]`.
6. Recover `v_dot[n]` and `v_ddot[n]`.
7. Store `v`, `v_dot`, and `v_ddot` if the discrete adjoint will be used.
8. Integrate outlet pressure after the pressure block is updated.

Because `M` has `Mpu` and `K` has `Kup`, the coupled system is generally not a
plain SPD scalar problem. Do not default to CG unless the assembled operator is
verified symmetric positive definite.

## 9. Outlet FFT And Objective

At every time step:

$$
\widehat p(t_n)=\int_{\Gamma_{out}}p(x,t_n)\,d\Gamma.
$$

This is an exterior boundary quadrature loop using the pressure field. It does
not need Algoim.

Then:

$$
P_m=\operatorname{FFT}\{w_n\widehat p(t_n)\},\qquad
S_m=\frac{|P_m|}{|P_{0,m}|}.
$$

`P0` is the empty-duct response. It must be generated with the same mesh, source
history, time step, final time, outlet integral, window, and FFT normalization.

Paper objective examples:

$$
\Phi_1=\sum_{m=n_1}^{n_2}\frac{(S_m-a)^2}{a^2},\qquad a=1,
$$

$$
\Phi_2=\sum_{m=n_3}^{n_4}\frac{(S_m-b)^2}{b^2}.
$$

Use nonzero `b`; the paper tested `1e-2`, `1e-3`, and `1e-4`.

## 10. PDE Filter And Optimization Data

Optimizer chain:

```text
design s
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
finite-volume filter, with node-to-cell and cell-to-node interpolation around
the PDE solve.

Simple MFEM H1 adaptation:

$$
\int_\Omega \bar s_h w_h\,d\Omega
+r^2\int_\Omega \nabla\bar s_h\cdot\nabla w_h\,d\Omega
=\int_\Omega \tilde s_h w_h\,d\Omega.
$$

MFEM build:

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

Important data distinction:

| Layer | Type | Meaning |
|---|---|---|
| Shared app | `LevelSet::design` | optimizer variables, mutable by optimizer |
| Shared app | `LevelSet::phi` | filtered physical level set, read by solver/exporter |
| Solver adapter | `mfem::GridFunction phi_h` | MFEM DOF copy of `LevelSet::phi` |
| Cut integration | `mfem::GridFunctionCoefficient` | point evaluator of `phi_h` |

## 11. MFEM Syntax Appendix

| Object/API | Arguments | Math role | Data role |
|---|---|---|---|
| `Mesh::MakeCartesian2D(nx, ny, QUADRILATERAL, true, lx, ly)` | element counts, element type, boundary flag, lengths | background domain $\Omega_h$ | vertices, elements, boundary attributes |
| `Mesh::MakeCartesian3D(nx, ny, nz, HEXAHEDRON, lx, ly, lz)` | 3D counts, element type, lengths | 3D background domain | hex mesh storage |
| `H1_FECollection(order, dim)` | polynomial order, dimension | basis family | owns finite element basis definitions |
| `FiniteElementSpace(&mesh, &fec)` | mesh, collection | scalar space for $p_h$ or $\phi_h$ | global scalar DOF map |
| `FiniteElementSpace(&mesh, &fec, dim, byVDIM)` | mesh, collection, vector dimension, ordering | vector space for $u_h$ | global vector DOF map |
| `GridFunction(&fes)` | finite element space | discrete field | DOF vector plus FE-space pointer |
| `ConstantCoefficient(c)` | scalar value | constant $c$ | quadrature evaluator |
| `FunctionCoefficient(f)` | callback | scalar function $f(x)$ | quadrature evaluator |
| `GridFunctionCoefficient(&gf)` | grid function pointer | evaluates FE field $g_h(x)$ | adapter from DOFs to point values |
| `BilinearForm(&fes)` | one FE space | square form $a(\psi_j,\psi_i)$ | owns integrators and assembles sparse matrix |
| `MixedBilinearForm(&trial, &test)` | trial and test spaces | rectangular form | useful shape for `Kup`/`Mpu` if not assembled manually |
| `LinearForm(&fes)` | test space | RHS $\ell(\psi_i)$ | assembles load vector |
| `AddDomainIntegrator(new ...)` | integrator pointer | volume integral | form owns the integrator |
| `AddBoundaryIntegrator(new ..., marker)` | integrator pointer, boundary marker | boundary integral | form owns the integrator |
| `Assemble()` | none | accumulate local forms | fills internal sparse/vector data |
| `Finalize()` | none | finish sparse graph | prepares matrix for algebra use |
| `SpMat()` | none | assembled matrix | returns `SparseMatrix` reference |
| `SetIntRule(&ir)` | integration rule pointer | custom quadrature | forces an integrator to use `ir` |
| `IntegrationRule` | points and weights | quadrature sum | array of `IntegrationPoint` |
| `DenseMatrix` | rows, columns | element matrix $A^e$ | local contribution before insertion |
| `SparseMatrix` | sparse entries | global matrix | algebra object |
| `BlockVector(offsets)` | block offsets | coupled vector $[u,p]^T$ | views into one vector |
| `BlockMatrix(offsets)` | block offsets | block operator | stores block sparse matrices |
| `BlockOperator(offsets)` | block offsets | block operator | applies blocks without necessarily owning sparse matrices |
| `GetEssentialTrueDofs(marker, out)` | boundary marker, output list | Dirichlet DOFs | true-DOF indices for elimination |
| `FormLinearSystem(ess, x, b, A, X, B)` | essential DOFs, FE solution/RHS, outputs | constrained system | builds eliminated linear system |
| `RecoverFEMSolution(X, b, x)` | solved true vector, RHS, FE solution | recover field | maps solved vector back to FE DOFs |
| `AlgoimIntegrationRules(order, coeff, ls_order)` | cut order, level-set coefficient, level-set projection order | cut quadrature | generates volume/surface rules |
| `GetVolumeIntegrationRule(*T, ir)` | element transformation, output rule | integrate positive phase | fills `ir` |
| `GetSurfaceIntegrationRule(*T, ir)` | element transformation, output rule | integrate $\phi=0$ | fills interface rule |
| `GetSurfaceWeights(*T, ir, weights)` | transformation, surface rule, output weights | correct interface measure | metric weights for cut surface |
| `ParaViewDataCollection(name, &mesh)` | collection name, mesh | visualization output | writes mesh and fields |

## 12. Paper Reproduction Defaults

| Parameter | Value |
|---|---|
| Element type | Q4, MFEM `H1_FECollection(1, 2)` |
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
