# MFEM 4.9 CutFEM guide for the vibroacoustic paper

This is the compact implementation path from a fixed MFEM background mesh to
the cut-element transient vibroacoustic problem in Dilgen and Aage's
[2024 paper](https://doi.org/10.1016/j.finel.2024.104123). It uses the notation
reconstructed in [`mfem_building_blocks.md`](mfem_building_blocks.md), but keeps
only the syntax and decisions needed to implement the solver.

MFEM 4.9 is the current release and the version pinned by this repository.
Its relevant shortcut is `mfem::AlgoimIntegrationRules`: it constructs
element-local quadrature for a level-set-positive volume and its zero surface.
It does **not** assemble this paper's two-physics interface terms or their
design derivatives.

## 1. Make the fixed background mesh

The paper never remeshes during optimization. Both physics and the level set
live on one Cartesian Q1 mesh.

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
    nx, ny,
    mfem::Element::QUADRILATERAL,
    true,            // generate edges
    length, height);
```

This represents the fixed domain
\(\Omega=\Omega_s\cup\Omega_a\). `nx * ny` is the number of Q4 elements.
[MFEM documents](https://docs.mfem.org/4.9/classmfem_1_1Mesh.html) that
`MakeCartesian2D` creates `[0,length] x [0,height]`.

The 3D equivalent is:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian3D(
    nx, ny, nz,
    mfem::Element::HEXAHEDRON,
    length, height, depth);
```

Use `HEXAHEDRON`, not `QUADRILATERAL`, in 3D. Start with the paper's 2D
plane-stress problem; 3D multiplies both implementation and validation cost.

Boundary attributes describe only the *outer* duct boundary. The immersed
solid-acoustic interface is not a mesh boundary and will come from
\(\phi=0\). Inspect `mesh.bdr_attributes` before assigning inlet, outlet,
hard-wall, or clamp markers rather than relying on undocumented numbering.

## 2. Create Q1 fields and keep ownership outside the solver

The paper uses continuous Q4 fields for pressure, displacement, and level set.
In MFEM that is an order-one `H1_FECollection`:

```cpp
const int dim = mesh.Dimension();

mfem::H1_FECollection q1(1, dim);
mfem::FiniteElementSpace scalar_fes(&mesh, &q1);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &q1, dim, mfem::Ordering::byVDIM);

mfem::GridFunction phi(&scalar_fes);       // level set
mfem::GridFunction pressure(&scalar_fes);  // acoustic state
mfem::GridFunction displacement(&displacement_fes);
```

The sign convention is paper Eq. 1:

\[
\phi>0:\Omega_s,\qquad \phi=0:\Gamma_{as},\qquad
\phi<0:\Omega_a.
\]

Initialize a test geometry by projection:

```cpp
mfem::FunctionCoefficient initial_phi([](const mfem::Vector &x) {
    const mfem::real_t dx = x[0] - 0.5;
    const mfem::real_t dy = x[1] - 0.5;
    return 0.2 - std::sqrt(dx*dx + dy*dy); // positive inside
});
phi.ProjectCoefficient(initial_phi);
```

The required lifetime order is `Mesh`, then `FiniteElementCollection`, then
`FiniteElementSpace`, then `GridFunction`. Put those in one discretization
owner. Do not make the transient solver own them.

Use these data contracts between stages:

| Meaning | Canonical type | Reason |
|---|---|---|
| Background geometry | `mfem::Mesh` | Connectivity and physical coordinates |
| Mathematical design \(s\) | `mfem::Vector` of true DOFs | Contiguous optimizer input in \([0,1]\) |
| Physical level set \(\bar s=\phi\) | scalar Q1 `mfem::GridFunction` | Directly evaluable by Algoim and exportable |
| Coupled state \([u,p]\) | `mfem::BlockVector` or `mfem::Vector` with offsets | Preserves the paper's block layout |
| \(M,C,K\) | `mfem::Operator`/`mfem::SparseMatrix` blocks | Solver and adjoint need `Mult` and `MultTranspose` |
| Time history | `std::vector<StepState>` | Stores \(v,\dot v,\ddot v\) for the discrete adjoint |

