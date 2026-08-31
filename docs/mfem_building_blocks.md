# Coupled transient vibroacoustics with MFEM and NLopt

This is the implementation reference for translating Dilgen and Aage's
*Topology optimization of transient vibroacoustic problems for broadband
filter design using cut elements* into this repository's C++ stack.

- Primary source: C. B. Dilgen and N. Aage, 2024,
  [doi:10.1016/j.finel.2024.104123](https://doi.org/10.1016/j.finel.2024.104123).
- Local copy: [`golden-paper.pdf`](golden-paper.pdf).
- Companion derivation used by the paper for the detailed cut-element weak
  forms: C. B. Dilgen and N. Aage, 2021,
  [doi:10.1002/nme.6591](https://doi.org/10.1002/nme.6591).

References such as **(paper Eq. 12, p. 4)** point to the numbered equation and
printed page in the primary paper. Equations labelled **derived** below follow
from the paper's strong form but are not printed there verbatim. Sections
labelled **repository adaptation** describe implementation choices for MFEM,
FFTW, or NLopt and should not be mistaken for claims made by the authors.

The shortest safe route is:

1. reproduce the paper's two-dimensional plane-stress duct;
2. verify the forward and adjoint models independently;
3. connect the verified evaluator to MMA;
4. only then extend the same formulation to three dimensions.

Periodic/Bloch unit-cell analysis is a separate problem. It is not part of the
paper's duct model and must not be mixed into the first validation target.

## 1. What the paper actually solves

The method uses one fixed background mesh for both physics. A nodal level-set
field \(\phi\) identifies the phases:

$$
\phi(\mathbf{x}) > 0 \Rightarrow \Omega_s, \qquad
\phi(\mathbf{x}) = 0 \Rightarrow \Gamma_{as}, \qquad
\phi(\mathbf{x}) < 0 \Rightarrow \Omega_a.
\tag{1}
$$

This is paper Eq. 1, p. 3. The structural displacement \(\mathbf{u}\) and
acoustic pressure \(p\) are both solved over the whole computational domain
\(\Omega=\Omega_s\cup\Omega_a\). Cut quadrature resolves the physical
subdomains and the internal interface without remeshing.

Use two phase indicators in code; the paper reuses the symbol \(\alpha\) in
two different physical contexts:

$$
\alpha_s = \begin{cases}1 & \phi>0\\ \epsilon_f & \phi<0\end{cases},
\qquad
\alpha_a = \begin{cases}\epsilon_f & \phi>0\\ 1 & \phi<0\end{cases},
\qquad \epsilon_f=10^{-8}.
\tag{2}
$$

The solid properties are

$$
E_s(\mathbf{x})=\alpha_s\widetilde E_s,
\qquad
\rho_s(\mathbf{x})=\alpha_s\widetilde\rho_s,
\tag{3}
$$

from paper Eq. 6, pp. 3-4. The acoustic properties are

$$
K_a(\mathbf{x})=\frac{\widetilde K_a}{\alpha_a},
\qquad
\rho_a(\mathbf{x})=\frac{\widetilde\rho_a}{\alpha_a},
\qquad \widetilde K_a=\widetilde\rho_a c_a^2,
\tag{4}
$$

from paper Eq. 11, p. 4. Thus the fictitious region contributes only
\(\epsilon_f\) times the physical mass and stiffness for either field.

The authors explicitly warn that scaling stiffness and density by the same
contrast can introduce fictitious-domain modes (paper p. 4). Keep
\(\epsilon_f\) configurable and inspect modes and conditioning; \(10^{-8}\) is
a reproduction value, not a universal safe default.

### 1.1 Structural equation

The paper uses small-strain linear elasticity with Rayleigh damping:

$$
\rho_s\ddot{\mathbf u}
-\nabla\!\cdot\boldsymbol\sigma
+\alpha_d\rho_s\dot{\mathbf u}
-\nabla\!\cdot(\beta_d\dot{\boldsymbol\sigma})=\mathbf0
\quad\text{in }\Omega,
\tag{5}
$$

with clamping, traction-free boundaries, and pressure traction

$$
\mathbf u=\mathbf0\text{ on }\Gamma_{sd},\qquad
\boldsymbol\sigma\mathbf n_s=\mathbf0\text{ on }\Gamma_{sn},\qquad
\boldsymbol\sigma\mathbf n_s=p\mathbf n_a\text{ on }\Gamma_{as}.
\tag{6}
$$

These are paper Eqs. 2-5, p. 3. Normals point out of their respective domains,
so \(\mathbf n_s=-\mathbf n_a\) on the interface.

For the paper-faithful two-dimensional model, use plane-stress Lamé
coefficients with MFEM's `ElasticityIntegrator`:

$$
\lambda_{ps}=\frac{E\nu}{1-\nu^2},
\qquad
\mu=\frac{E}{2(1+\nu)}.
\tag{7}
$$

Equation (7) is a repository adaptation of the paper's stated plane-stress
constitutive law (paper p. 3). Do not use the plane-strain value
\(E\nu/[(1+\nu)(1-2\nu)]\) for the 2D reproduction.

### 1.2 Acoustic equation and boundaries

The pressure satisfies

$$
\frac{1}{K_a}\ddot p-\frac{1}{\rho_a}\nabla^2p=0
\quad\text{in }\Omega,
\tag{8}
$$

with

$$
\begin{aligned}
\mathbf n_a\!\cdot\nabla p &=0 &&\text{on }\Gamma_{ad},\\
\mathbf n_a\!\cdot\nabla p
&=\rho_a\frac{\partial^2(\mathbf n_s\!\cdot\mathbf u)}{\partial t^2}
&&\text{on }\Gamma_{as},\\
\mathbf n_a\!\cdot\nabla p+\frac1{c_a}\dot p
&=\frac2{c_a}\dot p_{in} &&\text{on }\Gamma_{ar}.
\end{aligned}
\tag{9}
$$

These are paper Eqs. 7-10, p. 4. The first condition is a hard wall, the
second couples normal structural acceleration into the acoustic field, and the
third is a first-order plane-wave absorbing/injection condition.

## 2. Weak form and coupled block system

This section is **derived** from paper Eqs. 2-10. It fixes the signs and block
locations needed by the implementation. Let \(\mathbf w\) and \(q\) be the
structural and acoustic test functions. Define

$$
\begin{aligned}
m_{uu}(\mathbf w,\mathbf u)
&=\int_\Omega \rho_s\,\mathbf w\!\cdot\mathbf u\,d\Omega,\\
k_{uu}(\mathbf w,\mathbf u)
&=\int_\Omega \boldsymbol\varepsilon(\mathbf w):
  \mathsf C:\boldsymbol\varepsilon(\mathbf u)\,d\Omega,\\
c_{uu}&=\alpha_d m_{uu}+\beta_d k_{uu},\\
m_{pp}(q,p)&=\int_\Omega \frac1{K_a}qp\,d\Omega,\\
k_{pp}(q,p)&=\int_\Omega \frac1{\rho_a}\nabla q\!\cdot\nabla p\,d\Omega,\\
c_{pp}(q,p)&=\int_{\Gamma_{ar}}\frac1{\rho_a c_a}qp\,d\Gamma,\\
k_{up}(\mathbf w,p)&=-\int_{\Gamma_{as}}
  (\mathbf w\!\cdot\mathbf n_a)p\,d\Gamma,\\
m_{pu}(q,\mathbf u)&=-\int_{\Gamma_{as}}
  q(\mathbf n_s\!\cdot\mathbf u)\,d\Gamma,\\
g(q,t)&=\int_{\Gamma_{ar}}\frac2{\rho_a c_a}q\dot p_{in}(t)\,d\Gamma.
\end{aligned}
\tag{10}
$$

The signs follow directly from integration by parts with outward acoustic and
solid normals. A reversed level-set normal reverses both interface signs; this
is why a normal-orientation test is mandatory.

With \(\mathbf v=[\mathbf u,\mathbf p]^T\), the semi-discrete system is

$$
\mathbf M\ddot{\mathbf v}
+\mathbf C\dot{\mathbf v}
+\mathbf K\mathbf v=\mathbf h,
\qquad
\mathbf h=\begin{bmatrix}\mathbf0\\\mathbf g\end{bmatrix},
\tag{11}
$$

which is paper Eqs. 12-13, pp. 4-5. The useful block layout is

$$
\mathbf M=
\begin{bmatrix}M_{uu}&0\\M_{pu}&M_{pp}\end{bmatrix},\qquad
\mathbf C=
\begin{bmatrix}C_{uu}&0\\0&C_{pp}\end{bmatrix},\qquad
\mathbf K=
\begin{bmatrix}K_{uu}&K_{up}\\0&K_{pp}\end{bmatrix}.
\tag{12}
$$

The interface coupling is not two generic stiffness blocks:

- pressure traction is \(K_{up}\mathbf p\);
- normal structural acceleration is \(M_{pu}\ddot{\mathbf u}\).

Consequently the effective coupled operator is generally nonsymmetric. Do not
use CG merely because the diagonal elasticity and acoustic blocks are
symmetric.

## 3. MFEM 4.9 spatial implementation

The repository pins MFEM 4.9 and already enables Algoim in
[`CMakeLists.txt`](../CMakeLists.txt). Start with the serial classes because
they expose the same finite-element concepts with less MPI bookkeeping:

```cpp
mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
    nx, ny, mfem::Element::QUADRILATERAL, true, lx, ly);

mfem::H1_FECollection q1(1, 2);
mfem::FiniteElementSpace pressure_fes(&mesh, &q1);
mfem::FiniteElementSpace displacement_fes(
    &mesh, &q1, 2, mfem::Ordering::byVDIM);
mfem::FiniteElementSpace level_set_fes(&mesh, &q1);

mfem::GridFunction phi(&level_set_fes);
mfem::GridFunctionCoefficient phi_coeff(&phi);
```

The paper uses continuous Galerkin Q4 elements and special cut/interface
quadrature (paper Section 2.3, p. 4). The code above is the direct MFEM Q1/Q4
mapping.

### 3.1 Reuse standard MFEM integrators where possible

The uncut algebra maps to:

| Term | MFEM building block |
|---|---|
| \(M_{uu}\) | `BilinearForm` + `VectorMassIntegrator` |
| \(K_{uu}\) | `BilinearForm` + `ElasticityIntegrator` |
| \(M_{pp}\) | `BilinearForm` + `MassIntegrator` |
| \(K_{pp}\) | `BilinearForm` + `DiffusionIntegrator` |
| \(C_{pp}\) on an exterior boundary | `BoundaryMassIntegrator` |
| coupled vectors/operators | `BlockVector`, `BlockMatrix`, `BlockOperator` |

In cut elements, ordinary integrators cannot select a different quadrature
rule for each element and phase. Reuse their formulas, but put them in a small
custom `BilinearFormIntegrator` or direct element loop which obtains the
element's cut rule before evaluating shapes.

MFEM already owns element transformations, basis functions, DOF ordering,
dense element algebra, sparse insertion, and block operators. Eigen is already
linked by this repository but is unnecessary for global FEM assembly. Using a
second sparse algebra stack would only add conversions and ownership bugs.

### 3.2 Cut volume and interface rules

MFEM's [Example 38](../build/deps/src/mfem-src/examples/ex38.cpp) demonstrates
`AlgoimIntegrationRules`, cut-volume rules, cut-surface rules, and the required
surface transformation weights. MFEM's contract is important:
`GetVolumeIntegrationRule` integrates the region where its level-set
coefficient is **positive**.

Therefore construct the three rules as follows:

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

mfem::AlgoimIntegrationRules solid_rules(order, phi_coeff, 1);
NegatedCoefficient minus_phi(phi_coeff); // Eval returns -phi_coeff.Eval(...)
mfem::AlgoimIntegrationRules acoustic_rules(order, minus_phi, 1);

mfem::IntegrationRule solid_ir;     // phi > 0
mfem::IntegrationRule acoustic_ir;  // -phi > 0, therefore phi < 0
mfem::IntegrationRule interface_ir; // phi == 0

solid_rules.GetVolumeIntegrationRule(T, solid_ir);
acoustic_rules.GetVolumeIntegrationRule(T, acoustic_ir);
solid_rules.GetSurfaceIntegrationRule(T, interface_ir);
```

For surface integration, also call `GetSurfaceWeights` as Example 38 does and
multiply the integration-point weights by those values. Do not assume a
cut-surface rule behaves like an ordinary element-boundary rule.

For each cut element:

1. integrate physical structural terms on `solid_ir` and multiply the same
   formulas by \(\epsilon_f\) on `acoustic_ir`;
2. integrate physical acoustic terms on `acoustic_ir` and multiply them by
   \(\epsilon_f\) on `solid_ir`;
3. integrate \(K_{up}\) and \(M_{pu}\) once on `interface_ir`;
4. obtain \(\nabla\phi\) at each interface point using
   `GridFunction::GetGradient`, then set
   \(\mathbf n_a=\nabla\phi/\|\nabla\phi\|\) and
   \(\mathbf n_s=-\mathbf n_a\), because \(\phi>0\) is solid.

Reject or diagnose interface points where \(\|\nabla\phi\|\) is too small to
define a stable normal.

The current CMake configuration has `MFEM_USE_ALGOIM=ON` and
`MFEM_USE_LAPACK=OFF`. Example 38's Algoim path is available; its
moment-fitting path is not. Do not describe moment fitting as a runtime option
until LAPACK is enabled and tested.

### 3.3 Interface assembly

The internal interface is not a mesh boundary attribute, so standard boundary
integrators cannot assemble it. One element loop is sufficient:

```cpp
for (int e = 0; e < mesh.GetNE(); ++e) {
    mfem::ElementTransformation *T = mesh.GetElementTransformation(e);
    // Build interface_ir for T. Empty rules contribute nothing.
    // Evaluate scalar pressure shapes and vector displacement shapes.
    // Compute n_a from grad(phi), then accumulate local Kup and Mpu.
    // Insert both local matrices using the two spaces' element VDofs.
}
```

Keep the two local matrices rectangular:

$$
K_{up}^{(e)}\in\mathbb R^{n_u\times n_p},\qquad
M_{pu}^{(e)}\in\mathbb R^{n_p\times n_u}.
$$

Handle signed MFEM VDof indices with MFEM's DOF transformation/insertion
utilities; manually taking absolute indices loses orientation information.

### 3.4 Essential boundaries and linear solver

Clamp structural true DOFs on \(\Gamma_{sd}\). The acoustic hard-wall
condition is natural and requires no essential elimination. Apply structural
elimination consistently to `M`, `C`, `K`, the off-diagonal blocks, and the
right-hand side before forming the effective operator.

Use true-DOF offsets:

```cpp
mfem::Array<int> offsets(3);
offsets[0] = 0;
offsets[1] = displacement_fes.GetTrueVSize();
offsets[2] = offsets[1] + pressure_fes.GetTrueVSize();
```

For the initial small 2D problem, assemble `SparseMatrix` blocks and use a
direct solver if one is enabled, otherwise `GMRESSolver` with a simple block
preconditioner. The paper used PETSc and MUMPS (paper p. 5), neither of which
is enabled by the current repository. `HypreParMatrix` and MPI are future
choices, not current building blocks.

The repository's CUDA option does not automatically accelerate Algoim rule
construction, custom cut loops, or a host-assembled sparse solve. Establish a
correct CPU baseline before considering device kernels.

## 4. Average-acceleration Newmark integration

The paper uses Newmark with

$$
\widetilde\beta=\frac14,\qquad
\widetilde\gamma=\frac12,
\tag{13}
$$

which is paper Eq. 20, p. 5. For \(\Delta t\), define

$$
\begin{aligned}
a_1&=1-\widetilde\gamma/\widetilde\beta,&
a_2&=(1-\widetilde\gamma/(2\widetilde\beta))\Delta t,&
a_3&=\widetilde\gamma/(\widetilde\beta\Delta t),\\
a_4&=1/(\widetilde\beta\Delta t),&
a_5&=1/(2\widetilde\beta)-1,&
a_6&=1/(\widetilde\beta\Delta t^2).
\end{aligned}
\tag{14}
$$

This is paper Eq. 19, p. 5. Each step solves

$$
\widehat K\mathbf v^n=\widehat h^n,
\qquad
\widehat K=K+a_6M+a_3C,
\tag{15}
$$

$$
\widehat h^n=h^n
+M(a_4\dot v^{n-1}+a_5\ddot v^{n-1}+a_6v^{n-1})
+C(-a_1\dot v^{n-1}-a_2\ddot v^{n-1}+a_3v^{n-1}),
\tag{16}
$$

from paper Eqs. 16-18, p. 5. Then recover

$$
\begin{aligned}
\dot v^n&=a_1\dot v^{n-1}+a_2\ddot v^{n-1}
          +a_3(v^n-v^{n-1}),\\
\ddot v^n&=-a_4\dot v^{n-1}-a_5\ddot v^{n-1}
           +a_6(v^n-v^{n-1}),
\end{aligned}
\tag{17}
$$

which are paper Eqs. 14-15, p. 5. With the paper's zero displacement and
velocity initial conditions, solve

$$M\ddot v^0=h^0$$

as in paper Eq. 21, p. 5.

MFEM provides `NewmarkSolver(0.25, 0.5)` and Example 23 demonstrates a
`SecondOrderTimeDependentOperator`. That is useful for an independent forward
smoke test. The optimization implementation should keep the explicit loop
above because the fully discrete adjoint needs the exact residual and stored
\((v^n,\dot v^n,\ddot v^n)\) sequence.

For adjoint assembly, use the three residuals printed in paper Eqs. 23-25,
p. 5:

$$
\begin{aligned}
r_1^n={}&(K+a_6M+a_3C)v^n-(a_6M+a_3C)v^{n-1}\\
&-(a_4M-a_1C)\dot v^{n-1}
+(a_2C-a_5M)\ddot v^{n-1}-h^n,\\
r_2^n={}&\dot v^n-a_1\dot v^{n-1}-a_2\ddot v^{n-1}
-a_3(v^n-v^{n-1}),\\
r_3^n={}&\ddot v^n+a_4\dot v^{n-1}+a_5\ddot v^{n-1}
-a_6(v^n-v^{n-1}).
\end{aligned}
\tag{18}
$$

Then \(R^n=[r_1^n,r_2^n,r_3^n]^T=A U^n+B U^{n-1}\), as in paper
Eqs. 22 and 43, pp. 5 and 8.

## 5. Broadband response with FFTW

After the forward solve, integrate the pressure at the outlet:

$$
\widehat p(t_n)=\int_{\Gamma_{out}}p(\mathbf x,t_n)\,d\Gamma,
\tag{19}
$$

which is paper Eq. 49, p. 9. The outlet is an exterior mesh boundary, so a
small boundary quadrature loop is enough; this does not need cut integration.

Apply the same Hann window to the design and empty-duct time histories, then
use FFTW:

$$
P_m=\operatorname{FFT}\{w_n\widehat p(t_n)\},
\qquad f_m=\frac{m}{N\Delta t}.
\tag{20}
$$

The DFT is paper Eq. 29, p. 6; the paper states that a “Hanning” window and
FFTW are used on the same page. Create FFTW plans once and reuse them across
design evaluations.

The paper writes transmission as the ratio to an empty duct (paper Eqs. 50-51,
p. 9) and discusses it as an amplitude. Use the real nonnegative amplitude
ratio

$$
S_m=\frac{|P_m|}{|P_{0,m}|}.
\tag{21}
$$

Compute \(P_{0,m}\) once with no structure using the identical mesh, source,
time step, duration, outlet functional, and window. Exclude bins where
\(|P_{0,m}|\) is below a documented excitation threshold; dividing by an
unexcited reference bin is not a meaningful transmission measurement.

The paper's pass- and stop-band functions are

$$
\Phi_1=\sum_{m\in\mathcal P}\frac{(S_m-a)^2}{a^2},\quad a=1,
\qquad
\Phi_2=\sum_{m\in\mathcal S}\frac{(S_m-b)^2}{b^2},
\tag{22}
$$

from paper Eqs. 52-53, p. 9. The paper studies \(b\) values rather than using
literal zero because of the inverse weight (paper Section 5.1, p. 11). Treat
\(b\) as a user setting and never allow \(b=0\).

The differentiable epigraph formulation is

$$
\min_{s,z}z
\quad\text{subject to}\quad
\Phi_1(s)-z\le0,\quad\Phi_2(s)-z\le0,\quad0\le s_i\le1,
\tag{23}
$$

from paper Eqs. 55-59, p. 9.

For reproducing the paper's numerical scale, it used \(T=0.02\) s,
\(\Delta t=2\times10^{-5}\) s, a 2 mm element edge, air with
\(c_a=343\) m/s and \(\rho_a=1.21\) kg/m³, and a structural material with
\(E=50\) MPa, \(\nu=0.4\), and \(\rho_s=1000\) kg/m³ (paper pp. 10-11).
These are validation inputs, not hard-coded application defaults.

The Rayleigh parameters are

$$
\alpha_d=2\zeta\frac{\omega_1\omega_2}{\omega_1+\omega_2},
\qquad
\beta_d=2\zeta\frac1{\omega_1+\omega_2},
\tag{24}
$$

from paper Eqs. 60-61, p. 11. Its example uses \(\zeta=0.1\),
\(\omega_1=1600(2\pi)\) rad/s, and \(\omega_2=2200(2\pi)\) rad/s on the same
page.

## 6. Fully discrete adjoint

Because the objective is defined through a discrete FFT of a discrete Newmark
history, the paper rejects a semi-discrete-in-time adjoint and uses a fully
discrete one (paper Section 3.3, p. 6).

### 6.1 Frequency-to-time derivative

First compute the complex derivative of each band function with respect to its
selected FFT bins. Then apply the adjoint of the actual FFT pipeline:

$$
\frac{\partial\Phi}{\partial U^n}
=\left(\frac{\partial U_f}{\partial U}\right)^T
  \frac{\partial\Phi}{\partial U_f},
\tag{25}
$$

which is paper Eq. 38, p. 7. Paper Eq. 39, p. 7 expresses this step as an
inverse DFT. The implementation must also differentiate the Hann window, so
the time-domain derivative is multiplied by \(w_n\).

FFTW leaves both forward and backward transforms unnormalized. Pick one
normalization convention, document it, and verify the coded adjoint with the
discrete transpose identity

$$
\langle F_w x,y\rangle=\langle x,F_w^*y\rangle.
\tag{26}
$$

Do not guess an extra factor of \(N\); make this identity and a scalar finite
difference pass for the exact window/FFT/objective code.

### 6.2 Reverse Newmark solve

The global adjoint is

$$
\left(\frac{\partial R}{\partial U}\right)^T\Lambda
=-\frac{\partial\Phi}{\partial U},
\tag{27}
$$

from paper Eq. 41, p. 7. Since each residual depends only on the current and
previous step, solve backwards:

$$
\begin{aligned}
A^T\Lambda^N&=-\Phi_U^N,\\
A^T\Lambda^n&=-\Phi_U^n-B^T\Lambda^{n+1},
\quad n=N-1,\ldots,1,\\
A_0^T\Lambda^0&=-\Phi_U^0-B^T\Lambda^1.
\end{aligned}
\tag{28}
$$

These are paper Eqs. 44-46, p. 8. The physical-design sensitivity is

$$
\frac{d\Phi}{d\bar s}
=(\Lambda^0)^T\frac{\partial A_0}{\partial\bar s}U^0
+\sum_{n=1}^{N}(\Lambda^n)^T
\left(
\frac{\partial A}{\partial\bar s}U^n
+\frac{\partial B}{\partial\bar s}U^{n-1}
\right),
\tag{29}
$$

from paper Eq. 47, p. 8. Store all forward states for the first implementation.
Checkpointing is only needed after memory measurements justify the extra
forward solves.

The paper notes that these matrix derivatives are nonzero only in cut elements
(paper p. 8). MFEM Example 38 supplies cut quadrature, not derivatives of the
quadrature and interface geometry with respect to nodal level-set variables.
Those analytic cut sensitivities are the main custom research component and
are delegated by the primary paper to the companion formulation. Until they
are implemented, finite differences are a verification tool for small meshes,
not a scalable replacement for the adjoint.

### 6.3 Design parameterization and filter

The optimizer variables obey \(0\le s_i\le1\) (paper Eq. 26, p. 5) and are
mapped to \([-h_e/2,h_e/2]\) so the interface cannot jump too far in one
update (paper Eq. 27, p. 6). The paper then applies

$$
-r^2\nabla^2\bar s_c+\bar s_c=\widetilde s_c
\tag{30}
$$

with Neumann conditions (paper Eq. 28, p. 6). It uses a finite-volume,
cell-centred filter plus node/cell interpolation.

**Repository adaptation:** the minimal MFEM equivalent is an H1 Helmholtz
filter assembled as `MassIntegrator + r*r*DiffusionIntegrator`; homogeneous
Neumann conditions are natural. This avoids a separate finite-volume stack but
is not bit-for-bit identical to the paper. Its adjoint is the transpose filter
solve. If exact reproduction requires the paper's cell-centred mapping, add
that only after the rest of the gradient passes finite differences.

Apply the chain in reverse as paper Eq. 48, p. 8:

$$
\frac{d\Phi}{ds}
=\frac{d\Phi}{d\bar s}
 \frac{\partial\bar s}{\partial\bar s_c}
 \frac{\partial\bar s_c}{\partial\widetilde s_c}
 \frac{\partial\widetilde s_c}{\partial\widetilde s}
 \frac{\partial\widetilde s}{\partial s}.
\tag{31}
$$

The paper checked every case against first-order finite differences and reports
worst-case disagreement below 0.1% (paper p. 8). Use the same target before
running topology optimization.

## 7. Minimal NLopt MMA adapter

NLopt is not currently fetched or linked by this repository. When the verified
evaluator exists, add NLopt as one dependency and use its official C++ API;
do not write an MMA implementation.

NLopt's `LD_MMA` is a globally convergent CCSA/MMA variant supporting nonlinear
inequality constraints. It is **not** the parallel MMA implementation used by
the paper. The paper's initial/decrease/increase asymptote values and penalty
parameter (paper p. 11) do not map directly to NLopt settings and must not be
advertised as reproduced.

Represent \(x=[s_0,\ldots,s_{n-1},z]\):

```cpp
nlopt::opt opt(nlopt::LD_MMA, n_design + 1);

std::vector<double> lower(n_design + 1, 0.0);
std::vector<double> upper(n_design + 1, 1.0);
upper.back() = std::numeric_limits<double>::infinity();
opt.set_lower_bounds(lower);
opt.set_upper_bounds(upper);

opt.set_min_objective(
    [](const std::vector<double> &x, std::vector<double> &grad, void *) {
        if (!grad.empty()) {
            std::fill(grad.begin(), grad.end(), 0.0);
            grad.back() = 1.0;
        }
        return x.back();
    }, nullptr);

std::vector<double> constraint_tol(2, 1e-8);
opt.add_inequality_mconstraint(band_constraints, &problem,
                               constraint_tol);
opt.set_maxeval(max_evaluations);
```

The vector callback performs one shared forward analysis and two adjoint solves:

```cpp
void band_constraints(unsigned m, double *value,
                      unsigned n, const double *x,
                      double *grad, void *data) {
    // s = x[0..n-2], z = x[n-1]
    // evaluate returns Phi1, Phi2, dPhi1/ds, dPhi2/ds
    const auto result = static_cast<Problem *>(data)->evaluate(x, n - 1);
    value[0] = result.phi1 - x[n - 1];
    value[1] = result.phi2 - x[n - 1];

    if (grad) {
        for (unsigned j = 0; j + 1 < n; ++j) {
            grad[0*n + j] = result.dphi1[j];
            grad[1*n + j] = result.dphi2[j];
        }
        grad[0*n + n - 1] = -1.0;
        grad[1*n + n - 1] = -1.0;
    }
}
```

NLopt stores vector-constraint gradients row-major as `grad[i*n + j]`.
Exceptions from the PDE solve must cross the callback boundary safely; record
the failing design and call NLopt's forced-stop mechanism rather than returning
fabricated objective values.

Official references:

- [NLopt C++ reference](https://nlopt.readthedocs.io/en/latest/NLopt_C-plus-plus_Reference/)
- [NLopt algorithms: MMA/CCSA](https://nlopt.readthedocs.io/en/stable/NLopt_Algorithms/#mma-method-of-moving-asymptotes-and-ccsa)
- [FFTW one-dimensional DFTs](https://www.fftw.org/fftw3_doc/Complex-One_002dDimensional-DFTs.html)
- [MFEM examples](https://mfem.org/examples/)
- [MFEM 4.9 `AlgoimIntegrationRules`](https://docs.mfem.org/4.9/classmfem_1_1AlgoimIntegrationRules.html)
- [MFEM 4.9 `NewmarkSolver`](https://docs.mfem.org/4.9/classmfem_1_1NewmarkSolver.html)

## 8. Verification gates and implementation order

Do not debug the optimizer and PDE at the same time. Each gate should leave one
small runnable regression check.

### Gate 1: cut integration

- Reproduce Example 38's analytic cut-volume and cut-surface integrals.
- Verify both \(\phi>0\) and \(\phi<0\) volumes and that they sum to the full
  element/domain volume.
- Check interface normals on a linear level set with a known direction.

### Gate 2: uncoupled physics

- Acoustic-only: hard-wall or manufactured wave problem with mesh/time-step
  convergence.
- Elasticity-only: clamped plane-stress patch test and structural eigenmodes.
- Confirm fictitious material reduces contributions by \(\epsilon_f\) without
  producing relevant spurious modes.

### Gate 3: coupled transient system

- Check every block dimension and the sparsity locations in Eq. (12).
- A uniform interface pressure must load the structure in the expected normal
  direction.
- A prescribed normal acceleration must create pressure with the expected sign.
- Compare the explicit Newmark loop with MFEM's `NewmarkSolver` on a fixed
  linear system.

### Gate 4: broadband response

- Run the empty duct through the identical FFT pipeline and obtain
  \(S(f)\approx1\) in excited bins.
- Confirm FFT frequencies, window, normalization, and outlet integration with
  a single known sinusoid.
- Compare a fixed design at selected frequencies with a frequency-domain solve
  or independent FEM package, as the paper does in Section 5.4.

### Gate 5: adjoint

- Verify the FFT transpose identity in Eq. (26).
- Compare each band gradient with centred finite differences over several
  random design variables.
- Require relative disagreement below 0.1%, matching the paper's reported
  check on p. 8, before enabling MMA.

### Gate 6: optimization

- Start on a coarse mesh and a few active frequency bins.
- Confirm both epigraph constraints and their gradients use the same design.
- Require a reduction in the maximum of \(\Phi_1\) and \(\Phi_2\), not merely
  a successful NLopt return code.
- Log NLopt status, evaluations, \(z\), both constraints, and gradient norms.

## 9. Three-dimensional extension

Only extend the validated 2D solver. The governing block structure, Newmark
scheme, FFT objective, and adjoint recursion remain unchanged.

Change the spatial pieces:

- use a Cartesian hexahedral `Mesh` and Q1 `H1_FECollection(1, 3)`;
- create the displacement space with `vdim=3`;
- use the three-dimensional Lamé coefficient
  \(\lambda=E\nu/[(1+\nu)(1-2\nu)]\), not the plane-stress value;
- obtain Algoim cut volumes and two-dimensional cut surfaces inside each hex;
- compute three-component normals from \(\nabla\phi\);
- interpret Eq. (19) as an outlet-area integral;
- validate on an extruded 2D case before attempting new 3D topology.

Memory becomes the first practical limit: storing three Newmark fields for all
time steps, plus one or two adjoint histories, scales with the full coupled DOF
count. Measure it before adding checkpointing. Likewise, MPI/Hypre, GPU partial
assembly, and periodic/Bloch constraints should be added only when the
validated 3D baseline demonstrates that they are necessary.

## 10. Definition of done

The implementation represented by this guide is ready for research use only
when all of the following are true:

- positive, negative, and surface cut quadrature pass analytic checks;
- plane-stress acoustic and elastic subproblems converge independently;
- interface signs and block locations match Eqs. (10)-(12);
- the empty-duct normalization gives \(S(f)\approx1\);
- the coded FFT/Newmark adjoint agrees with finite differences below 0.1%;
- NLopt reduces the worst band error on a coarse reproducible case;
- a fixed optimized geometry is checked independently in the frequency domain.

That sequence uses MFEM for the FEM machinery, Algoim for cut quadrature, FFTW
for spectra, and NLopt for MMA. The remaining custom code is limited to the
actual research contribution: cut-phase physics, interface coupling, and
their consistent shape sensitivities.
