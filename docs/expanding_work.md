# Expanding the coupled vibroacoustic work

This document is the research and implementation roadmap beyond the
two-dimensional transient CutFEM formulation summarized in
[`mfem_building_blocks.md`](mfem_building_blocks.md). It addresses four
extensions:

1. three-dimensional coupled vibroacoustics;
2. periodic and Floquet--Bloch boundary conditions;
3. minimum-feature and connectivity constraints for manufacturable designs;
4. testing whether a two-dimensional optimized channel remains effective when
   extruded into a three-dimensional wall.

These extensions are proposals, not results established by Dilgen and Aage.
The document marks exact mathematical extensions, repository implementation
choices, and hypotheses that require experiments separately.

## 1. Sources and nearby work

- Baseline: Dilgen and Aage,
  [Topology optimization of transient vibroacoustic problems for broadband
  filter design using cut elements](https://doi.org/10.1016/j.finel.2024.104123).
- Detailed baseline weak form: Dilgen and Aage,
  [Generalized shape optimization of transient vibroacoustic problems using
  cut elements](https://doi.org/10.1002/nme.6591).
- Local periodic-cell reference:
  [`Contribution_706_proceeding_3.pdf`](Contribution_706_proceeding_3.pdf),
  especially its Bloch transformation and reduced eigenproblem on pp. 3-4.
- Local evidence that the cut-element machinery itself extends to 3D solid
  mechanics: [`NSCM_32_naage_CutFEM.pdf`](NSCM_32_naage_CutFEM.pdf).
- Time-domain Bloch precedent: Mazzotti et al.,
  [The finite-element time-domain method for elastic band-structure
  calculations](https://doi.org/10.1016/j.cpc.2018.12.016).
- Connectivity comparison for periodic vibroacoustic structures: Cool, Aage,
  and Sigmund,
  [A practical review on promoting connectivity in topology
  optimization](https://doi.org/10.1007/s00158-025-04004-z).
- Minimum-feature/robust topology reference: Wang, Lazarov, and Sigmund,
  [On projection methods, convergence and robust formulations in topology
  optimization](https://doi.org/10.1007/s00158-010-0602-y).
- Periodic manufacturability reference: Swartz et al.,
  [Manufacturing and stiffness constraints for topology optimized periodic
  structures](https://doi.org/10.1007/s00158-022-03222-z).
- Original virtual-temperature connectivity formulation: Li et al.,
  [Structural topology optimization considering connectivity
  constraint](https://doi.org/10.1007/s00158-016-1459-5).

## 2. Keep three periodic problems separate

"Periodic unit cell" can mean three different physical problems. They use
related algebra but do not have the same boundary conditions or outputs.

| Model | Periodic directions | Propagation boundary | Primary output |
|---|---|---|---|
| Repeating wall tile | transverse $y,z$ | inlet/outlet in $x$ | transmission $S(f)$ |
| Fixed $\mathbf{k}$ Bloch cell | selected lattice directions | none in those directions | modes present for that $\mathbf{k}$ |
| Infinite bulk cube | $x,y,z$ | no inlet/outlet | dispersion and complete band gaps |

The first model is the direct extension of the broadband duct solver. The
third is a different optimization problem: once the propagation direction is
made periodic, there is no empty-duct inlet/outlet transmission experiment.

Recommended order:

1. repeating wall tile with zero transverse phase;
2. repeating wall tile at selected fixed Bloch vectors;
3. fully periodic bulk-cell dispersion only if the application requires a
   bulk metamaterial rather than a wall filter.

## 3. Three-dimensional coupled mathematics

Let $\Omega\subset\mathbb{R}^3$, displacement
$\mathbf{u}:\Omega\times[0,T]\to\mathbb{R}^3$, and pressure
$p:\Omega\times[0,T]\to\mathbb{R}$. The level-set rule remains

$$
\phi>0\text{ in }\Omega_s,\qquad
\phi<0\text{ in }\Omega_a,\qquad
\phi=0\text{ on }\Gamma_{as}.
\tag{1}
$$

The strong structural and acoustic equations do not change form:

$$
\rho_s\ddot{\mathbf u}-\nabla\cdot\boldsymbol\sigma
+\alpha_d\rho_s\dot{\mathbf u}
-\nabla\cdot(\beta_d\dot{\boldsymbol\sigma})=\mathbf0,
\tag{2}
$$

$$
\frac1{K_a}\ddot p-\frac1{\rho_a}\nabla^2p=0.
\tag{3}
$$

What changes is the constitutive law and geometry:

$$
\boldsymbol\varepsilon(\mathbf u)
=\frac12(\nabla\mathbf u+\nabla\mathbf u^T),\qquad
\boldsymbol\sigma
=\lambda\operatorname{tr}(\boldsymbol\varepsilon)\mathbf I
+2\mu\boldsymbol\varepsilon,
\tag{4}
$$

$$
\lambda=\frac{E\nu}{(1+\nu)(1-2\nu)},\qquad
\mu=\frac{E}{2(1+\nu)}.
\tag{5}
$$

The interface is now a cut surface rather than a cut line. With outward
normals $\mathbf n_s=-\mathbf n_a$, the coupling remains

$$
\boldsymbol\sigma\mathbf n_s=p\mathbf n_a,
\qquad
\mathbf n_a\cdot\nabla p
=\rho_a\frac{\partial^2(\mathbf n_s\cdot\mathbf u)}{\partial t^2}.
\tag{6}
$$

The weak forms and block locations remain exactly the same:

$$
M=
\begin{bmatrix}M_{uu}&0\\M_{pu}&M_{pp}\end{bmatrix},\quad
C=
\begin{bmatrix}C_{uu}&0\\0&C_{pp}\end{bmatrix},\quad
K=
\begin{bmatrix}K_{uu}&K_{up}\\0&K_{pp}\end{bmatrix}.
\tag{7}
$$

Only the integration measures and vector dimensions change:

$$
K_{up}(\mathbf w,p)
=-\int_{\Gamma_{as}}(\mathbf w\cdot\mathbf n_a)p\,dA,
\qquad
M_{pu}(q,\mathbf u)
=-\int_{\Gamma_{as}}q(\mathbf n_s\cdot\mathbf u)\,dA.
\tag{8}
$$

The Newmark scheme, FFT objective, and reverse-time adjoint recursion from the
baseline guide are dimension-independent. Analytic derivatives of moving 3D
cut surfaces with respect to nodal level-set values remain custom research
code; MFEM/Algoim supplies quadrature, not this design derivative.

### 3.1 MFEM mapping

Use Q1 hexahedra first:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian3D(
    nx, ny, nz, mfem::Element::HEXAHEDRON, lx, ly, lz);
mfem::H1_FECollection q1(1, 3);
mfem::FiniteElementSpace pressure_fes(&mesh, &q1);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &q1, 3, mfem::Ordering::byVDIM);
mfem::FiniteElementSpace level_set_fes(&mesh, &q1);
```

Reuse the baseline's positive and negated level-set
`AlgoimIntegrationRules`. Example 38 already supports three-dimensional cut
volumes and cut surfaces. Validate that path on a plane cutting a hex before
using an optimized surface.

## 4. Periodic and Floquet--Bloch conditions

### 4.1 Zero-phase periodic wall tile

For a wall normal to $x$, retain the absorbing/injection boundaries at the
left and right and identify the transverse faces:

$$
\begin{aligned}
p(x,y+L_y,z,t)&=p(x,y,z,t),\\
p(x,y,z+L_z,t)&=p(x,y,z,t),\\
\mathbf u(x,y+L_y,z,t)&=\mathbf u(x,y,z,t),\\
\mathbf u(x,y,z+L_z,t)&=\mathbf u(x,y,z,t).
\end{aligned}
\tag{9}
$$

This is straightforward in a real-valued broadband simulation. MFEM natively
identifies matching vertices and therefore matching H1 DOFs:

```cpp
std::vector<mfem::Vector> translations = {
    mfem::Vector({0.0, Ly, 0.0}),
    mfem::Vector({0.0, 0.0, Lz})
};
mfem::Mesh periodic_mesh = mfem::Mesh::MakePeriodic(
    mesh, mesh.CreatePeriodicVertexMapping(translations));
```

MFEM requires at least two interior vertices between identified periodic
faces. Periodic meshes also store geometry through a discontinuous nodal grid
function; after `MakePeriodic`, use `GetNodes`/`GetNode` rather than assuming
`GetVertex` is a unique physical coordinate. See the official
[MFEM periodic-boundary guide](https://mfem.org/howto/periodic-boundaries/).

Apply the same topology to displacement, pressure, level set, design filter,
and connectivity auxiliary fields. Periodic state fields with a nonperiodic
design field would optimize a seam.

### 4.2 Fixed $\mathbf{k}$ Floquet--Bloch cell

For lattice vector $\mathbf L_j$ and prescribed Bloch vector $\mathbf k$,

$$
v(\mathbf x+\mathbf L_j,t)
=e^{i\mathbf k\cdot\mathbf L_j}v(\mathbf x,t),
\qquad v\in\{p,u_x,u_y,u_z\}.
\tag{10}
$$

Partition the full DOFs and construct the complex constraint map

$$
v=\Lambda(\mathbf k)\widetilde v.
\tag{11}
$$

The local periodic-cell paper in this repository gives the corresponding
edge/corner construction. Reduce all coupled matrices consistently:

$$
M_{\mathbf k}=\Lambda^H M\Lambda,\qquad
C_{\mathbf k}=\Lambda^H C\Lambda,\qquad
K_{\mathbf k}=\Lambda^H K\Lambda,\qquad
h_{\mathbf k}=\Lambda^Hh.
\tag{12}
$$

The Newmark equations then run unchanged with complex matrices and vectors.
If a complex sparse path is inconvenient, split every operator
$A=A_R+iA_I$ into a real doubled system:

$$
\begin{bmatrix}A_R&-A_I\\A_I&A_R\end{bmatrix}
\begin{bmatrix}v_R\\v_I\end{bmatrix}
=\begin{bmatrix}h_R\\h_I\end{bmatrix}.
\tag{13}
$$

MFEM's `MakePeriodic` only implements phase one. Nonzero phase therefore needs
the explicit $\Lambda(\mathbf k)$ restriction/prolongation or an equivalent
paired-DOF assembler; topological identification alone is insufficient.

A broadband time-domain run is valid for one fixed $\mathbf k$. Recover a
band diagram by repeating the run for selected points along the irreducible
Brillouin contour and identifying spectral peaks, as in the FETD reference.
The design field remains real and zero-phase periodic; only physical state
fields receive Bloch phase.

### 4.3 What is not straightforward

For plane-wave incidence at fixed angle $\theta$, the transverse wavevector
depends on frequency:

$$
k_t(\omega)=\frac{\omega}{c_a}\sin\theta,
\qquad
e^{ik_t(\omega)L}.
\tag{14}
$$

One constant Bloch phase cannot represent Eq. (14) for every FFT bin in one
broadband run. In time, the phase becomes a delay relation, for example

$$
p(y+L,t)=p(y,t-\tau),\qquad
\tau=\frac{L\sin\theta}{c_a},
\tag{15}
$$

and the history term must be included consistently in the forward residual
and discrete adjoint. This is not a native `MakePeriodic` boundary and is not
the first implementation target. Use zero phase for normal incidence, or run
separate fixed $\mathbf k$ cases.

## 5. Manufacturability requirements

For the first extension, "manufacturable" means:

1. minimum solid and void feature sizes;
2. no floating solid components in the periodically repeated wall;
3. a connected load path to designated wall skins or attachment faces;
4. for an extruded design, channels remain open through the extrusion depth;
5. the exported thresholded design passes an exact component audit.

Overhang, powder removal, tool access, and molding directions are
process-specific. Add them only after choosing a manufacturing process.

### 5.1 Periodic minimum feature size

The baseline paper smooths the design but explicitly does not impose a length
scale. Extend its Helmholtz filter to the periodic mesh:

$$
-r_f^2\nabla^2\widetilde s+\widetilde s=s,
\qquad
\widetilde s(\mathbf x+\mathbf L_j)=\widetilde s(\mathbf x).
\tag{16}
$$

Apply a smooth projection at three thresholds
$\eta_d<\eta_b<\eta_e$ to form dilated, blueprint, and eroded geometries.
The acoustic objective should remain acceptable for all three, or at minimum
for the eroded and dilated realizations. This prevents a nominally acceptable
feature from disappearing under plausible manufacturing offsets.

The filter radius and projection thresholds are calibration parameters tied
to mesh size and the intended minimum tool/nozzle size. Verify the final
thresholded geometry with a physical-distance or morphological feature check;
the filter radius alone is not a certificate.

Freeze non-design skins and attachment pads before filtering. Apply periodic
filtering in transverse directions so features crossing one cell face continue
smoothly into the neighboring cell.

### 5.2 Connectivity options

No single cheap constraint means every possible notion of manufacturability.

| Method | Extra solve | What it promotes | Known gap |
|---|---:|---|---|
| Static compliance | one elasticity solve | required face-to-face load path | may leave unrelated islands |
| Self-weight compliance | one elasticity solve | support for every solid region | biases material toward support direction |
| Virtual temperature | one scalar diffusion solve | connection to chosen sink/anchor | threshold and interpolation dependent |
| Spectral connectivity | several scalar eigenpairs | global connectedness | more expensive and eigenvalue-sensitive |
| Flood fill/union-find | no PDE | exact discrete component audit | nondifferentiable |

#### Lax structural constraint

The simplest reuse of the current stack is an auxiliary solid-only elasticity
problem. Fix the attachment skin, apply a uniform load to the opposite skin,
and enforce

$$
K_s(\bar s)u_c=f_c,\qquad
J_c=f_c^Tu_c\le\mu_cJ_c^{full}.
\tag{17}
$$

Use periodic conditions transversely. Equation (17) forces a useful connection
between the loaded and fixed faces, but it does not prevent an additional
unloaded island. It is therefore a “lax” manufacturability constraint, not a
no-island guarantee.

Self-weight compliance gives each solid region a design-dependent load:

$$
K_s(\bar s)u_w=f_w(\bar s),
\qquad
J_w=f_w^Tu_w\le\mu_wJ_w^{full}.
\tag{18}
$$

Its derivative includes the load derivative:

$$
\frac{dJ_w}{d\bar s_i}
=2u_w^T\frac{\partial f_w}{\partial\bar s_i}
-u_w^T\frac{\partial K_s}{\partial\bar s_i}u_w.
\tag{19}
$$

This is more responsive to isolated material but introduces a preferred
support direction.

#### Scalar virtual-temperature constraint

For a less directionally biased constraint, make the solid phase conductive
and heat-generating, give the void a small conductivity, impose $T=0$ on
the allowed attachment region, and solve

$$
-\nabla\cdot(k(\bar s)\nabla T)=Q(\bar s),
\tag{20}
$$

with transverse periodicity. A disconnected solid component can release heat
only through the low-conductivity void and develops a high virtual
temperature. Constrain a smooth maximum such as

$$
\Theta_T=
\left(\frac1{N_T}\sum_iT_i^p\right)^{1/p}
\le\mu_T\Theta_T^{full}.
\tag{21}
$$

This reuses MFEM `DiffusionIntegrator`, `MassIntegrator`, a scalar linear
solve, and one scalar adjoint. Normalize against a full-solid reference and
calibrate $p,\mu_T$, and the conductivity contrast on known connected and
disconnected test geometries.

#### Exact acceptance audit

Threshold the final level set into solid/void cells and run union-find with
wrapped neighbors across periodic faces. Mark components touching an allowed
attachment/skin component; any other solid component is a floating island.

Audit either a wrapped graph directly or a $3\times3$ transverse tiling.
Checking only one unwrapped unit cell is wrong: a component split across
opposite cell faces may be connected in the infinite periodic structure.

The audit is nondifferentiable, so use it as a regression and export gate. If
the lax compliance constraint repeatedly fails it, promote the virtual-
temperature constraint into the optimization rather than repairing geometry
silently after optimization.

### 5.3 Recommended first constraint stack

1. periodic Helmholtz filter and robust eroded/blueprint/dilated projections;
2. fixed wall skins/attachment regions;
3. self-weight compliance as the first inexpensive optimizer constraint;
4. wrapped union-find after every saved design and as a hard export gate;
5. virtual temperature if self-weight still admits islands or biases the
   acoustic layout too strongly.

This is deliberately smaller than adding compliance, temperature, graph
eigenvalues, and morphology constraints at once.

## 6. Does a 2D filter survive extrusion?

### 6.1 Exact acoustic reduction

Suppose the 3D geometry, material coefficients, source, initial conditions,
and boundary conditions are invariant in $z$, and only the zero transverse
mode is excited. Then $\partial p/\partial z=0$, and

$$
\nabla_{3D}^2p
=\frac{\partial^2p}{\partial x^2}
+\frac{\partial^2p}{\partial y^2}
=\nabla_{2D}^2p.
\tag{22}
$$

Under those assumptions, the 2D acoustic pressure is an exact slice of the 3D
solution. This provides a testable hypothesis, not a guarantee for a finite
wall in a room.

Higher depth modes break the reduction. For rigid faces separated by depth
$D$, the transverse wavenumbers and first cut-on frequency are

$$
k_{z,n}=\frac{n\pi}{D},
\qquad f_{z,1}=\frac{c_a}{2D}.
\tag{23}
$$

For a periodically repeated depth $D$ cell,

$$
k_{z,n}=\frac{2n\pi}{D},
\qquad f_{z,1}=\frac{c_a}{D}.
\tag{24}
$$

Below the relevant cut-on frequency, normal uniform incidence strongly favors
the invariant mode. Oblique incidence, nonuniform sources, finite edges,
leakage around the wall, and asymmetric solid modes can excite 3D behavior
even below a simple empty-duct cutoff estimate.

### 6.2 Structural mismatch: plane stress versus extrusion

The paper's 2D solid is plane stress. An infinitely long, invariant extrusion
with suppressed out-of-plane strain is plane strain. A thin body with free
front/back faces may approach plane stress, while a thick extrusion develops
3D modes.

Therefore compare both 2D constitutive assumptions:

$$
\lambda_{plane\ stress}=\frac{E\nu}{1-\nu^2},
\qquad
\lambda_{plane\ strain}=\frac{E\nu}{(1+\nu)(1-2\nu)}.
\tag{25}
$$

If the plane-stress and plane-strain transmission curves already differ
substantially, a 2D paper design should not be presumed to survive extrusion.

### 6.3 Extruded wall versus free 3D design

| Question | Extruded 2D cross-section | Direct 3D wall tile |
|---|---|---|
| Design variables | $s(x,y)$ | $s(x,y,z)$ |
| Geometry | constant through depth | free 3D cut surface |
| Manufacturing | extrusion, lamination, stacked cutting | additive/casting/CNC dependent |
| Compute and adjoint memory | close to 2D | grows with depth layers and 3 displacement DOFs |
| Acoustic modes | invariant mode by construction | transverse scattering and mode conversion |
| Structural modes | limited 2D kinematics | bending, torsion, and depth-local resonances |
| Optimization risk | misses beneficial 3D effects | larger nonconvex design space and more islands |
| Validation value | controlled bridge from the paper | final physical target |

The extruded design is preferable if it meets the required response because it
is cheaper to optimize, easier to manufacture, and easier to validate. A free
3D design is justified only by a measured performance gain or a requirement
that the extrusion cannot meet.

### 6.4 Experiment matrix

Use identical materials, incident signal, FFT window, frequency bins, and
normalization in every case:

| ID | Model | Purpose |
|---|---|---|
| E0 | paper 2D plane stress | published baseline |
| E1 | same geometry, 2D plane strain | isolate structural constitutive effect |
| E2 | 3D extrusion with zero-phase depth periodicity | test invariant 3D equivalence |
| E3 | finite-depth 3D extrusion with physical front/back conditions | expose depth modes and edge effects |
| E4 | freely optimized 3D periodic wall tile initialized from E2 | measure value of 3D design freedom |
| E5 | replicated multi-cell wall | check finite-array and seam effects |

Sweep depth, frequency band, and at least normal plus selected oblique
incidence cases. Compare:

- complex outlet pressure and transmission $S(f)$;
- pass/stop objectives and insertion loss in dB;
- energy in nonzero transverse acoustic modes;
- structural displacement energy by component;
- connected-component count and minimum solid/void feature size;
- forward, adjoint, and optimization wall time and memory.

Recommended validation thresholds, to be calibrated after mesh/time-step
convergence, are:

- E2 versus E1: within 1 dB insertion loss throughout the target band;
- adjoint gradients: below 0.1% relative error on sampled design variables;
- no unattached solid components in a wrapped periodic audit;
- every measured feature at or above the declared manufacturing minimum.

The 1 dB value is an engineering acceptance target for this project, not a
result reported by the cited papers.

## 7. Direct 3D cube design

Before optimizing a “cube,” choose its physical interpretation.

### 7.1 Cube-shaped wall tile

If $x$ is wall thickness, keep inlet/outlet faces in $x$ and periodize only
$y,z$. Optimize broadband transmission exactly as in the baseline. The cube
shape of the design box does not make the physics bulk-periodic.

### 7.2 Fully periodic bulk cube

If all three axes repeat, apply Eq. (10) on all face pairs and optimize a band-
structure metric. For the undamped free problem,

$$
\Lambda^H(\mathbf k)
\left(K-\omega^2M\right)
\Lambda(\mathbf k)\widetilde v=0.
\tag{26}
$$

This is the same reduced eigenproblem used by the local periodic-cell paper,
now with the coupled vibroacoustic block matrices. With damping, the operator
also contains $i\omega C$, producing a complex quadratic eigenproblem, or a
linear generalized eigenproblem after state-space linearization.

A broadband FETD alternative runs one fixed $\mathbf k$ transient per
Brillouin-zone sample and identifies spectral peaks. Validate it against the
direct eigenproblem on a coarse undamped cell before using it in optimization.

For either cube interpretation, impose minimum features and connectivity on
the periodically repeated topology, not only within the displayed reference
cube.

## 8. Implementation sequence and gates

### Phase A: invariant 3D forward model

1. Extrude a known 2D level set without adding 3D design variables.
2. Validate 3D cut volume, interface area, and normals analytically.
3. Match E2 against plane-strain E1 under zero-phase depth periodicity.
4. Repeat the empty-duct check $S(f)\approx1$.

### Phase B: periodic wall tile

1. Create a zero-phase $y,z$-periodic MFEM mesh.
2. Verify paired DOF counts and continuity for pressure, displacement, level
   set, filter, and auxiliary manufacturability fields.
3. Compare one cell against an explicitly tiled $3\times3$ model at normal
   incidence.
4. Extend the discrete adjoint and repeat finite-difference checks.

### Phase C: manufacturing constraints

1. Add periodic robust filtering and fixed skins.
2. Implement self-weight compliance and its design-dependent sensitivity.
3. Add the wrapped union-find export gate.
4. Introduce virtual temperature only if the lax constraint fails controlled
   island tests.

### Phase D: free 3D optimization

1. Initialize the 3D field from the validated extrusion.
2. Release variation through depth gradually rather than from random 3D noise.
3. Compare E4 against E2 at equal feature size, material budget, and optimizer
   evaluation budget.
4. Keep the 3D result only if its validated benefit justifies the additional
   manufacturing and computational cost.

### Phase E: Floquet--Bloch research branch

1. Implement and unit-test paired-face $\Lambda(\mathbf k)$ on a scalar wave.
2. Validate real/imaginary splitting against a direct complex solve.
3. Reproduce a simple dispersion curve with direct eigenanalysis.
4. Reproduce it with fixed $\mathbf k$ broadband FETD.
5. Only then connect Bloch outputs and sensitivities to topology optimization.

## 9. Definition of done

The expansion is supported only when:

- the invariant 3D extrusion converges to the corresponding 2D plane-strain
  result under invariant loading and boundaries;
- zero-phase periodic cells match an explicit multi-cell model;
- fixed $\mathbf k$ Bloch results match a direct eigenproblem on a reference
  cell;
- the periodic filter has no seam and both solid/void minimum features pass a
  physical-distance audit;
- every exported solid component connects to an allowed skin in the wrapped
  periodic graph;
- all new objective and constraint sensitivities pass finite differences;
- the freely designed 3D tile beats the extruded design under the same
  material, feature-size, and evaluation budgets.

Until the last comparison passes, the extruded wall is the preferred design:
it is the smaller model and the simpler manufacturing path that can still
retain the paper's broadband mechanism.