Always transfer optimizer data through true DOFs:

```cpp
mfem::Vector design(scalar_fes.GetTrueVSize());
mfem::Vector phi_true(scalar_fes.GetTrueVSize());

// ...map and filter design into phi_true...
phi.SetFromTrueDofs(phi_true);
```

That remains valid if the mesh later becomes nonconforming or parallel.

## 3. Map optimizer variables to the level set

The paper's design chain is

\[
s\in[0,1]
\longrightarrow \widetilde s\in[-h_e/2,h_e/2]
\longrightarrow \bar s=\phi,
\]

followed by a Helmholtz/PDE smoothing filter. The first map is simply:

```cpp
for (int i = 0; i < design.Size(); ++i) {
    phi_true[i] = element_size * (design[i] - 0.5);
}
```

For the compact MFEM implementation, solve

\[
(M_\phi+r^2K_\phi)\,\bar s=M_\phi\widetilde s.
\]

Build `M_phi` with `mfem::MassIntegrator` and `K_phi` with
`mfem::DiffusionIntegrator`. Homogeneous Neumann conditions are natural, so no
essential boundary marker is needed. This H1 filter is the clean MFEM analogue
of paper Eq. 28; the paper itself uses a cell-centred finite-volume filter.

Keep the map and filter outside the forward solver. The optimizer owns
`design`; the geometry stage produces `phi`; the solver only sees assembled
operators.

## 4. Ask Algoim for the two cut volumes and interface

