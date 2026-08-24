# FEM and MFEM for Coupled Vibroacoustics, From Zero

This is a beginner-first construction manual for the forward solver described by
Dilgen and Aage's vibroacoustic cut-element work. It explains what FEM is, what
MFEM does, how the governing equations become matrices, what data every matrix
needs, and which MFEM calls build and solve the system.

The companion documents remain useful for different jobs:

- [`golden-paper.md`](golden-paper.md) is the paper transcription and source
  context;
- [`vibroacoustic_quick_reference.md`](vibroacoustic_quick_reference.md) is the
  compact lookup sheet;
- this document is the slow, pedagogical path between the mathematics and code.

The target is MFEM 4.9, which this repository pins in `CMakeLists.txt`. The first
validation target is the paper's two-dimensional plane-stress duct. The same
construction is then shown for three dimensions. Optimization, FFT objectives,
and adjoints come later; first make the forward FEM solve correct.

## Contents

1. [What FEM is](#1-what-is-the-finite-element-method)
2. [The minimum mathematics](#2-the-minimum-mathematics-needed)
3. [One fully worked element](#3-one-completely-worked-scalar-element)
4. [What MFEM offers](#4-what-mfem-offers)
5. [Prerequisite data](#5-the-problem-and-all-prerequisite-data)
6. [Strong forms in 2D and 3D](#6-strong-form-briefly)
7. [Strong form to weak form](#7-strong-form-to-weak-form)
8. [The discrete block system](#8-from-weak-form-to-the-discrete-block-system)
9. [Every matrix entry and prerequisite](#9-every-matrix-entry-and-its-prerequisite-data)
10. [Hand-calculated contributions](#10-hand-calculated-2d-matrix-contributions)
11. [Construct every matrix with MFEM](#11-constructing-every-matrix-with-the-mfem-api)
12. [Run and solve](#12-impose-boundaries-advance-time-and-solve)
13. [Verify the implementation](#13-verification-prove-each-layer-before-coupling-everything)
14. [Recommended implementation order](#14-recommended-implementation-order)
15. [Checkpoint answers](#15-checkpoint-answers)
16. [Sources and teaching design](#16-sources-and-why-the-teaching-order-looks-like-this)

## How to read this document

Do not try to memorize every equation. Keep one chain in mind:

```text
physical statement
  -> strong-form PDE and boundary conditions
  -> weak-form integrals
  -> basis-function expansions
  -> small dense matrix for each mesh element
  -> global sparse matrices
  -> boundary-condition elimination
  -> one algebraic system per time step
  -> linear solve
  -> pressure and displacement fields
```

Every major matrix is presented using the same questions:

1. What physical effect does it represent?
2. What is one entry of the matrix?
3. What data must exist before it can be built?
4. Which region, boundary, or interface contributes?
5. Which MFEM object or loop constructs it?
6. How can its result be checked?

Notation used throughout:

- a superscript `(e)` means "belonging to mesh element `e`";
- a superscript `n` means "at time step `n`";
- subscripts `i` and `j` index basis functions or matrix rows and columns;
- `u` is structural displacement and `p` is acoustic pressure;
- bold capital letters such as `M` denote assembled matrices.

> Beginner warning: a **mesh element** is a small geometric cell. A **matrix
> entry** is one number such as `A(i,j)`. They are both sometimes casually
> called an "element," but they are not the same thing.

---

## 1. What is the finite element method?

### 1.1 The problem FEM solves

A partial differential equation, or PDE, asks for a field at infinitely many
points. For example, pressure is a number `p(x,t)` at every position `x` and
time `t`. A computer cannot store infinitely many values.

FEM makes the problem finite:

1. divide the domain into small cells called **finite elements**;
2. describe the field inside each cell using simple basis functions;
3. store only the coefficients of those basis functions;
4. turn the PDE into a finite matrix equation for those coefficients.

For a scalar field on one element,

$$
p_h(x)=\sum_{j=1}^{n_p}N_j(x)P_j.
$$

Read this as:

- `N_j(x)` is a known shape or basis function;
- `P_j` is an unknown number, usually associated with a node;
- `p_h` is the finite-element approximation to the true pressure `p`;
- the subscript `h` reminds us that the answer depends on mesh size.

If a two-node line element has nodal values `P_1=2` and `P_2=6`, its linear
basis functions interpolate continuously from 2 to 6. The computer stores two
numbers, not a distinct pressure at every point.

### 1.2 The essential FEM words

| Word | Plain meaning | In this problem |
|---|---|---|
| Domain | Region where a PDE is solved | Fixed background duct `Omega` |
| Mesh | Domain split into cells | Quadrilaterals in 2D, hexahedra in 3D |
| Element | One mesh cell | Q4 quadrilateral or H8 hexahedron |
| Node | Geometric interpolation point | Q1 element corner |
| Field | Quantity that varies in space | `p`, `u`, or level set `phi` |
| Basis/shape function | Known local interpolation function | `N_i(x)` |
| DOF | One stored unknown coefficient | One pressure or displacement component |
| Local matrix | Small dense matrix for one element | `mfem::DenseMatrix` |
| Global matrix | All local matrices added together | `mfem::SparseMatrix` |
| Trial function | The unknown being acted on | Matrix column `j` |
| Test function | The equation used to measure residual | Matrix row `i` |
| Coefficient | Known material/spatial value | `rho`, `1/K`, `lambda`, `mu` |
| Quadrature | Weighted sample-point integration | `mfem::IntegrationRule` |
| Essential condition | Field value prescribed directly | Clamp `u=0` |
| Natural condition | Flux/traction appearing through weak form | Hard wall or free surface |

### 1.3 Why the matrix is sparse

A basis function is nonzero only on the few elements touching its node. It can
interact only with nearby basis functions. Therefore most global matrix entries
are zero. FEM stores only the nonzero entries in a sparse matrix.

### 1.4 Rows are tests; columns are unknowns

For a bilinear form `a(test, trial)`, the convention used here and by MFEM is

$$
A_{ij}=a(\text{test basis }i,\text{trial basis }j).
$$

Therefore:

- row `i` asks, "what does test basis `i` see?";
- column `j` asks, "how does unknown coefficient `j` affect that test?"

This matters especially for rectangular coupling matrices. `K_up` has
displacement tests in its rows and pressure trials in its columns. `M_pu` has
pressure tests in its rows and displacement trials in its columns.

### Checkpoint 1

Before continuing, answer without looking back:

1. Does MFEM solve for a value at every mathematical point?
2. Is a trial basis associated with a matrix row or column?
3. Why is a global FEM matrix usually sparse?

Answers are in [Section 15](#15-checkpoint-answers).

---

## 2. The minimum mathematics needed

### 2.1 Scalars, vectors, and matrices

Pressure is a scalar:

$$p\in\mathbb R.$$

Displacement has one component per spatial direction:

$$
u=\begin{bmatrix}u_x\\u_y\end{bmatrix}\text{ in 2D},\qquad
u=\begin{bmatrix}u_x\\u_y\\u_z\end{bmatrix}\text{ in 3D}.
$$

A matrix maps a vector of coefficients to a vector of residuals:

$$Ax=b.$$

Entry `A_ij` tells how unknown `x_j` contributes to equation `i`.

### 2.2 Derivatives used here

| Symbol | Meaning |
|---|---|
| `dot p` | First time derivative, velocity-like rate |
| `ddot p` | Second time derivative, acceleration-like rate |
| `grad p` | Spatial direction and rate of pressure change |
| `div u` | Net outward spatial change of a vector field |
| `epsilon(u)` | Symmetric spatial gradient of displacement, called strain |
| `sigma(u)` | Stress obtained from strain and material law |

Time discretization and spatial discretization are separate:

- FEM in this document discretizes space;
- Newmark later advances the resulting system through time.

### 2.3 An integral becomes a weighted sum

On physical element `Omega_e`, a computer approximates

$$
\int_{\Omega_e}f(x)\,d\Omega
\approx
\sum_{q=1}^{n_q}w_q f(x_q)|J_e(x_q)|.
$$

At quadrature point `q`:

- `x_q` is the sample location;
- `w_q` is the reference quadrature weight;
- `|J_e|` scales reference-element area/volume to physical area/volume.

In MFEM:

```cpp
T.SetIntPoint(&ip);
const mfem::real_t dV = ip.weight * T.Weight();
```

Each matrix entry is just a sum of small deposits made at quadrature points.

### 2.4 Integration by parts is the bridge to the weak form

In one dimension,

$$
\int_a^b q(-p'')\,dx
=\int_a^b q'p'\,dx-[qp']_a^b.
$$

In several dimensions, the corresponding identity is

$$
\int_\Omega q[-\nabla\cdot(a\nabla p)]\,d\Omega
=\int_\Omega a\nabla q\cdot\nabla p\,d\Omega
-\int_{\partial\Omega}qa(\nabla p\cdot n)\,d\Gamma.
$$

This performs two jobs:

1. it lowers the derivative required of the unknown `p`;
2. it exposes boundary fluxes, so natural boundary conditions can enter.

That is the central mathematical move from strong form to weak form.

---

## 3. One completely worked scalar element

The coupled problem is easier after seeing one small matrix appear by hand.
Consider the one-dimensional transient equation

$$
c\ddot p-\frac{d}{dx}\left(a\frac{dp}{dx}\right)=f
\quad\text{on }[0,h].
$$

Here `c` and `a` are positive constants. This has the same mass-plus-diffusion
pattern as the acoustic equation.

### 3.1 Strong form to weak form

Multiply by a test function `q` and integrate:

$$
\int_0^h cq\ddot p\,dx
-\int_0^h q(ap')'\,dx
=\int_0^h qf\,dx.
$$

Integrate the second term by parts:

$$
\int_0^h cq\ddot p\,dx
+\int_0^h aq'p'\,dx
-[qap']_0^h
=\int_0^h qf\,dx.
$$

If the end flux is zero, the boundary term vanishes. The weak form is then

$$
\int_0^h cq\ddot p\,dx
+\int_0^h aq'p'\,dx
=\int_0^h qf\,dx.
$$

### 3.2 Choose two linear basis functions

On the element:

$$
N_1(x)=1-\frac{x}{h},\qquad N_2(x)=\frac{x}{h}.
$$

Approximate

$$
p_h=N_1P_1+N_2P_2.
$$

Use `q=N_i` once for each row. Because

$$
\ddot p_h=N_1\ddot P_1+N_2\ddot P_2,
$$

the first integral becomes a matrix times nodal accelerations.

### 3.3 Compute every local entry

The element mass entries are

$$
M_{ij}^{(e)}=\int_0^h cN_iN_j\,dx,
$$

which gives

$$
M^{(e)}=\frac{ch}{6}
\begin{bmatrix}
2&1\\
1&2
\end{bmatrix}.
$$

The element diffusion/stiffness entries are

$$
K_{ij}^{(e)}=\int_0^h aN_i'N_j'\,dx,
$$

which gives

$$
K^{(e)}=\frac{a}{h}
\begin{bmatrix}
1&-1\\
-1&1
\end{bmatrix}.
$$

The element load entries are

$$
f_i^{(e)}=\int_0^h N_if\,dx.
$$

For constant `f`,

$$
f^{(e)}=\frac{fh}{2}\begin{bmatrix}1\\1\end{bmatrix}.
$$

The element equation is

$$
M^{(e)}\ddot P^{(e)}+K^{(e)}P^{(e)}=f^{(e)}.
$$

Nothing magical happened: choosing a test basis selected a row; expanding the
trial field produced the columns; integration produced the numbers.

### 3.4 Assembly: two local matrices become one global matrix

Suppose element 0 uses global nodes `[0,1]` and element 1 uses `[1,2]`. Both
local matrices contribute at shared node 1:

$$
K=\frac{a}{h}
\begin{bmatrix}
1&-1&0\\
-1&2&-1\\
0&-1&1
\end{bmatrix}.
$$

Assembly is the repeated operation

```text
global_matrix[global_dof_i, global_dof_j]
    += local_matrix[local_i, local_j]
```

The middle diagonal is `2` because two elements touch the middle node.

### 3.5 The direct MFEM mapping

For an ordinary, uncut scalar finite-element space:

```cpp
mfem::BilinearForm mass(&pressure_fes);
mass.AddDomainIntegrator(new mfem::MassIntegrator(c_coeff));

mfem::BilinearForm stiffness(&pressure_fes);
stiffness.AddDomainIntegrator(new mfem::DiffusionIntegrator(a_coeff));

mass.Assemble();
mass.Finalize();
stiffness.Assemble();
stiffness.Finalize();
```

`MassIntegrator` computes the `N_i N_j` integrals. `DiffusionIntegrator`
computes the `grad(N_i) dot grad(N_j)` integrals. `BilinearForm` performs the
element loop and global scatter.

This same pattern will become `M_pp` and `K_pp` in the acoustic system.

---

## 4. What MFEM offers

MFEM is a C++ finite-element library. It supplies tested machinery for meshes,
basis functions, element transformations, quadrature, assembly, sparse linear
algebra, solvers, and field output. It does not decide the physics for you.

### 4.1 Responsibilities by object

| MFEM object | What it knows | What it does not know |
|---|---|---|
| `mfem::Mesh` | Geometry, cells, exterior boundary attributes | Pressure, material law, coupling signs |
| `mfem::H1_FECollection` | Basis family and polynomial order | Which field uses it |
| `mfem::FiniteElementSpace` | DOF layout on a mesh | Current field values |
| `mfem::GridFunction` | FE field coefficients | Governing equation |
| `mfem::Coefficient` | Known value at quadrature points | How it enters a form |
| `mfem::IntegrationRule` | Quadrature points and weights | Physics evaluated there |
| `mfem::BilinearForm` | Square global matrix assembly | Correct PDE/sign choice |
| `mfem::MixedBilinearForm` | Rectangular global matrix assembly | This cut-interface law automatically |
| `mfem::LinearForm` | Global right-hand-side assembly | Time-stepping policy |
| `mfem::DenseMatrix` | One local matrix | Global DOF connectivity |
| `mfem::SparseMatrix` | Global sparse operator | Physical meaning of entries |
| `mfem::BlockMatrix` | Placement of matrix blocks | Ownership of raw block pointers |
| `mfem::GMRESSolver` | Iterative solution of `Ax=b` | How `A` and `b` were derived |

### 4.2 What MFEM automates

For standard terms, MFEM can automate this nested work:

```text
for each mesh element e
    find its local basis functions and global DOF numbers
    for each quadrature point q
        evaluate basis values/gradients, coefficient, and geometry weight
        add the contribution to local DenseMatrix
    scatter local DenseMatrix into global SparseMatrix
```

MFEM's official [bilinear-form integrator
documentation](https://mfem.org/bilininteg/) describes exactly this division:
an integrator produces the small local matrix and `BilinearForm` assembles the
global sparse matrix.

### 4.3 What must be custom in this problem

The immersed acoustic-structure interface `phi=0` lies inside background mesh
elements. It is not an ordinary exterior mesh boundary. Therefore:

- MFEM/Algoim can generate cut-volume and cut-surface quadrature;
- standard MFEM integrators can still compute volume element formulas when
  given those rules;
- a custom element loop must assemble `K_up` and `M_pu` on the internal cut
  surface;
- your code remains responsible for phase signs, interface normal orientation,
  block placement, Newmark bookkeeping, and validation.

### 4.4 Object lifetime order

Objects store pointers to earlier objects. Keep them alive in this order:

```text
Mesh
  -> FiniteElementCollection
    -> FiniteElementSpace
      -> GridFunction and forms
        -> matrices/operators/solvers that reference them
```

For example, do not create an `H1_FECollection` as a temporary inside a helper
and return a `FiniteElementSpace` that points to the destroyed collection.

### 4.5 Builds data versus runs computation

| Call | Effect |
|---|---|
| `AddDomainIntegrator` | Registers a formula; no element loop yet |
| `AddBoundaryIntegrator` | Registers a boundary formula |
| `Assemble` | Computes and adds element contributions |
| `Finalize` | Converts an open sparse matrix to finalized sparse storage |
| `LoseMat` | Transfers matrix ownership out of a form |
| `SetBlock` | Places a matrix pointer into a block container |
| `CreateMonolithic` | Copies block values into one sparse matrix |
| `FormLinearSystem` | Builds an eliminated system; does not solve it |
| `solver.SetOperator(A)` | Attaches/prepares the operator |
| `solver.Mult(b,x)` | Actually solves or applies the solver |
| `SetFromTrueDofs` | Copies algebraic values into a `GridFunction` |

---

## 5. The problem and all prerequisite data

### 5.1 Geometry and phase convention

The fixed background domain is

$$
\Omega=\Omega_s\cup\Omega_a,
$$

where `Omega_s` is solid and `Omega_a` is acoustic fluid. A level-set field
separates them:

$$
\phi(x)>0\text{ in solid},\qquad
\phi(x)<0\text{ in fluid},\qquad
\phi(x)=0\text{ on }\Gamma_{as}.
$$

With this convention, the acoustic outward normal points from acoustic
`phi<0` toward solid `phi>0`:

$$
n_a=\frac{\nabla\phi}{\|\nabla\phi\|},\qquad n_s=-n_a.
$$

### 5.2 Fields and element-local sizes

Let `n_s` be the number of scalar basis functions on one element and `d` the
spatial dimension.

| Model | Scalar nodes | Pressure DOFs | Displacement DOFs |
|---|---:|---:|---:|
| 2D Q4/Q1 | `n_s=4` | `n_p=4` | `n_u=2*4=8` |
| 3D H8/Q1 | `n_s=8` | `n_p=8` | `n_u=3*8=24` |

The element block sizes therefore are:

| Block | Rows x columns in 2D Q4 | Rows x columns in 3D H8 |
|---|---:|---:|
| `M_uu`, `K_uu`, `C_uu` | `8 x 8` | `24 x 24` |
| `M_pp`, `K_pp`, `C_pp` | `4 x 4` | `8 x 8` |
| `K_up` | `8 x 4` | `24 x 8` |
| `M_pu` | `4 x 8` | `8 x 24` |
| `g` | `4` | `8` |

### 5.3 Raw inputs

| Input | Symbol/code name | Units | Used by |
|---|---|---|---|
| Duct dimensions | `lx`, `ly`, `lz` | m | mesh/Jacobians |
| Cell counts | `nx`, `ny`, `nz` | none | mesh |
| FE order | `fe_order` | none | basis and spaces |
| Level-set field | `phi_h` | usually m or scaled | cut rules, normal |
| Solid density | `rho_s` | kg/m^3 | `M_uu` |
| Young's modulus | `E` | Pa | `K_uu` |
| Poisson ratio | `nu` | none | `K_uu` |
| Fluid density | `rho_a` | kg/m^3 | `K_pp`, `C_pp`, `g` |
| Sound speed | `c_a` | m/s | `K_a`, `C_pp`, `g` |
| Fictitious contrast | `epsilon_f` | none | cut-volume blocks |
| Rayleigh mass factor | `alpha_d` | 1/s | `C_uu` |
| Rayleigh stiffness factor | `beta_d` | s | `C_uu` |
| Absorbing boundary marker | `absorbing_marker` | none | `C_pp` |
| Inlet marker | `inlet_marker` | none | `g` |
| Clamp marker | `clamp_marker` | none | essential displacement DOFs |
| Incident pressure rate | `dot_p_in(t)` | Pa/s | `g(t)` |
| Time step | `dt` | s | Newmark effective system |
| Final time | `T` | s | number of time steps |

### 5.4 Derived values

The acoustic bulk modulus is

$$
K_a=\rho_a c_a^2.
$$

The shear modulus used in both 2D and 3D is

$$
\mu=\frac{E}{2(1+\nu)}.
$$

The first Lamé-type coefficient differs:

$$
\lambda_{ps}=\frac{E\nu}{1-\nu^2}
\quad\text{for 2D plane stress},
$$

$$
\lambda_{3D}=\frac{E\nu}{(1+\nu)(1-2\nu)}
\quad\text{for 3D elasticity}.
$$

Do not use the 3D/plane-strain value in the paper's 2D plane-stress
reproduction.

If equal damping ratio `zeta` is requested at angular frequencies `omega_1`
and `omega_2`, the Rayleigh coefficients are

$$
\alpha_d=\frac{2\zeta\omega_1\omega_2}{\omega_1+\omega_2},\qquad
\beta_d=\frac{2\zeta}{\omega_1+\omega_2}.
$$

### 5.5 MFEM objects that must exist before assembly

```cpp
const int dim = mesh.Dimension();

mfem::H1_FECollection q1(fe_order, dim);
mfem::FiniteElementSpace pressure_fes(&mesh, &q1);
mfem::FiniteElementSpace level_set_fes(&mesh, &q1);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &q1, dim, mfem::Ordering::byVDIM);

mfem::GridFunction phi_h(&level_set_fes);
mfem::GridFunction pressure(&pressure_fes);
mfem::GridFunction displacement(&displacement_fes);
```

`byVDIM` groups element displacement DOFs by component. In 2D Q4, the local
ordering is conceptually

```text
[u1_x, u2_x, u3_x, u4_x, u1_y, u2_y, u3_y, u4_y].
```

Always obtain actual DOF lists from MFEM instead of assuming global numbering.

---

## 6. Strong form, briefly

The **strong form** states the PDE point by point and separately states its
boundary conditions. It is the physical model before FEM testing and
integration by parts.

### 6.1 Structure in any spatial dimension

Small strain is

$$
\varepsilon(u)=\frac12(\nabla u+\nabla u^T).
$$

Isotropic linear stress is

$$
\sigma(u)=\lambda\operatorname{tr}[\varepsilon(u)]I
+2\mu\varepsilon(u).
$$

With Rayleigh-type damping, structural momentum balance is

$$
\rho_s\ddot u-\nabla\cdot\sigma(u)
+\alpha_d\rho_s\dot u
-\nabla\cdot[\beta_d\sigma(\dot u)]=0
\quad\text{in }\Omega.
$$

Boundary conditions are

$$
u=0\text{ on }\Gamma_{sd},\qquad
\sigma n_s=0\text{ on }\Gamma_{sn},\qquad
\sigma n_s=p n_a\text{ on }\Gamma_{as}.
$$

The last condition means acoustic pressure loads the solid interface.

### 6.2 The 2D plane-stress specialization

For the paper's 2D model,

$$
\epsilon(u)=
\begin{bmatrix}
\partial_xu_x\\
\partial_yu_y\\
\partial_yu_x+\partial_xu_y
\end{bmatrix},
$$

where the third component is engineering shear strain. Then

$$
\sigma_v=D_{ps}\epsilon,
$$

with

$$
D_{ps}=\frac{E}{1-\nu^2}
\begin{bmatrix}
1&\nu&0\\
\nu&1&0\\
0&0&(1-\nu)/2
\end{bmatrix}.
$$

Using `lambda_ps` and `mu` above in `mfem::ElasticityIntegrator` represents
this plane-stress law.

### 6.3 The 3D specialization

In 3D, use

$$
u=[u_x,u_y,u_z]^T,
$$

the same tensor strain and stress definitions, and `lambda_3D`. The strong
momentum equation and boundary types do not change; only the vector dimension,
strain components, constitutive coefficient, and element sizes change.

### 6.4 Acoustics in 2D or 3D

The transient pressure equation is

$$
\frac1{K_a}\ddot p
-\nabla\cdot\left(\frac1{\rho_a}\nabla p\right)=0
\quad\text{in }\Omega.
$$

For piecewise constant fluid density this agrees with the paper's
`-(1/rho_a) Laplacian(p)` form. The divergence form makes the weak-form
coefficient placement explicit.

Boundary conditions are

$$
n_a\cdot\nabla p=0
\quad\text{on hard walls }\Gamma_{ad},
$$

$$
n_a\cdot\nabla p
=\rho_a\frac{\partial^2(n_s\cdot u)}{\partial t^2}
\quad\text{on }\Gamma_{as},
$$

$$
n_a\cdot\nabla p+\frac1{c_a}\dot p
=\frac2{c_a}\dot p_{in}
\quad\text{on }\Gamma_{ar}.
$$

These say, respectively: no normal acoustic flux at a hard wall; solid normal
acceleration drives the fluid; and the outer duct boundary absorbs outgoing
waves while the inlet can inject an incident wave.

### 6.5 Fictitious material on the fixed mesh

Both fields are solved over the entire background mesh. The inactive phase is
made weak but not exactly zero:

$$
\alpha_s=
\begin{cases}1&\phi>0\\\epsilon_f&\phi<0\end{cases},\qquad
\alpha_a=
\begin{cases}\epsilon_f&\phi>0\\1&\phi<0\end{cases}.
$$

Use

$$
E_s(x)=\alpha_s E,\quad \rho_s(x)=\alpha_s\rho_s,
$$

and, equivalently for the weak acoustic coefficients,

$$
\frac1{K_a(x)}=\frac{\alpha_a}{\widetilde K_a},\qquad
\frac1{\rho_a(x)}=\frac{\alpha_a}{\widetilde\rho_a}.
$$

The paper uses `epsilon_f=1e-8`, but also warns that equal mass and stiffness
scaling can create fictitious-domain modes. Treat it as a reproduction value,
not a universal law.

---

## 7. Strong form to weak form

The recipe is always:

1. multiply the structural equation by displacement test `w`, or the acoustic
   equation by pressure test `q`;
2. integrate over the domain;
3. integrate divergence terms by parts once;
4. substitute boundary conditions;
5. move prescribed source terms to the right-hand side.

### 7.1 Structural weak form

Start from the structural PDE, multiply by `w`, and integrate. Integration by
parts changes the stress-divergence term according to

$$
-\int_\Omega w\cdot(\nabla\cdot\sigma)\,d\Omega
=\int_\Omega\varepsilon(w):\sigma\,d\Omega
-\int_{\partial\Omega}w\cdot(\sigma n_s)\,d\Gamma.
$$

The clamp is handled by choosing tests that vanish on `Gamma_sd`. The free
traction is zero. On the acoustic-structure interface,
`sigma n_s=p n_a`. The structural weak equation is therefore

$$
\begin{aligned}
&\int_\Omega\rho_s w\cdot\ddot u\,d\Omega
+\alpha_d\int_\Omega\rho_s w\cdot\dot u\,d\Omega\\
&+\int_\Omega\varepsilon(w):C:\varepsilon(u)\,d\Omega
+\beta_d\int_\Omega\varepsilon(w):C:\varepsilon(\dot u)\,d\Omega\\
&-\int_{\Gamma_{as}}(w\cdot n_a)p\,d\Gamma=0.
\end{aligned}
$$

Notice where pressure appears: it multiplies the displacement test. It will
therefore become a matrix with displacement rows and pressure columns,
`K_up`.

### 7.2 Acoustic weak form

Multiply the acoustic PDE by `q` and integrate:

$$
\int_\Omega\frac1{K_a}q\ddot p\,d\Omega
+\int_\Omega\frac1{\rho_a}\nabla q\cdot\nabla p\,d\Omega
-\int_{\partial\Omega}\frac1{\rho_a}q(n_a\cdot\nabla p)\,d\Gamma=0.
$$

The hard-wall contribution is zero. Substitute the interface and absorbing
conditions. The result is

$$
\begin{aligned}
&\int_\Omega\frac1{K_a}q\ddot p\,d\Omega
+\int_\Omega\frac1{\rho_a}\nabla q\cdot\nabla p\,d\Omega\\
&-\int_{\Gamma_{as}}q(n_s\cdot\ddot u)\,d\Gamma
+\int_{\Gamma_{ar}}\frac1{\rho_ac_a}q\dot p\,d\Gamma\\
&=\int_{\Gamma_{ar,in}}
\frac2{\rho_ac_a}q\dot p_{in}\,d\Gamma.
\end{aligned}
$$

Normal displacement acceleration multiplies a pressure test. It therefore
becomes a matrix with pressure rows and displacement columns, `M_pu`.

### 7.3 Name each weak-form contribution

$$
\begin{aligned}
m_{uu}(w,u)&=\int_\Omega\rho_s w\cdot u\,d\Omega,\\
k_{uu}(w,u)&=\int_\Omega\varepsilon(w):C:\varepsilon(u)\,d\Omega,\\
c_{uu}(w,u)&=\alpha_dm_{uu}(w,u)+\beta_dk_{uu}(w,u),\\
m_{pp}(q,p)&=\int_\Omega K_a^{-1}qp\,d\Omega,\\
k_{pp}(q,p)&=\int_\Omega\rho_a^{-1}\nabla q\cdot\nabla p\,d\Omega,\\
c_{pp}(q,p)&=\int_{\Gamma_{ar}}(\rho_ac_a)^{-1}qp\,d\Gamma,\\
k_{up}(w,p)&=-\int_{\Gamma_{as}}(w\cdot n_a)p\,d\Gamma,\\
m_{pu}(q,u)&=-\int_{\Gamma_{as}}q(n_s\cdot u)\,d\Gamma,\\
g(q,t)&=\int_{\Gamma_{ar,in}}2(\rho_ac_a)^{-1}q\dot p_{in}(t)\,d\Gamma.
\end{aligned}
$$

These nine expressions are the complete spatial assembly target for the
forward coupled problem.

---

## 8. From weak form to the discrete block system

### 8.1 Expand pressure and displacement

For scalar pressure basis functions `N_j`,

$$
p_h=\sum_{j=1}^{n_p}N_jP_j.
$$

For displacement, use one copy of each scalar basis per component:

$$
u_h=\sum_{j=1}^{n_s}\sum_{d=1}^{D}N_j e_d U_{j,d},
$$

where `e_d` is a Cartesian unit vector. Define vector basis

$$
\Psi_{j,d}=N_je_d.
$$

Choose each `N_i` or `Psi_i,c` in turn as the test. This produces one discrete
row per test basis.

### 8.2 The semi-discrete equation

Collect all true displacement and pressure DOFs into

$$
v=\begin{bmatrix}u\\p\end{bmatrix},\qquad
h=\begin{bmatrix}0\\g\end{bmatrix}.
$$

The spatially discrete but time-continuous system is

$$
M\ddot v+C\dot v+Kv=h.
$$

Its blocks are

$$
M=
\begin{bmatrix}
M_{uu}&0\\
M_{pu}&M_{pp}
\end{bmatrix},\qquad
C=
\begin{bmatrix}
C_{uu}&0\\
0&C_{pp}
\end{bmatrix},
$$

$$
K=
\begin{bmatrix}
K_{uu}&K_{up}\\
0&K_{pp}
\end{bmatrix}.
$$

Read the first block row:

$$
M_{uu}\ddot u+C_{uu}\dot u+K_{uu}u+K_{up}p=0.
$$

Read the second block row:

$$
M_{pu}\ddot u+M_{pp}\ddot p+C_{pp}\dot p+K_{pp}p=g.
$$

This system is generally nonsymmetric. Pressure traction lives in `K_up`,
while structural acceleration coupling lives in `M_pu`. Do not move them into
matching stiffness blocks merely to make the matrix look symmetric.

### Checkpoint 2

1. Why does `K_up` have displacement rows and pressure columns?
2. Why is `M_pu` a mass-like coupling rather than a stiffness coupling?
3. Which block contains the absorbing boundary condition?

---

## 9. Every matrix entry and its prerequisite data

This section is the matrix handbook. First define three element-level objects
that are reused everywhere.

### 9.1 Shape, interpolation, and strain matrices

At one quadrature point, collect scalar basis values into

$$
N=\begin{bmatrix}N_1&N_2&\cdots&N_{n_s}\end{bmatrix}^T.
$$

For 2D Q4 displacement with `byVDIM` ordering, the vector interpolation matrix
is

$$
S=
\begin{bmatrix}
N_1&N_2&N_3&N_4&0&0&0&0\\
0&0&0&0&N_1&N_2&N_3&N_4
\end{bmatrix}.
$$

It maps local displacement DOFs to displacement at the quadrature point:

$$u_h(x_q)=S(x_q)U^{(e)}.$$

For node `i` in 2D, define

$$
B_i=
\begin{bmatrix}
\partial_xN_i&0\\
0&\partial_yN_i\\
\partial_yN_i&\partial_xN_i
\end{bmatrix}.
$$

Concatenating the columns in the selected DOF ordering gives the element strain
matrix `B`, and

$$\epsilon(u_h)=BU^{(e)}.$$

For node `i` in 3D, using engineering strain order
`[epsilon_xx, epsilon_yy, epsilon_zz, gamma_xy, gamma_yz, gamma_xz]`, use

$$
B_i=
\begin{bmatrix}
N_{i,x}&0&0\\
0&N_{i,y}&0\\
0&0&N_{i,z}\\
N_{i,y}&N_{i,x}&0\\
0&N_{i,z}&N_{i,y}\\
N_{i,z}&0&N_{i,x}
\end{bmatrix}.
$$

MFEM calculates basis values and physical gradients for you. These matrices are
shown so you understand the numbers its integrators deposit.

### 9.2 Structural mass `M_uu`

**Meaning:** resistance of the solid mass to acceleration.

**Element size:** `D*n_s` by `D*n_s`.

**One entry:** for test component `c` at node `i` and trial component `d` at
node `j`,

$$
(M_{uu}^{(e)})_{(i,c),(j,d)}
=\int_{\Omega_e}\rho_s N_iN_j\delta_{cd}\,d\Omega.
$$

`delta_cd` is 1 when the components match and 0 otherwise. That is why scalar
density produces repeated diagonal component blocks and no `x-y` mass mixing.

**Cut-element split:**

$$
M_{uu}^{(e)}=
\int_{\Omega_e\cap\Omega_s}\rho_sS^TS\,d\Omega
+\epsilon_f\int_{\Omega_e\cap\Omega_a}\rho_sS^TS\,d\Omega.
$$

**One quadrature deposit:**

$$M_{uu}^{(e)}\mathrel{+}=\rho_sS^TS\,dV.$$

**Prerequisites:**

- displacement finite element and its vector dimension;
- structural density `rho_s`;
- solid and acoustic cut-volume quadrature rules;
- fictitious contrast `epsilon_f`;
- element transformation/Jacobian;
- displacement element VDOF list for global insertion.

**MFEM formula:** `mfem::VectorMassIntegrator`.

**Checks:** square, symmetric, nonnegative diagonal; size `8x8` for Q4 and
`24x24` for H8. With scalar density and `byVDIM`, its component diagonal blocks
match.

### 9.3 Structural stiffness `K_uu`

**Meaning:** elastic force created by deformation.

**Element size:** `D*n_s` by `D*n_s`.

**One entry:**

$$
(K_{uu}^{(e)})_{(i,c),(j,d)}
=\int_{\Omega_e}\varepsilon(N_ie_c):C:\varepsilon(N_je_d)\,d\Omega.
$$

In matrix notation,

$$
K_{uu}^{(e)}=\int_{\Omega_e}B^TDB\,d\Omega.
$$

Use `D_ps` in 2D plane stress and the 3D isotropic law in 3D.

**Cut-element split:**

$$
K_{uu}^{(e)}=
\int_{\Omega_e\cap\Omega_s}B^TDB\,d\Omega
+\epsilon_f\int_{\Omega_e\cap\Omega_a}B^TDB\,d\Omega.
$$

**One quadrature deposit:**

$$K_{uu}^{(e)}\mathrel{+}=B^TDB\,dV.$$

**Prerequisites:**

- everything needed to evaluate physical basis gradients;
- `E`, `nu`, and the correct 2D/3D `lambda` plus `mu`;
- solid and acoustic cut-volume rules;
- `epsilon_f`, element transformation, and displacement VDOFs.

**MFEM formula:** `mfem::ElasticityIntegrator(lambda, mu)`.

**Checks:** square and symmetric before essential elimination; rigid
translations have zero elastic energy on an unconstrained isolated element;
using plane-stress `lambda` in 2D is deliberate.

### 9.4 Structural Rayleigh damping `C_uu`

**Meaning:** a weighted combination of mass-like and stiffness-like damping.

**Construction:** no new integration is needed:

$$
C_{uu}=\alpha_dM_{uu}+\beta_dK_{uu}.
$$

**Prerequisites:** finalized `M_uu`, finalized `K_uu`, `alpha_d`, `beta_d`.

**MFEM operation:** sparse matrix addition using `mfem::Add`.

**Checks:** same dimensions and sparsity neighborhood as the structural blocks;
zero coefficients produce a zero damping block.

### 9.5 Acoustic mass/compressibility `M_pp`

**Meaning:** acoustic pressure's inertial/compressibility term multiplying
`ddot p`.

**Element size:** `n_s` by `n_s`.

**One entry:**

$$
(M_{pp}^{(e)})_{ij}
=\int_{\Omega_e}\frac1{K_a}N_iN_j\,d\Omega.
$$

**Cut-element split:** acoustic material is physical in `phi<0`:

$$
M_{pp}^{(e)}=
\int_{\Omega_e\cap\Omega_a}\frac1{K_a}N^TN\,d\Omega
+\epsilon_f\int_{\Omega_e\cap\Omega_s}\frac1{K_a}N^TN\,d\Omega.
$$

Here `N` is viewed as a row in `N^T N`.

**One quadrature deposit:**

$$M_{pp}^{(e)}\mathrel{+}=K_a^{-1}N^TN\,dV.$$

**Prerequisites:** pressure finite element, `rho_a`, `c_a`, derived
`K_a=rho_a*c_a*c_a`, both cut-volume rules, `epsilon_f`, transformation, and
pressure element DOFs.

**MFEM formula:** `mfem::MassIntegrator(inv_K_a)`.

**Checks:** square, symmetric, positive diagonal; size `4x4` for Q4 and `8x8`
for H8.

### 9.6 Acoustic stiffness/diffusion `K_pp`

**Meaning:** spatial pressure-gradient propagation.

**Element size:** `n_s` by `n_s`.

**One entry:**

$$
(K_{pp}^{(e)})_{ij}
=\int_{\Omega_e}\frac1{\rho_a}\nabla N_i\cdot\nabla N_j\,d\Omega.
$$

**Cut-element split:**

$$
K_{pp}^{(e)}=
\int_{\Omega_e\cap\Omega_a}\frac1{\rho_a}\nabla N_i\cdot\nabla N_j\,d\Omega
+\epsilon_f\int_{\Omega_e\cap\Omega_s}
\frac1{\rho_a}\nabla N_i\cdot\nabla N_j\,d\Omega.
$$

**One quadrature deposit:**

$$
(K_{pp}^{(e)})_{ij}\mathrel{+}=
\rho_a^{-1}(\nabla N_i\cdot\nabla N_j)dV.
$$

**Prerequisites:** pressure basis gradients in physical coordinates,
`rho_a`, both cut-volume rules, `epsilon_f`, transformation, and pressure DOFs.

**MFEM formula:** `mfem::DiffusionIntegrator(inv_rho_a)`.

**Checks:** symmetric; an unconstrained constant pressure vector is in the
nullspace of a pure hard-wall diffusion operator; row sums are approximately
zero before absorbing-boundary and essential terms are added.

### 9.7 Acoustic absorbing-boundary damping `C_pp`

**Meaning:** lets outgoing acoustic waves leave the outer boundary with reduced
reflection.

**Element-boundary size:** number of pressure DOFs supported on the boundary
element, scattered into the global `p-p` block.

**One entry:**

$$
(C_{pp})_{ij}
=\int_{\Gamma_{ar}}\frac1{\rho_ac_a}N_iN_j\,d\Gamma.
$$

**One boundary quadrature deposit:**

$$C_{pp}\mathrel{+}=(\rho_ac_a)^{-1}N^TN\,d\Gamma.$$

**Prerequisites:** pressure space, exterior absorbing-boundary attribute
marker, `rho_a`, `c_a`, ordinary boundary quadrature and transformations, and
pressure boundary DOFs.

**MFEM formula:** `mfem::BoundaryMassIntegrator(inv_rho_c)` added to a pressure
`BilinearForm` with an absorbing marker.

**Checks:** nonzero only on absorbing-boundary pressure DOFs; symmetric and
positive semidefinite; absent on hard walls.

### 9.8 Pressure-to-structure interface coupling `K_up`

**Meaning:** pressure traction pushes structural displacement equations.

**Element size:** `D*n_s` displacement rows by `n_s` pressure columns.

**One entry:**

$$
(K_{up}^{(e)})_{(i,c),j}
=-\int_{\Gamma_{as}\cap\Omega_e}N_i(n_a)_cN_j\,d\Gamma.
$$

**Matrix form:**

$$
K_{up}^{(e)}=-\int_{\Gamma_{as}\cap\Omega_e}
S^Tn_aN\,d\Gamma,
$$

where `n_a` is a column vector and `N` is a row vector.

**One interface quadrature deposit:**

$$K_{up}^{(e)}\mathrel{+}=-S^Tn_aN\,d\Gamma.$$

**Prerequisites:**

- a cut element and its cut-surface quadrature rule;
- Algoim surface transformation weights;
- `phi_h` gradient and a verified normal orientation;
- displacement shapes, pressure shapes, and both element DOF lists;
- a rectangular `D*n_s` by `n_s` local matrix.

**MFEM construction:** custom cut-surface element loop. An ordinary
`BoundaryMassIntegrator` cannot see an interface inside a volume element.

**Checks:** `8x4` for a 2D Q4 cut element and `24x8` for 3D H8; reversing the
normal reverses its sign; it is zero for uncut elements.

### 9.9 Structure-to-pressure interface coupling `M_pu`

**Meaning:** normal structural acceleration drives acoustic pressure equations.

**Element size:** `n_s` pressure rows by `D*n_s` displacement columns.

**One entry:**

$$
(M_{pu}^{(e)})_{i,(j,c)}
=-\int_{\Gamma_{as}\cap\Omega_e}N_i(n_s)_cN_j\,d\Gamma.
$$

Because `n_s=-n_a`,

$$
M_{pu}^{(e)}=\int_{\Gamma_{as}\cap\Omega_e}N^Tn_a^TS\,d\Gamma.
$$

**One interface quadrature deposit:**

$$M_{pu}^{(e)}\mathrel{+}=-N^Tn_s^TS\,d\Gamma.$$

**Prerequisites:** exactly the same cut-surface, normal, shape, and DOF data as
`K_up`, but rows and columns are reversed.

**MFEM construction:** the same custom cut-surface loop as `K_up`.

**Powerful local check:** with the stated normal convention and identical
bases/quadrature,

$$M_{pu}^{(e)}=-(K_{up}^{(e)})^T.$$

This transpose relation is a sign/debugging check. The final coupled time-step
operator is still nonsymmetric because these blocks live in different global
matrices and receive different Newmark multipliers.

### 9.10 Incident-wave load `g`

**Meaning:** prescribed incident pressure-rate forcing at the inlet.

**Element-boundary size:** one entry per local pressure test basis.

**One entry:**

$$
g_i(t)=\int_{\Gamma_{ar,in}}
\frac2{\rho_ac_a}N_i\dot p_{in}(t)\,d\Gamma.
$$

**One boundary quadrature deposit:**

$$g_i\mathrel{+}=2(\rho_ac_a)^{-1}N_i\dot p_{in}(t)d\Gamma.$$

**Prerequisites:** inlet boundary marker, pressure space, `rho_a`, `c_a`, the
current time or current sampled `dot_p_in`, and ordinary boundary quadrature.

**MFEM formula:** pressure `mfem::LinearForm` plus
`mfem::BoundaryLFIntegrator` on the inlet marker.

**Checks:** length equals the number of pressure true DOFs after true-DOF
assembly; only inlet-supported entries are nonzero; changing the source sign
changes the response sign.

### 9.11 Combined matrices and vectors

After all subblocks are assembled and finalized:

$$
C_{uu}=\alpha_dM_{uu}+\beta_dK_{uu},
$$

then build block `M`, `C`, `K`, and

$$h=\begin{bmatrix}0\\g\end{bmatrix}.$$

The block prerequisites are:

| Object | Must already exist |
|---|---|
| `M` | `M_uu`, `M_pu`, `M_pp`, block offsets |
| `C` | `C_uu`, `C_pp`, block offsets |
| `K` | `K_uu`, `K_up`, `K_pp`, block offsets |
| `h(t)` | current `g(t)`, zero structural block, block offsets |
| `Khat` | monolithic `M`, `C`, `K`, Newmark constants |
| `hhat^n` | `h^n` and previous `v`, `v_dot`, `v_ddot` |

### 9.12 Contribution ledger by geometric location

This table prevents adding a correct formula on the wrong region.

| Geometric piece | Structural contribution | Acoustic contribution | Coupling/source contribution |
|---|---|---|---|
| Entirely solid element | physical `M_uu`, `K_uu` | `epsilon_f` times `M_pp`, `K_pp` | none |
| Entirely acoustic element | `epsilon_f` times `M_uu`, `K_uu` | physical `M_pp`, `K_pp` | none |
| Cut element, solid portion | physical structural; fictitious acoustic | as stated | none on volume |
| Cut element, acoustic portion | fictitious structural; physical acoustic | as stated | none on volume |
| Internal `phi=0` surface | none | none | `K_up`, `M_pu` |
| Exterior absorbing boundary | none | `C_pp` | none |
| Exterior inlet boundary | none | absorbing `C_pp` if also absorbing | `g(t)` |
| Exterior hard wall | none | no term; condition is natural | none |
| Exterior structural clamp | essential elimination later | none | none |

---

## 10. Hand-calculated 2D matrix contributions

This section makes the abstract entries visible. These small matrices are for
learning and unit tests. Production code should ask MFEM for basis values and
gradients instead of hard-coding node ordering.

### 10.1 Q4 basis on a reference square

On reference square `[0,1] x [0,1]`, one common Q4 ordering uses

$$
\begin{aligned}
N_1&=(1-\xi)(1-\eta),&
N_2&=\xi(1-\eta),\\
N_3&=\xi\eta,&
N_4&=(1-\xi)\eta.
\end{aligned}
$$

They satisfy two critical identities:

$$
\sum_{i=1}^4N_i=1,
\qquad
\sum_{i=1}^4\nabla N_i=0.
$$

The first exactly represents a constant field. The second explains why a
constant field produces zero diffusion/stiffness gradient.

### 10.2 Exact scalar Q4 mass matrix

For an uncut rectangular element of area `A` and constant coefficient `c`,

$$
\int_{\Omega_e}cN^TN\,d\Omega
=\frac{cA}{36}
\begin{bmatrix}
4&2&1&2\\
2&4&2&1\\
1&2&4&2\\
2&1&2&4
\end{bmatrix}.
$$

Use `c=rho_s` for one component block of `M_uu`. Use `c=1/K_a` for
`M_pp`. This is called a **consistent mass matrix**; off-diagonal entries are
expected and correct.

For 2D `M_uu` with scalar density and `byVDIM` ordering:

$$
M_{uu}^{(e)}=
\begin{bmatrix}
M_s&0\\
0&M_s
\end{bmatrix},
$$

where `M_s` is the `4x4` scalar matrix above with `c=rho_s`.

### 10.3 Exact scalar diffusion matrix on a square

For a square element and constant coefficient `a`,

$$
\int_{\Omega_e}a(\nabla N)^T\nabla N\,d\Omega
=\frac{a}{6}
\begin{bmatrix}
4&-1&-2&-1\\
-1&4&-1&-2\\
-2&-1&4&-1\\
-1&-2&-1&4
\end{bmatrix}.
$$

Use `a=1/rho_a` for an uncut square contribution to `K_pp`. Every row sums to
zero, so a constant nodal vector produces zero gradient energy.

The formula changes with aspect ratio, mapping, and cut region. MFEM's
`DiffusionIntegrator` handles those cases through the physical transformation
and quadrature.

### 10.4 One structural stiffness quadrature deposit

At the center of a square physical element of side `h`, the shape gradients in
the ordering above are

$$
\begin{aligned}
\nabla N_1&=\frac1{2h}[-1,-1]^T,&
\nabla N_2&=\frac1{2h}[1,-1]^T,\\
\nabla N_3&=\frac1{2h}[1,1]^T,&
\nabla N_4&=\frac1{2h}[-1,1]^T.
\end{aligned}
$$

For `byVDIM` ordering, the center-point strain matrix is

$$
B=\frac1{2h}
\begin{bmatrix}
-1&1&1&-1&0&0&0&0\\
0&0&0&0&-1&-1&1&1\\
-1&-1&1&1&-1&1&1&-1
\end{bmatrix}.
$$

One quadrature point adds

$$
K_{uu}^{(e)}\mathrel{+}=B^TD_{ps}B\,dV.
$$

The result is `8x8`: `B` is `3x8`, `D_ps` is `3x3`, so
`B^T D_ps B` is `(8x3)(3x3)(3x8) = 8x8`. This dimension check is often the
fastest way to catch a wrong transpose.

A production Q4 element normally uses enough quadrature points to integrate
the formula accurately. Sum `B^T D B dV` over all of them.

### 10.5 Exact absorbing-edge contribution

Take a straight exterior edge of length `L` with two linear pressure shapes.
For constant `1/(rho_a*c_a)`, its exact contribution is

$$
C_{pp}^{(edge)}=
\frac{L}{6\rho_ac_a}
\begin{bmatrix}
2&1\\
1&2
\end{bmatrix}.
$$

If `dot p_in` is constant along that same inlet edge,

$$
g^{(edge)}=
\frac{L\dot p_{in}}{\rho_ac_a}
\begin{bmatrix}1\\1\end{bmatrix}.
$$

The factor 2 in the incident-wave boundary law cancels the `L/2` integral of
each linear edge basis.

### 10.6 One interface quadrature deposit, written out

Suppose a cut interface passes through a Q4 element. At one interface
quadrature point in the element center:

$$
N_1=N_2=N_3=N_4=\frac14.
$$

For a teaching example, take `n_a=[1,0]^T` and let the complete quadrature
measure be `dGamma=L_q`. The pressure-to-structure deposit is

$$
\Delta K_{up}^{(e)}
=-\frac{L_q}{16}
\begin{bmatrix}
1&1&1&1\\
1&1&1&1\\
1&1&1&1\\
1&1&1&1\\
0&0&0&0\\
0&0&0&0\\
0&0&0&0\\
0&0&0&0
\end{bmatrix}.
$$

Only the x-displacement rows are nonzero because the normal has no y
component. With `n_s=-n_a`, the reciprocal acceleration-coupling deposit is

$$
\Delta M_{pu}^{(e)}
=\frac{L_q}{16}
\begin{bmatrix}
1&1&1&1&0&0&0&0\\
1&1&1&1&0&0&0&0\\
1&1&1&1&0&0&0&0\\
1&1&1&1&0&0&0&0
\end{bmatrix}.
$$

Therefore

$$
\Delta M_{pu}^{(e)}=-(\Delta K_{up}^{(e)})^T.
$$

This is one quadrature deposit, not generally the full exact interface matrix.
Production assembly repeats it for all interface quadrature points, where the
shape values, normal, and measure may vary.

### Checkpoint 3

1. Why are the lower four rows of the example `K_up` zero?
2. If `n_a` were `[0,1]^T`, which rows would be nonzero?
3. Which scalar Q4 matrix above can be reused as a block inside `M_uu`?

---

## 11. Constructing every matrix with the MFEM API

This chapter uses the shortest route that keeps the mathematics visible:

1. learn each standard term with `BilinearForm`;
2. for the real cut problem, reuse the same MFEM integrators inside one custom
   element loop with per-element cut rules;
3. write custom algebra only for the two internal-interface blocks.

### 11.1 Create the 2D or 3D mesh

Two-dimensional paper baseline:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
    nx, ny,
    mfem::Element::QUADRILATERAL,
    true,
    lx, ly);
```

Three-dimensional extension:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian3D(
    nx, ny, nz,
    mfem::Element::HEXAHEDRON,
    lx, ly, lz);
```

The level set cuts this background mesh. Do not remesh at each geometry update.

### 11.2 Create spaces and fields

```cpp
const int dim = mesh.Dimension();
const int fe_order = 1;

mfem::H1_FECollection fec(fe_order, dim);

mfem::FiniteElementSpace pressure_fes(&mesh, &fec);
mfem::FiniteElementSpace level_set_fes(&mesh, &fec);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &fec, dim, mfem::Ordering::byVDIM);

mfem::GridFunction phi_h(&level_set_fes);
mfem::GridFunction pressure(&pressure_fes);
mfem::GridFunction displacement(&displacement_fes);
```

Data roles are distinct:

- a `FiniteElementSpace` owns a DOF layout, not field values;
- a `GridFunction` stores field values in that layout;
- a `Coefficient` evaluates known data at quadrature points;
- a matrix acts on algebraic DOF vectors.

### 11.3 Fill and expose the level set

For a first geometry, project a known function:

```cpp
mfem::FunctionCoefficient initial_phi([](const mfem::Vector &x) {
    const mfem::real_t dx = x[0] - 0.5;
    const mfem::real_t dy = x[1] - 0.5;
    return 0.2 - std::sqrt(dx*dx + dy*dy); // positive inside solid
});

phi_h.ProjectCoefficient(initial_phi);
mfem::GridFunctionCoefficient phi_coeff(&phi_h);
```

When the optimizer later provides a true-DOF vector, transfer it with
`phi_h.SetFromTrueDofs(phi_true)`. The assembly code should only require a valid
`phi_h`; it should not know how the optimizer created it.

### 11.4 Create material coefficients

```cpp
const mfem::real_t K_a = rho_a * c_a * c_a;
const mfem::real_t mu_value = E / (2.0 * (1.0 + nu));

const mfem::real_t lambda_value =
    (dim == 2)
        ? E * nu / (1.0 - nu*nu)                    // plane stress
        : E * nu / ((1.0 + nu) * (1.0 - 2.0*nu)); // 3D

mfem::ConstantCoefficient rho_s_coeff(rho_s);
mfem::ConstantCoefficient lambda_coeff(lambda_value);
mfem::ConstantCoefficient mu_coeff(mu_value);
mfem::ConstantCoefficient inv_K_coeff(1.0 / K_a);
mfem::ConstantCoefficient inv_rho_coeff(1.0 / rho_a);
mfem::ConstantCoefficient inv_rho_c_coeff(1.0 / (rho_a * c_a));
```

These coefficient objects must outlive every integrator that stores a pointer
to them.

### 11.5 First learn ordinary, uncut assembly

The following is correct for an all-solid/all-fluid validation mesh or for
ordinary fitted subdomains. It demonstrates exactly which MFEM integrator owns
each mathematical formula. It is not yet the paper's cut-volume assembly.
The later cut-assembly snippets reuse the same short matrix names; treat the two
sections as alternative construction paths, not code to paste into one scope.

Structural mass:

```cpp
mfem::BilinearForm muu_form(&displacement_fes);
muu_form.AddDomainIntegrator(
    new mfem::VectorMassIntegrator(rho_s_coeff));
muu_form.Assemble();
muu_form.Finalize();
std::unique_ptr<mfem::SparseMatrix> Muu(muu_form.LoseMat());
```

Structural stiffness:

```cpp
mfem::BilinearForm kuu_form(&displacement_fes);
kuu_form.AddDomainIntegrator(
    new mfem::ElasticityIntegrator(lambda_coeff, mu_coeff));
kuu_form.Assemble();
kuu_form.Finalize();
std::unique_ptr<mfem::SparseMatrix> Kuu(kuu_form.LoseMat());
```

Acoustic mass:

```cpp
mfem::BilinearForm mpp_form(&pressure_fes);
mpp_form.AddDomainIntegrator(new mfem::MassIntegrator(inv_K_coeff));
mpp_form.Assemble();
mpp_form.Finalize();
std::unique_ptr<mfem::SparseMatrix> Mpp(mpp_form.LoseMat());
```

Acoustic stiffness:

```cpp
mfem::BilinearForm kpp_form(&pressure_fes);
kpp_form.AddDomainIntegrator(new mfem::DiffusionIntegrator(inv_rho_coeff));
kpp_form.Assemble();
kpp_form.Finalize();
std::unique_ptr<mfem::SparseMatrix> Kpp(kpp_form.LoseMat());
```

`LoseMat()` transfers matrix ownership from the form into the
`unique_ptr`. Without `LoseMat`, the form owns its assembled matrix.

### 11.6 Build boundary markers, `C_pp`, and `g`

MFEM boundary markers are arrays indexed by boundary attribute minus one. First
classify boundary attributes by geometry; do not assume attribute 1 is always
the inlet without checking the mesh.

```cpp
mfem::Array<int> absorbing_marker(mesh.bdr_attributes.Max());
mfem::Array<int> inlet_marker(mesh.bdr_attributes.Max());
mfem::Array<int> clamp_marker(mesh.bdr_attributes.Max());

absorbing_marker = 0;
inlet_marker = 0;
clamp_marker = 0;

absorbing_marker[inlet_attr - 1] = 1;
absorbing_marker[outlet_attr - 1] = 1;
inlet_marker[inlet_attr - 1] = 1;
clamp_marker[clamp_attr - 1] = 1;
```

Absorbing-boundary damping:

```cpp
mfem::BilinearForm cpp_form(&pressure_fes);
cpp_form.AddBoundaryIntegrator(
    new mfem::BoundaryMassIntegrator(inv_rho_c_coeff),
    absorbing_marker);
cpp_form.Assemble();
cpp_form.Finalize();
std::unique_ptr<mfem::SparseMatrix> Cpp(cpp_form.LoseMat());
```

At time `t_n`, assemble the current incident load:

```cpp
const mfem::real_t source_value =
    2.0 * dot_p_in / (rho_a * c_a);
mfem::ConstantCoefficient source_coeff(source_value);

mfem::LinearForm inlet_load(&pressure_fes);
inlet_load.AddBoundaryIntegrator(
    new mfem::BoundaryLFIntegrator(source_coeff),
    inlet_marker);
inlet_load.Assemble();

mfem::Vector g(inlet_load.Size());
g = inlet_load;
```

For clarity this creates the scalar source coefficient at each time step. A
later optimization can keep a mutable time-dependent coefficient and reuse the
form's structure.

### 11.7 Generate cut-volume and interface rules

MFEM's Algoim rule integrates the region where its level-set coefficient is
positive. With `phi>0` as solid, negate the coefficient to request the fluid
region:

```cpp
class NegatedCoefficient final : public mfem::Coefficient {
public:
    explicit NegatedCoefficient(mfem::Coefficient &source)
        : source_(source) {}

    mfem::real_t Eval(mfem::ElementTransformation &T,
                      const mfem::IntegrationPoint &ip) override {
        return -source_.Eval(T, ip);
    }

private:
    mfem::Coefficient &source_;
};

NegatedCoefficient minus_phi(phi_coeff);

const int cut_order = 4;
const int level_set_order = 1;

mfem::AlgoimIntegrationRules solid_rules(
    cut_order, phi_coeff, level_set_order);
mfem::AlgoimIntegrationRules acoustic_rules(
    cut_order, minus_phi, level_set_order);
```

For each element:

```cpp
mfem::IntegrationRule solid_ir;
mfem::IntegrationRule acoustic_ir;
mfem::IntegrationRule interface_ir;
mfem::Vector surface_weights;

solid_rules.GetVolumeIntegrationRule(*T, solid_ir);       // phi > 0
acoustic_rules.GetVolumeIntegrationRule(*T, acoustic_ir); // phi < 0
solid_rules.GetSurfaceIntegrationRule(*T, interface_ir);  // phi = 0
solid_rules.GetSurfaceWeights(*T, interface_ir, surface_weights);
```

For a volume quadrature point,

```cpp
const mfem::real_t dV = ip.weight * T->Weight();
```

For a cut-surface point, include the extra Algoim factor:

```cpp
const mfem::real_t dGamma =
    ip.weight * surface_weights[q] * T->Weight();
```

Omitting `surface_weights[q]` gives an incorrect physical interface measure.

### 11.8 Assemble all four cut-volume blocks element by element

For the serial conforming Cartesian baseline, create open sparse matrices on
the FE-space VDOFs:

```cpp
mfem::SparseMatrix Muu(displacement_fes.GetVSize());
mfem::SparseMatrix Kuu(displacement_fes.GetVSize());
mfem::SparseMatrix Mpp(pressure_fes.GetVSize());
mfem::SparseMatrix Kpp(pressure_fes.GetVSize());
```

Create the standard local formulas once:

```cpp
mfem::VectorMassIntegrator muu_integrator(rho_s_coeff);
muu_integrator.SetVDim(dim);
mfem::ElasticityIntegrator kuu_integrator(lambda_coeff, mu_coeff);
mfem::MassIntegrator mpp_integrator(inv_K_coeff);
mfem::DiffusionIntegrator kpp_integrator(inv_rho_coeff);
```

The core element loop is:

```cpp
for (int e = 0; e < mesh.GetNE(); ++e) {
    mfem::ElementTransformation *T = mesh.GetElementTransformation(e);
    const mfem::FiniteElement &u_fe = *displacement_fes.GetFE(e);
    const mfem::FiniteElement &p_fe = *pressure_fes.GetFE(e);

    mfem::Array<int> u_vdofs;
    mfem::Array<int> p_dofs;
    displacement_fes.GetElementVDofs(e, u_vdofs);
    pressure_fes.GetElementDofs(e, p_dofs);

    mfem::IntegrationRule solid_ir;
    mfem::IntegrationRule acoustic_ir;
    solid_rules.GetVolumeIntegrationRule(*T, solid_ir);
    acoustic_rules.GetVolumeIntegrationRule(*T, acoustic_ir);

    mfem::DenseMatrix Muu_e(u_vdofs.Size());
    mfem::DenseMatrix Kuu_e(u_vdofs.Size());
    mfem::DenseMatrix Mpp_e(p_dofs.Size());
    mfem::DenseMatrix Kpp_e(p_dofs.Size());
    Muu_e = 0.0;
    Kuu_e = 0.0;
    Mpp_e = 0.0;
    Kpp_e = 0.0;

    mfem::DenseMatrix tmp;

    // Solid volume: physical structure, fictitious acoustics.
    if (solid_ir.GetNPoints() > 0) {
        muu_integrator.SetIntRule(&solid_ir);
        muu_integrator.AssembleElementMatrix(u_fe, *T, tmp);
        Muu_e.Add(1.0, tmp);

        kuu_integrator.SetIntRule(&solid_ir);
        kuu_integrator.AssembleElementMatrix(u_fe, *T, tmp);
        Kuu_e.Add(1.0, tmp);

        mpp_integrator.SetIntRule(&solid_ir);
        mpp_integrator.AssembleElementMatrix(p_fe, *T, tmp);
        Mpp_e.Add(epsilon_f, tmp);

        kpp_integrator.SetIntRule(&solid_ir);
        kpp_integrator.AssembleElementMatrix(p_fe, *T, tmp);
        Kpp_e.Add(epsilon_f, tmp);
    }

    // Acoustic volume: fictitious structure, physical acoustics.
    if (acoustic_ir.GetNPoints() > 0) {
        muu_integrator.SetIntRule(&acoustic_ir);
        muu_integrator.AssembleElementMatrix(u_fe, *T, tmp);
        Muu_e.Add(epsilon_f, tmp);

        kuu_integrator.SetIntRule(&acoustic_ir);
        kuu_integrator.AssembleElementMatrix(u_fe, *T, tmp);
        Kuu_e.Add(epsilon_f, tmp);

        mpp_integrator.SetIntRule(&acoustic_ir);
        mpp_integrator.AssembleElementMatrix(p_fe, *T, tmp);
        Mpp_e.Add(1.0, tmp);

        kpp_integrator.SetIntRule(&acoustic_ir);
        kpp_integrator.AssembleElementMatrix(p_fe, *T, tmp);
        Kpp_e.Add(1.0, tmp);
    }

    Muu.AddSubMatrix(u_vdofs, u_vdofs, Muu_e);
    Kuu.AddSubMatrix(u_vdofs, u_vdofs, Kuu_e);
    Mpp.AddSubMatrix(p_dofs, p_dofs, Mpp_e);
    Kpp.AddSubMatrix(p_dofs, p_dofs, Kpp_e);
}

Muu.Finalize();
Kuu.Finalize();
Mpp.Finalize();
Kpp.Finalize();
```

This is the code version of the contribution ledger in Section 9.12.

For the initial serial Cartesian Q1 mesh, local conforming VDOFs and true DOFs
coincide. If nonconforming refinement or MPI is introduced later, replace this
shortcut with MFEM conforming/parallel assembly rather than assuming
`GetVSize()==GetTrueVSize()`.

### 11.9 Assemble `K_up` and `M_pu` in the same element loop

Create rectangular global matrices before the loop:

```cpp
mfem::SparseMatrix Kup(
    displacement_fes.GetVSize(), pressure_fes.GetVSize());
mfem::SparseMatrix Mpu(
    pressure_fes.GetVSize(), displacement_fes.GetVSize());
```

Inside each element iteration, after obtaining `u_vdofs`, `p_dofs`, and `T`:

```cpp
mfem::IntegrationRule interface_ir;
mfem::Vector surface_weights;
solid_rules.GetSurfaceIntegrationRule(*T, interface_ir);
solid_rules.GetSurfaceWeights(*T, interface_ir, surface_weights);

mfem::DenseMatrix Kup_e(u_vdofs.Size(), p_dofs.Size());
mfem::DenseMatrix Mpu_e(p_dofs.Size(), u_vdofs.Size());
Kup_e = 0.0;
Mpu_e = 0.0;

mfem::Vector u_shape(u_fe.GetDof());
mfem::Vector p_shape(p_fe.GetDof());
mfem::Vector grad_phi(dim);

for (int q = 0; q < interface_ir.GetNPoints(); ++q) {
    const mfem::IntegrationPoint &ip = interface_ir.IntPoint(q);
    T->SetIntPoint(&ip);

    u_fe.CalcShape(ip, u_shape);
    p_fe.CalcShape(ip, p_shape);

    phi_h.GetGradient(*T, grad_phi);
    const mfem::real_t grad_norm = grad_phi.Norml2();
    MFEM_VERIFY(grad_norm > 1e-12,
                "Cannot define interface normal: |grad(phi)| is too small");
    grad_phi /= grad_norm;
    const mfem::Vector &n_acoustic = grad_phi;

    const mfem::real_t dGamma =
        ip.weight * surface_weights[q] * T->Weight();

    for (int c = 0; c < dim; ++c) {
        for (int a = 0; a < u_shape.Size(); ++a) {
            const int ua = c * u_shape.Size() + a; // byVDIM
            for (int b = 0; b < p_shape.Size(); ++b) {
                const mfem::real_t entry =
                    dGamma * u_shape[a] * n_acoustic[c] * p_shape[b];

                Kup_e(ua, b) -= entry;
                Mpu_e(b, ua) += entry; // n_solid = -n_acoustic
            }
        }
    }
}

Kup.AddSubMatrix(u_vdofs, p_dofs, Kup_e);
Mpu.AddSubMatrix(p_dofs, u_vdofs, Mpu_e);
```

After all elements:

```cpp
Kup.Finalize();
Mpu.Finalize();
```

The loop is dimension-independent: `dim=2` produces `8x4` local coupling
blocks and `dim=3` produces `24x8` blocks for H8/Q1.

### 11.10 Construct `C_uu`

After `Muu` and `Kuu` are finalized:

```cpp
std::unique_ptr<mfem::SparseMatrix> Cuu(
    mfem::Add(alpha_d, Muu, beta_d, Kuu));
```

No element loop is needed. This line is exactly
`C_uu = alpha_d*M_uu + beta_d*K_uu`.

### 11.11 Build coupled block matrices

Create true-DOF offsets:

```cpp
mfem::Array<int> offsets(3);
offsets[0] = 0;
offsets[1] = displacement_fes.GetTrueVSize();
offsets[2] = offsets[1] + pressure_fes.GetTrueVSize();
```

For the serial conforming baseline, place the finalized blocks:

```cpp
mfem::BlockMatrix M_block(offsets);
M_block.SetBlock(0, 0, &Muu);
M_block.SetBlock(1, 0, &Mpu);
M_block.SetBlock(1, 1, &Mpp);

mfem::BlockMatrix C_block(offsets);
C_block.SetBlock(0, 0, Cuu.get());
C_block.SetBlock(1, 1, Cpp.get());

mfem::BlockMatrix K_block(offsets);
K_block.SetBlock(0, 0, &Kuu);
K_block.SetBlock(0, 1, &Kup);
K_block.SetBlock(1, 1, &Kpp);
```

Unset blocks mean zero. `SetBlock` stores raw pointers; the matrices that own
the data must outlive the `BlockMatrix`.

For the first explicit Newmark implementation, make monolithic sparse copies:

```cpp
std::unique_ptr<mfem::SparseMatrix> M(M_block.CreateMonolithic());
std::unique_ptr<mfem::SparseMatrix> C(C_block.CreateMonolithic());
std::unique_ptr<mfem::SparseMatrix> K(K_block.CreateMonolithic());
```

These act on one monolithic vector ordered as `[all u true DOFs, all p true
DOFs]`.

### 11.12 Build the load block vector

```cpp
mfem::BlockVector h(offsets);
h = 0.0;
h.GetBlock(1) = g;
```

The structural block is zero because the external excitation enters through
the acoustic inlet. Interface pressure forces are already in `K_up`; do not add
them again to `h`.

---

## 12. Impose boundaries, advance time, and solve

Matrix assembly creates the spatial operators. It does not yet compute a
pressure or displacement solution. The remaining sequence is:

```text
find clamped true DOFs
  -> create Newmark effective matrix
  -> eliminate clamp rows/columns
  -> at each time: assemble source and effective right-hand side
  -> call a linear solver
  -> update velocity and acceleration
  -> copy true DOFs into GridFunctions when output is needed
```

### 12.1 Find clamped structural true DOFs

```cpp
mfem::Array<int> ess_u_tdofs;
displacement_fes.GetEssentialTrueDofs(clamp_marker, ess_u_tdofs);
```

These indices refer to the displacement block. Because displacement is the
first monolithic block, the same integer indices address clamped rows in the
combined vector. If block order changes, the offset must be applied.

Hard acoustic walls require no essential pressure DOFs. Their zero normal flux
is a natural boundary condition already represented by the absence of a
boundary term.

### 12.2 Average-acceleration Newmark constants

The paper uses

$$
\widetilde\beta=\frac14,\qquad
\widetilde\gamma=\frac12.
$$

For time step `dt`, define

$$
\begin{aligned}
a_1&=1-\widetilde\gamma/\widetilde\beta,&
a_2&=(1-\widetilde\gamma/(2\widetilde\beta))\Delta t,\\
a_3&=\widetilde\gamma/(\widetilde\beta\Delta t),&
a_4&=1/(\widetilde\beta\Delta t),\\
a_5&=1/(2\widetilde\beta)-1,&
a_6&=1/(\widetilde\beta\Delta t^2).
\end{aligned}
$$

For `beta=1/4` and `gamma=1/2`, these simplify to

$$
a_1=-1,\quad a_2=0,\quad a_3=\frac2{\Delta t},\quad
a_4=\frac4{\Delta t},\quad a_5=1,\quad
a_6=\frac4{\Delta t^2}.
$$

C++:

```cpp
const mfem::real_t beta = 0.25;
const mfem::real_t gamma = 0.5;

const mfem::real_t a1 = 1.0 - gamma / beta;
const mfem::real_t a2 = (1.0 - gamma / (2.0*beta)) * dt;
const mfem::real_t a3 = gamma / (beta * dt);
const mfem::real_t a4 = 1.0 / (beta * dt);
const mfem::real_t a5 = 1.0 / (2.0*beta) - 1.0;
const mfem::real_t a6 = 1.0 / (beta * dt * dt);
```

### 12.3 Build the effective matrix

Each time step solves

$$
\widehat K v^n=\widehat h^n,
$$

where

$$
\widehat K=K+a_6M+a_3C.
$$

MFEM sparse additions return newly allocated matrices:

```cpp
std::unique_ptr<mfem::SparseMatrix> K_plus_M(
    mfem::Add(1.0, *K, a6, *M));
std::unique_ptr<mfem::SparseMatrix> Khat(
    mfem::Add(1.0, *K_plus_M, a3, *C));
```

If the geometry, material, damping, and `dt` remain fixed, `Khat` is constant
throughout the time run. Build and prepare it once, not once per step.

For a homogeneous clamp, eliminate its rows and columns once:

```cpp
for (int i = 0; i < ess_u_tdofs.Size(); ++i) {
    Khat->EliminateRowCol(ess_u_tdofs[i]);
}
```

At every time step set the matching effective RHS entries to zero. A nonzero
prescribed displacement needs the more general elimination overload that also
modifies the RHS.

### 12.4 Attach a linear solver

The coupled effective matrix is generally nonsymmetric. Use GMRES as the safe
baseline, not conjugate gradients:

```cpp
mfem::GMRESSolver solver;
solver.SetRelTol(1e-10);
solver.SetAbsTol(0.0);
solver.SetMaxIter(500);
solver.SetPrintLevel(1);
solver.SetOperator(*Khat);
```

`SetOperator` prepares/attaches the matrix. It does not solve. The solve occurs
at

```cpp
solver.Mult(hhat, v_new);
```

For a small serial verification problem, a direct solver is convenient if the
matching external library was enabled when MFEM was built. This repository's
current default MFEM configuration does not enable SuiteSparse/UMFPACK or the
paper's PETSc/MUMPS stack, so GMRES is the portable baseline. A serious large
problem will need a suitable block preconditioner.

### 12.5 Initial state

Allocate one monolithic vector for each kinematic state:

```cpp
const int total_size = offsets.Last();

mfem::Vector v(total_size);
mfem::Vector v_dot(total_size);
mfem::Vector v_ddot(total_size);
v = 0.0;
v_dot = 0.0;
v_ddot = 0.0;
```

In general, initial acceleration satisfies

$$
M\ddot v^0=h^0-C\dot v^0-Kv^0.
$$

If the state, rate, and initial load all vanish, `v_ddot=0` is consistent. If
`h^0` is nonzero, solve this mass system instead of silently assuming zero
acceleration.

### 12.6 Form the effective right-hand side

From the previous state,

$$
\begin{aligned}
\widehat h^n={}&h^n
+M(a_4\dot v^{n-1}+a_5\ddot v^{n-1}+a_6v^{n-1})\\
&+C(-a_1\dot v^{n-1}-a_2\ddot v^{n-1}+a_3v^{n-1}).
\end{aligned}
$$

The corresponding MFEM vector operations are:

```cpp
mfem::Vector hhat(total_size);
mfem::Vector predictor(total_size);
mfem::Vector tmp(total_size);

hhat = h; // h contains the newly assembled g(t_n)

predictor = v;
predictor *= a6;
predictor.Add(a4, v_dot);
predictor.Add(a5, v_ddot);
M->Mult(predictor, tmp);
hhat += tmp;

predictor = v;
predictor *= a3;
predictor.Add(-a1, v_dot);
predictor.Add(-a2, v_ddot);
C->Mult(predictor, tmp);
hhat += tmp;

for (int i = 0; i < ess_u_tdofs.Size(); ++i) {
    hhat[ess_u_tdofs[i]] = 0.0;
}
```

`M->Mult` and `C->Mult` here mean ordinary matrix-vector products. They are not
linear solves. `solver.Mult` is the solver call.

### 12.7 Solve and update rates

After solving for the new state,

$$
\dot v^n=a_1\dot v^{n-1}+a_2\ddot v^{n-1}
+a_3(v^n-v^{n-1}),
$$

$$
\ddot v^n=-a_4\dot v^{n-1}-a_5\ddot v^{n-1}
+a_6(v^n-v^{n-1}).
$$

C++:

```cpp
mfem::Vector v_new(total_size);
mfem::Vector v_dot_new(total_size);
mfem::Vector v_ddot_new(total_size);

v_new = 0.0;
solver.Mult(hhat, v_new);
MFEM_VERIFY(solver.GetConverged(), "GMRES failed to converge");

v_dot_new = v_dot;
v_dot_new *= a1;
v_dot_new.Add(a2, v_ddot);
v_dot_new.Add(a3, v_new);
v_dot_new.Add(-a3, v);

v_ddot_new = v_dot;
v_ddot_new *= -a4;
v_ddot_new.Add(-a5, v_ddot);
v_ddot_new.Add(a6, v_new);
v_ddot_new.Add(-a6, v);

v = v_new;
v_dot = v_dot_new;
v_ddot = v_ddot_new;
```

### 12.8 Minimal paper-faithful time loop

Putting the operations in execution order:

```cpp
const int num_steps = static_cast<int>(std::ceil(final_time / dt));

for (int step = 1; step <= num_steps; ++step) {
    const mfem::real_t t = step * dt;

    // 1. Compute or look up dot_p_in(t).
    // 2. Assemble inlet LinearForm g(t).
    // 3. Set h = [0, g(t)].
    // 4. Form hhat from h and the previous v, v_dot, v_ddot.
    // 5. Zero hhat at clamped displacement true DOFs.
    // 6. Solve Khat * v_new = hhat with solver.Mult(...).
    // 7. Update v_dot_new and v_ddot_new.
    // 8. Store measurements or states required later.
    // 9. Replace previous state with the new state.
}
```

Keep the explicit loop for the paper reproduction because it exposes `Khat`,
`hhat`, and every state needed later for FFT measurements and a fully discrete
adjoint.

### 12.9 MFEM's built-in Newmark runner

MFEM can also drive a second-order system through
`mfem::NewmarkSolver`. You provide an operator that computes acceleration and
implicit acceleration solves:

```cpp
class CoupledOperator final
    : public mfem::SecondOrderTimeDependentOperator {
public:
    explicit CoupledOperator(int size)
        : mfem::SecondOrderTimeDependentOperator(size) {}

    void Mult(const mfem::Vector &x,
              const mfem::Vector &dxdt,
              mfem::Vector &d2xdt2) const override;

    void ImplicitSolve(mfem::real_t fac0,
                       mfem::real_t fac1,
                       const mfem::Vector &x,
                       const mfem::Vector &dxdt,
                       mfem::Vector &d2xdt2) override;
};

CoupledOperator op(total_size);
mfem::NewmarkSolver newmark(0.25, 0.5);
newmark.Init(op);

mfem::real_t t = 0.0;
newmark.Step(v, v_dot, t, dt);
```

Inside `Mult`, the acceleration conceptually solves

$$
M\ddot v=h(t)-C\dot v-Kv.
$$

MFEM passes predictor state `x`, predictor rate `dxdt`, and asks
`ImplicitSolve` for new acceleration `k` satisfying

$$
k=f(x+fac0\,k,\;dxdt+fac1\,k,\;t).
$$

For this linear coupled system, assemble and solve

$$
(M+fac1\,C+fac0\,K)k=h(t)-C\,dxdt-Kx.
$$

Therefore the two operator methods have this algebraic shape:

```cpp
// Mult(x, dxdt, k):
//     rhs = h(t) - C*dxdt - K*x
//     solve M*k = rhs

// ImplicitSolve(fac0, fac1, x, dxdt, k):
//     A   = M + fac1*C + fac0*K
//     rhs = h(t) - C*dxdt - K*x
//     solve A*k = rhs
```

`NewmarkSolver::Step` calls `ImplicitSolve(beta*dt*dt,
gamma*dt,...)`. Apply clamp constraints inside these solves just as in the
explicit route. Read MFEM Example 23 before implementing the class. The
built-in route is useful for a forward-only smoke test. The explicit paper loop
above is easier to audit against the stated equations and later differentiate.

### 12.10 Recover pressure and displacement fields

The solved vector uses true-DOF block ordering. Copy it into a block view, then
into `GridFunction`s:

```cpp
mfem::BlockVector state(v, offsets); // a block view of the solved vector

displacement.SetFromTrueDofs(state.GetBlock(0));
pressure.SetFromTrueDofs(state.GetBlock(1));
```

Now `pressure` and `displacement` can be sampled, saved, or sent to GLVis.
`SetFromTrueDofs` does not solve anything; it maps algebraic results back to FE
storage.

### 12.11 What "run" means in this API

There is no single magical `run_vibroacoustics()` MFEM call. Running the model
means your application performs these MFEM-assisted operations in order:

| Stage | Main call(s) | Result |
|---|---|---|
| Mesh | `MakeCartesian2D/3D` | geometric elements |
| Spaces | `FiniteElementSpace` constructors | DOF layouts |
| Geometry | `phi_h.SetFromTrueDofs` or projection | phase field |
| Quadrature | `AlgoimIntegrationRules` methods | cut points/weights |
| Local assembly | `AssembleElementMatrix` | dense element matrices |
| Global assembly | `AddSubMatrix`, `Finalize` | sparse blocks |
| Boundary assembly | `BilinearForm`, `LinearForm` | `C_pp`, `g` |
| Coupling | custom cut-surface loop | `K_up`, `M_pu` |
| Block creation | `SetBlock`, `CreateMonolithic` | `M`, `C`, `K` |
| Time system | `mfem::Add` and vector operations | `Khat`, `hhat` |
| Solve | `solver.Mult(hhat,v_new)` | new algebraic state |
| Field recovery | `SetFromTrueDofs` | pressure/displacement fields |

---

## 13. Verification: prove each layer before coupling everything

A solver that compiles can still solve the wrong equations. Verification should
follow the same layers used to construct the model.

### 13.1 Gate 1: one ordinary element

On an uncut square with constant coefficients:

- compare MFEM `MassIntegrator` output with the Q4 matrix in Section 10.2;
- compare `DiffusionIntegrator` output with Section 10.3;
- check dimensions and symmetry;
- multiply `K_pp` by a constant vector and confirm a near-zero result.

If this fails, do not investigate CutFEM or Newmark. The basic space,
coefficient, ordering, or transformation is wrong.

### 13.2 Gate 2: cut-volume integration alone

Choose a level set with known geometry, such as a vertical line through a unit
square. Integrate the constant function 1 over both phases:

$$
A_s=\int_{\phi>0}1\,d\Omega,
\qquad
A_a=\int_{\phi<0}1\,d\Omega.
$$

Check

$$A_s+A_a=A_{element}$$

to the expected quadrature tolerance. Also compare interface length with the
known value. This isolates Algoim rules and surface weights from all physics.

### 13.3 Gate 3: diagonal physics separately

Acoustics only:

- temporarily set coupling blocks to zero;
- test hard-wall behavior and the absorbing boundary;
- refine the mesh and time step and check response convergence.

Structure only:

- temporarily set coupling blocks to zero;
- check that clamp DOFs remain exactly zero;
- test a static or modal case with a known qualitative deformation;
- check that rigid motion costs no stiffness before the clamp is applied.

### 13.4 Gate 4: interface algebra

For every cut element, verify

$$
\|M_{pu}^{(e)}+(K_{up}^{(e)})^T\|
$$

is near roundoff for the stated convention. Then reverse the level-set sign in
a controlled test and confirm that the interpreted phase and normal changes are
understood. Do not "fix" only one coupling sign.

Useful debug output for one cut element:

- element number;
- number of solid, acoustic, and interface quadrature points;
- sum of volume and surface measures;
- `grad(phi)` and `n_a` at one interface point;
- sizes and norms of local `K_up` and `M_pu`;
- transpose-check norm above.

### 13.5 Gate 5: global block layout

Assert dimensions before building monolithic matrices:

```cpp
MFEM_VERIFY(Muu.Height() == offsets[1], "bad Muu rows");
MFEM_VERIFY(Muu.Width()  == offsets[1], "bad Muu cols");
MFEM_VERIFY(Kup.Height() == offsets[1], "bad Kup rows");
MFEM_VERIFY(Kup.Width()  == offsets[2] - offsets[1], "bad Kup cols");
MFEM_VERIFY(Mpu.Height() == offsets[2] - offsets[1], "bad Mpu rows");
MFEM_VERIFY(Mpu.Width()  == offsets[1], "bad Mpu cols");
```

Use a tiny mesh and print matrices in dense/Matlab form. Confirm visually that
`K_up` is in block `(0,1)` and `M_pu` is in block `(1,0)`.

### 13.6 Gate 6: Newmark without coupling, then with coupling

First test the time loop on a small scalar second-order system with a known
solution. Then run uncoupled acoustic and structural systems. Finally enable
`K_up` and `M_pu`.

At each step, compute the algebraic residual

$$
r^n=M\ddot v^n+C\dot v^n+Kv^n-h^n.
$$

The unconstrained residual norm should be consistent with the linear solver
tolerance and floating-point error. A small `Khat*v-hhat` residual alone checks
the solve; the equation above additionally checks the Newmark update formulas.

### 13.7 Gate 7: convergence, not one pretty plot

Repeat a small case with:

- smaller mesh size;
- smaller `dt`;
- higher cut quadrature order;
- several reasonable `epsilon_f` values.

A physical feature that disappears under these checks may be a numerical
artifact. The final settings should be justified by convergence rather than by
one visually plausible response.

### 13.8 Common mistakes and their symptoms

| Mistake | Typical symptom | Direct check |
|---|---|---|
| Trial/test reversed | coupling block has wrong shape | compare rows/columns with Section 9 |
| `K_up` put in `M` | wrong transient scaling | read both block-row equations |
| `M_pu` put in `K` | incorrect acceleration coupling | read acoustic weak form |
| One coupling sign flipped | nonphysical energy transfer | local transpose-sign check |
| `n_a` points wrong way | both interface signs reversed | inspect `phi` on both sides |
| Surface weight omitted | mesh/mapping-dependent coupling magnitude | known interface-length test |
| Acoustic phase not negated | integrates solid twice | phase volumes must sum to element volume |
| 3D lambda used in 2D plane stress | structure too stiff | inspect coefficient formula |
| `CGSolver` used | failure or false convergence | coupled `Khat` is nonsymmetric |
| Clamp applied only to `K_uu` | constrained DOFs move through other blocks | inspect monolithic constrained rows |
| `Assemble` mistaken for solve | fields remain unchanged | solve occurs at `solver.Mult` |
| `SetBlock` owner destroyed | crash/use-after-free | enforce lifetime order |
| Rebuild `Khat` every step unnecessarily | slow run | reuse when geometry and `dt` are fixed |

---

## 14. Recommended implementation order

Build the smallest observable piece at each milestone.

### Milestone A: FEM and MFEM plumbing

1. Create a 2D quadrilateral mesh.
2. Create scalar pressure and vector displacement spaces.
3. Print element counts, VDOF sizes, and true-DOF sizes.
4. Project a simple `phi_h` and save it.

Done means the geometry, spaces, and lifetimes are correct.

### Milestone B: ordinary element matrices

1. Build uncut `M_pp` and `K_pp` with standard forms.
2. Compare one Q4 element against Section 10.
3. Build uncut `M_uu` and `K_uu`.
4. Verify the 2D plane-stress coefficient branch.

Done means standard volume physics is understood.

### Milestone C: cut quadrature and phase volume blocks

1. Verify known solid/acoustic areas and interface length.
2. Run the per-element volume loop.
3. Assemble physical and fictitious contributions separately.
4. Compare all-solid/all-acoustic limits with ordinary assembly.

Done means `M_uu`, `K_uu`, `M_pp`, and `K_pp` are trustworthy.

### Milestone D: exterior conditions

1. Classify boundary attributes geometrically.
2. Assemble `C_pp` only on absorbing boundaries.
3. Assemble a simple constant `g` on the inlet.
4. Find clamp true DOFs.

Done means boundary support is visible in matrix/vector nonzeros.

### Milestone E: internal interface

1. Assemble `K_up` and `M_pu` for one cut element.
2. Pass the transpose-sign check.
3. Assemble them globally.
4. Confirm block dimensions and locations.

Done means the two physics are connected with the correct direction and sign.

### Milestone F: solve one time step

1. Form monolithic `M`, `C`, `K`.
2. Form `Khat`.
3. Apply the homogeneous clamp.
4. Form one `hhat` and call GMRES.
5. Check the residual.

Done means MFEM has actually solved one coupled algebraic system.

### Milestone G: run the transient model

1. Initialize consistent `v`, `v_dot`, and `v_ddot`.
2. Loop through time.
3. Update Newmark rates.
4. Recover `GridFunction`s at selected steps.
5. Store an outlet pressure measurement.
6. Perform refinement and tolerance studies.

Only after this gate should FFT objectives, adjoints, and optimization be
connected.

### The one-page construction recipe

```text
INPUT DATA
mesh dimensions/counts
phi_h
rho_s, E, nu
rho_a, c_a
epsilon_f, alpha_d, beta_d
boundary attributes
dt, T, source history

DERIVE
K_a, lambda, mu
markers and essential true DOFs
block offsets

ASSEMBLE VOLUMES PER ELEMENT
Muu <- VectorMassIntegrator on solid + epsilon_f on acoustic
Kuu <- ElasticityIntegrator on solid + epsilon_f on acoustic
Mpp <- MassIntegrator on acoustic + epsilon_f on solid
Kpp <- DiffusionIntegrator on acoustic + epsilon_f on solid

ASSEMBLE INTERFACE PER CUT ELEMENT
Kup <- - integral S^T n_a N
Mpu <- - integral N^T n_s^T S

ASSEMBLE EXTERIOR BOUNDARY
Cpp <- BoundaryMassIntegrator on absorbing marker
g(t) <- BoundaryLFIntegrator on inlet marker

COMBINE
Cuu <- alpha_d Muu + beta_d Kuu
M <- [[Muu, 0], [Mpu, Mpp]]
C <- [[Cuu, 0], [0, Cpp]]
K <- [[Kuu, Kup], [0, Kpp]]
h <- [0, g]

RUN
Khat <- K + a6 M + a3 C
apply clamp
for each time step:
    assemble g and h
    build hhat
    solve Khat v_new = hhat
    update v_dot and v_ddot
    measure/save fields
```

---

## 15. Checkpoint answers

### Checkpoint 1

1. No. FEM stores finitely many basis coefficients and interpolates a field.
2. A trial basis is associated with matrix column `j`.
3. A local basis overlaps only nearby bases, so distant DOFs do not interact.

### Checkpoint 2

1. Pressure is the trial unknown applying traction to a displacement test, so
   `K_up` has displacement rows and pressure columns.
2. The acoustic interface condition contains normal structural acceleration,
   so its discrete displacement coupling multiplies `ddot u`.
3. `C_pp` contains the first-order absorbing-boundary `dot p` term.

### Checkpoint 3

1. The chosen normal is purely x-directed, so it has no y traction component.
2. The four y-displacement rows would be nonzero and the x rows would be zero.
3. The scalar consistent mass matrix is repeated on the component diagonal of
   `M_uu` when density is scalar.

---

## 16. Sources and why the teaching order looks like this

### Physics and implementation sources

- C. B. Dilgen and N. Aage,
  [*Topology optimization of transient vibroacoustic problems for broadband
  filter design using cut elements*](https://doi.org/10.1016/j.finel.2024.104123),
  2024.
- C. B. Dilgen and N. Aage,
  [*Generalized shape optimization of transient vibroacoustic problems using
  cut elements*](https://doi.org/10.1002/nme.6591), 2021. This earlier open
  paper supplies the detailed coupled weak-form background referenced by the
  2024 paper.
- MFEM,
  [Bilinear Form Integrators](https://mfem.org/bilininteg/), for the relationship
  between weak integrals, local element matrices, and global assembly.
- MFEM,
  [Integration](https://mfem.org/integration/), for reference elements,
  quadrature weights, and Jacobians.
- MFEM 4.9,
  [`ElasticityIntegrator`](https://docs.mfem.org/4.9/classmfem_1_1ElasticityIntegrator.html)
  and
  [`VectorMassIntegrator`](https://docs.mfem.org/4.9/classmfem_1_1VectorMassIntegrator.html)
  API documentation.
- MFEM,
  [Example 38 source](https://docs.mfem.org/4.9/ex38_8cpp_source.html), for
  cut-volume/cut-surface integration and surface transformation weights.
- MFEM,
  [example catalog](https://mfem.org/examples/), especially Example 0 for the
  smallest assembly-and-solve path and Example 23 for second-order time
  integration.

### Pedagogical techniques deliberately used

This document starts with a fully worked scalar element, keeps related math and
code together, repeats a fixed question structure for each matrix, and only
then fades toward the full assembly loop. That choice is based on evidence that
worked examples reduce unproductive cognitive load for novices and improve
initial learning:

- Van Gog et al.,
  [*Effects of worked examples, example-problem, and problem-example pairs on
  novices' learning*](https://doi.org/10.1016/j.cedpsych.2010.10.004).
- Van Gog and Sweller,
  [*Cognitive Load Theory: Advances in Research on Worked Examples,
  Animations, and Cognitive Load
  Measurement*](https://doi.org/10.1007/s10648-010-9145-4).

The concrete Q4 matrices, repeated verbal/mathematical/code mappings, and short
recall checkpoints apply the supported strategies of concrete examples,
elaboration, dual coding, and retrieval practice:

- Weinstein, Sumeracki, and Caviglioli,
  [*Teaching the science of learning*](https://pubmed.ncbi.nlm.nih.gov/29399621/).
- Sana and Yan,
  [*Interleaving Retrieval Practice Promotes Science
  Learning*](https://pubmed.ncbi.nlm.nih.gov/35436145/).

The checkpoints are for retrieval, not grading. Attempting an answer before
revealing it is more useful than merely rereading the nearby paragraph.