MFEM's [Example 38](https://mfem.org/examples/) is the starting point. In 4.9,
[`AlgoimIntegrationRules`](https://docs.mfem.org/4.9/classmfem_1_1AlgoimIntegrationRules.html)
provides:

- `GetVolumeIntegrationRule`: quadrature where its coefficient is positive;
- `GetSurfaceIntegrationRule`: quadrature on its zero contour/surface;
- `GetSurfaceWeights`: the extra cut-surface transformation factors.

The repository already builds MFEM with `MFEM_USE_ALGOIM=ON`. It has LAPACK
off, so use Algoim rather than Example 38's moment-fitting path.

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

mfem::GridFunctionCoefficient phi_coefficient(&phi);
NegatedCoefficient minus_phi(phi_coefficient);

const int cut_order = 4; // configurable; prove convergence for the final value
mfem::AlgoimIntegrationRules solid_rules(cut_order, phi_coefficient, 1);
mfem::AlgoimIntegrationRules acoustic_rules(cut_order, minus_phi, 1);
```

Passing level-set projection order `1` preserves the paper's piecewise-linear
zero contour. In each element:

```cpp
mfem::IntegrationRule solid_ir;
mfem::IntegrationRule acoustic_ir;
mfem::IntegrationRule interface_ir;
mfem::Vector surface_weights;

for (int e = 0; e < mesh.GetNE(); ++e) {
    mfem::ElementTransformation *T = mesh.GetElementTransformation(e);

    solid_rules.GetVolumeIntegrationRule(*T, solid_ir);       // phi > 0
    acoustic_rules.GetVolumeIntegrationRule(*T, acoustic_ir); // phi < 0
    solid_rules.GetSurfaceIntegrationRule(*T, interface_ir);  // phi = 0
    solid_rules.GetSurfaceWeights(*T, interface_ir, surface_weights);

    // Assemble this element using the three rules.
}
```

For a volume point, the physical measure is:

```cpp
T->SetIntPoint(&ip);
const mfem::real_t dV = ip.weight * T->Weight();
```

For an interface point, include Algoim's extra factor:

```cpp
T->SetIntPoint(&ip);
const mfem::real_t dGamma =
    ip.weight * surface_weights[q] * T->Weight();
```

Omitting `surface_weights[q]` gives the wrong interface measure on mapped
elements.

The acoustic outward normal follows from the chosen sign convention:

```cpp
mfem::Vector grad_phi(dim);
phi.GetGradient(*T, grad_phi); // T already points at the interface ip
const mfem::real_t norm = grad_phi.Norml2();
MFEM_VERIFY(norm > 1e-12, "Undefined level-set normal");
grad_phi /= norm;

const mfem::Vector &n_acoustic = grad_phi; // negative phi -> positive phi
// n_solid = -n_acoustic
```

## 5. Assemble the paper's spatial operators

The weak terms represented by the code are:

\[
\begin{aligned}
m_{uu}(w,u)&=\int_\Omega \rho_s w\cdot u\,d\Omega,\\
k_{uu}(w,u)&=\int_\Omega \varepsilon(w):C:\varepsilon(u)\,d\Omega,\\
m_{pp}(q,p)&=\int_\Omega K_a^{-1}qp\,d\Omega,\\
k_{pp}(q,p)&=\int_\Omega \rho_a^{-1}\nabla q\cdot\nabla p\,d\Omega,\\
c_{pp}(q,p)&=\int_{\Gamma_{ar}}(\rho_ac_a)^{-1}qp\,d\Gamma,\\
k_{up}(w,p)&=-\int_{\Gamma_{as}}(w\cdot n_a)p\,d\Gamma,\\
m_{pu}(q,u)&=-\int_{\Gamma_{as}}q(n_s\cdot u)\,d\Gamma.
\end{aligned}
\]

Their MFEM building blocks are:

| Matrix | Standard formula to reuse | Quadrature |
|---|---|---|
| `Muu` | `VectorMassIntegrator` | solid and acoustic cut volumes |
| `Kuu` | `ElasticityIntegrator` | solid and acoustic cut volumes |
| `Mpp` | `MassIntegrator` | both cut volumes |
| `Kpp` | `DiffusionIntegrator` | both cut volumes |
| `Cpp` | `BoundaryMassIntegrator` | ordinary exterior boundary |
| `Kup`, `Mpu` | custom rectangular element matrices | cut interface |

For the paper's 2D plane-stress solid, pass

\[
\lambda=\frac{E\nu}{1-\nu^2},\qquad
\mu=\frac{E}{2(1+\nu)}
\]

to `ElasticityIntegrator`:

```cpp
mfem::ConstantCoefficient lambda(E * nu / (1.0 - nu*nu));
mfem::ConstantCoefficient mu(E / (2.0 * (1.0 + nu)));
mfem::ConstantCoefficient rho_solid(rho_s);

mfem::ElasticityIntegrator kuu(lambda, mu);
mfem::VectorMassIntegrator muu(rho_solid);
```

For acoustics, `MassIntegrator` receives `1 / (rho_a*c_a*c_a)` and
`DiffusionIntegrator` receives `1 / rho_a`. Build structural Rayleigh damping
after assembly as `Cuu = alpha_d*Muu + beta_d*Kuu`.

For every volume term, integrate both phases. Use physical coefficients in its
own phase and multiply the same formula by \(\epsilon_f=10^{-8}\) in the
fictitious phase:

\[
\alpha_s=1\text{ in }\phi>0,\quad
\alpha_s=\epsilon_f\text{ in }\phi<0,
\]

with the reverse assignment for acoustics. Keep `epsilon_f` configurable; the
paper warns that equal stiffness and mass scaling can create fictitious modes.

Standard MFEM integrators accept a replaceable integration rule. Reuse their
tested element formulas inside one element loop instead of rewriting mass,
diffusion, or elasticity algebra:

```cpp
const mfem::FiniteElement &u_fe = *displacement_fes.GetFE(e);
const mfem::FiniteElement &p_fe = *scalar_fes.GetFE(e);
mfem::DenseMatrix solid_kuu;
mfem::DenseMatrix solid_muu;

kuu.SetIntRule(&solid_ir);
kuu.AssembleElementMatrix(u_fe, *T, solid_kuu);

muu.SetIntRule(&solid_ir);
muu.AssembleElementMatrix(u_fe, *T, solid_muu);
```

Repeat with `acoustic_ir` and coefficients scaled by `epsilon_f`; do the
opposite phase assignment for acoustic mass and diffusion. Add both phase
matrices into the element matrix before sparse insertion.

Only the immersed interface blocks need new element algebra. With the selected
`byVDIM` ordering, their entries at one interface point are:

```cpp
u_fe.CalcShape(ip, u_shape);
p_fe.CalcShape(ip, p_shape);

for (int c = 0; c < dim; ++c) {
    for (int a = 0; a < u_shape.Size(); ++a) {
        const int ua = c * u_shape.Size() + a;
        for (int b = 0; b < p_shape.Size(); ++b) {
            const mfem::real_t entry =
                dGamma * u_shape[a] * n_acoustic[c] * p_shape[b];
            Kup(ua, b) -= entry;
            Mpu(b, ua) += entry; // n_solid = -n_acoustic
        }
    }
}
```

The complete assembly loop is therefore:

1. obtain `solid_ir`, `acoustic_ir`, and `interface_ir`;
2. use standard integrators with each volume rule;
3. accumulate `Kup` and `Mpu` over the interface rule;
4. insert the `mfem::DenseMatrix` blocks into `mfem::SparseMatrix` blocks using
   the FE-space DOF lists.

The coupled semi-discrete system is paper Eq. 12:

\[
M\ddot v+C\dot v+Kv=h,\qquad v=[u,p]^T,
\]

with the non-symmetric block layout

\[
M=\begin{bmatrix}M_{uu}&0\\M_{pu}&M_{pp}\end{bmatrix},\quad
C=\begin{bmatrix}C_{uu}&0\\0&C_{pp}\end{bmatrix},\quad
K=\begin{bmatrix}K_{uu}&K_{up}\\0&K_{pp}\end{bmatrix}.
\]

Pressure traction belongs in `Kup`; structural acceleration coupling belongs in
`Mpu`. Do not mirror them into a symmetric stiffness matrix, and do not use CG
for the coupled solve.

Use true-DOF block offsets:

```cpp
mfem::Array<int> offsets(3);
offsets[0] = 0;
offsets[1] = displacement_fes.GetTrueVSize();
offsets[2] = offsets[1] + scalar_fes.GetTrueVSize();
```

## 6. Apply exterior boundary conditions

- Clamp \(u=0\) on `Gamma_sd` with
  `displacement_fes.GetEssentialTrueDofs(marker, ess_tdofs)`.
- The acoustic hard wall \(n\cdot\nabla p=0\) is natural and adds no term.
- Add the absorbing boundary mass `Cpp` only on `Gamma_ar`.
- Add the incident-wave load on the same exterior marker.
- Never treat `Gamma_as` as a boundary attribute; it exists inside cut elements.

Apply structural essential elimination consistently to all affected diagonal
and off-diagonal blocks before forming the effective time-step operator.

## 7. Keep time integration as a separate solver

The solver's input should be assembled `M`, `C`, `K`, a load callback, and time
settings. It should know nothing about level sets, Algoim, filtering, MMA, or
export.

For the paper's average-acceleration Newmark scheme,
\(\beta=1/4\), \(\gamma=1/2\), each step solves

\[
\widehat K v^n=\widehat h^n,\qquad
\widehat K=K+\frac{1}{\beta\Delta t^2}M
             +\frac{\gamma}{\beta\Delta t}C.
\]

Store all three fields needed by the fully discrete adjoint:

```cpp
struct StepState {
    mfem::Vector value;
    mfem::Vector velocity;
    mfem::Vector acceleration;
};

using ForwardHistory = std::vector<StepState>;
```

MFEM has
[`NewmarkSolver`](https://docs.mfem.org/4.9/classmfem_1_1NewmarkSolver.html),
which is useful as an independent forward check. Keep an explicit Newmark loop
for production optimization so the exact residual and every stored state used
by the reverse-time adjoint are visible.

A minimal dependency direction is:

```text
design -> filter -> GridFunction phi -> cut assembly -> M,C,K
                                                    -> transient solver
                                                    -> state history
state history -> FFT/objective -> adjoint -> design gradient -> optimizer
mesh + phi -> exporter
```

No arrow points from the transient solver back to geometry, optimization, UI,
or export.

## 8. Preserve the FFT and adjoint data

At every time step, integrate outlet pressure on the ordinary exterior outlet:

\[
\widehat p(t_n)=\int_{\Gamma_{out}}p(x,t_n)\,d\Gamma.
\]

Apply the same Hann window to the design and empty-duct histories, then use the
already-linked FFTW library. The transmission is

\[
S_m=\frac{|P_m|}{|P_{0,m}|}.
\]

Compute `P0` once using the identical mesh, source, time step, duration, outlet
functional, and FFT normalization. Reject frequency bins where `abs(P0)` is
below an excitation tolerance.

The paper balances pass- and stop-band errors with

\[
\Phi_{pass}=\sum_{m\in P}(S_m-1)^2,
\qquad
\Phi_{stop}=\sum_{m\in S}\frac{(S_m-b)^2}{b^2},
\]

where `b` is a small nonzero target transmission. Its epigraph form is

\[
\min_{s,z}z\quad\text{subject to}\quad
\Phi_{pass}(s)-z\le0,\quad
\Phi_{stop}(s)-z\le0,\quad 0\le s_i\le1.
\]

Do not set `b=0`, because it appears in the denominator.

Because the objective is a discrete function of a discrete FFT of the Newmark
history, use the paper's fully discrete adjoint:

\[
(\partial R/\partial U)^T\Lambda=-\partial\Phi/\partial U.
\]

Run the FFT adjoint back to time, then solve the Newmark adjoint in reverse
time. `mfem::Operator::MultTranspose` is the useful common matrix contract.

The hard boundary of MFEM's support is important: Algoim returns quadrature
points and weights, but MFEM 4.9 does not return derivatives of those points,
weights, or interface normals with respect to nodal `phi`. The paper's exact
\(dM/ds\), \(dC/ds\), and \(dK/ds\) on cut elements remain custom research
code based on the companion 2021 derivation. Use centred finite differences on
a coarse mesh to verify them; finite-differencing every design variable is not
a scalable optimization method.

## 9. Keep the optimizer behind one evaluator

Expose one operation to any optimizer:

```cpp
struct Evaluation {
    mfem::real_t objective;
    mfem::Vector constraints;
    mfem::Vector objective_gradient;
    mfem::Vector constraint_gradients; // row-major
};

Evaluation evaluate(const mfem::Vector &design, bool need_gradient);
```

Inside `evaluate`: map/filter, update `phi`, assemble, solve forward, calculate
FFT objectives, and optionally solve adjoints. The transient solver remains
independent because it receives only operators and loads.

MFEM 4.9 now includes
[`mfem::MMA`](https://docs.mfem.org/4.9/classmfem_1_1MMA.html), so a small serial
prototype can update the same `mfem::Vector` directly. For the paper's epigraph
problem, append `z` to the nodal design variables:

```cpp
mfem::Vector x(n_design + 1); // x[0..n_design-1] = s, x[n_design] = z
mfem::MMA mma(x.Size(), 2, x);
mma.Update(dfdx, gx, dgdx, lower, upper, x);
```

Set `dfdx[n_design] = 1`; set the constraints to
`Phi_pass - z` and `Phi_stop - z`; and put `-1` in the `z` column of both
constraint-gradient rows.

`dgdx` is row-major by constraint. Keep this call outside `evaluate`; then a
different or distributed optimizer can replace MMA without changing geometry,
assembly, forward solve, or adjoint code. Audit optimizer memory before a
million-variable 3D run.

## 10. Export the final level set without losing information

The authoritative result is the pair `(background mesh, nodal phi)`, not a
triangle mesh produced during optimization. Save both MFEM-native restart data
and a portable VTK data set:

```cpp
mesh.Save("optimized-background.mesh");
phi.Save("optimized-level-set.gf");

mfem::ParaViewDataCollection output("optimized", &mesh);
output.SetPrefixPath("output");
output.SetHighOrderOutput(true);
output.RegisterField("level_set", &phi);
output.Save();
```

`ParaViewDataCollection` writes PVD/VTU and registers `GridFunction` fields as
documented by [MFEM 4.9](https://docs.mfem.org/4.9/classmfem_1_1ParaViewDataCollection.html).
VTU plus scalar `level_set` is the interoperable hand-off to ParaView, VTK,
PyVista, or other open-source contouring tools.

For a **3D** design:

1. contour `level_set` at value `0` in ParaView/VTK;
2. clean and triangulate the resulting surface;
3. check that it is closed and consistently oriented;
4. write STL or OBJ;
5. optionally remesh it with Gmsh.

Gmsh's official
[STL remeshing workflow](https://gmsh.info/doc/texinfo/gmsh.html#t13)
uses `ClassifySurfaces` followed by `CreateGeometry` to construct discrete
surfaces from STL.

For the paper's **2D** design, `phi=0` is a curve. Extract the zero contour,
close it, and extrude it by the desired physical thickness before writing STL.
MFEM's `GridFunction::SaveSTL` is **not** a level-set extractor: for a 2D mesh
it writes the graph `(x,y,phi(x,y))`, not the zero contour.

STL is the natural output for arbitrary optimized topology. STEP requires a
watertight CAD B-Rep, so it needs a separate surface-fitting/healing stage in
OpenCASCADE/FreeCAD or equivalent. Gmsh can create discrete geometry from an
STL, but that should not be confused with recovering an exact analytic STEP
model.

## 11. Implement in this order

1. Reproduce Example 38 integrals for `phi>0`, `phi<0`, and `phi=0`; verify the
   two cut volumes sum to the full domain volume.
2. Validate acoustic-only and plane-stress elasticity-only problems.
3. Assemble `Mpu` and `Kup`; test their dimensions, signs, and interface normal.
4. Validate explicit Newmark against `mfem::NewmarkSolver` on fixed matrices.
5. Validate the outlet FFT with an empty duct and one known sinusoid.
6. Implement the reverse FFT/Newmark adjoint and require sampled centred finite
   differences to agree below `0.1%`, matching the paper.
7. Only then call MMA on a coarse design.
8. Export `(mesh, phi)` to VTU, contour at zero, and check the resulting surface
   is watertight before STL conversion.

The first useful milestone is steps 1 through 4: a verified forward CutFEM
solver. Optimization is not ready merely because Algoim integration works; the
cut-geometry derivatives are the remaining research-critical component.

## Official references

- [MFEM 4.9 code documentation](https://docs.mfem.org/4.9/)
- [MFEM Example 38: cut-volume and cut-surface integration](https://mfem.org/examples/)
- [`AlgoimIntegrationRules` API](https://docs.mfem.org/4.9/classmfem_1_1AlgoimIntegrationRules.html)
- [`Mesh` Cartesian constructors](https://docs.mfem.org/4.9/classmfem_1_1Mesh.html)
- [`GridFunction` output API](https://docs.mfem.org/4.9/classmfem_1_1GridFunction.html)
- [`ParaViewDataCollection` API](https://docs.mfem.org/4.9/classmfem_1_1ParaViewDataCollection.html)
- [`mfem::MMA` API](https://docs.mfem.org/4.9/classmfem_1_1MMA.html)
- [Gmsh STL remeshing tutorial](https://gmsh.info/doc/texinfo/gmsh.html#t13)
