Yes. What you want is basically a **one-page implementation map**:

1. what each block matrix means,
2. what integral creates it,
3. what MFEM construct corresponds to it,
4. how those blocks combine into $$M,C,K$$,
5. how Newmark operates on them,
6. how the residuals are evaluated.

The paper’s starting point is

$$
\boxed{
M\ddot v^n+C\dot v^n+Kv^n=h^n
}
$$

with

$$
\boxed{
v=
\begin{bmatrix}
u \\ p
\end{bmatrix}.
}
$$

Here $$u$$ is structural displacement and $$p$$ is acoustic pressure. 

---

# 1. Master lookup table for $$M,C,K$$

For the coupled system, keep this block notation:

$$
M=
\begin{bmatrix}
M_{uu} & M_{up} \\
M_{pu} & M_{pp}
\end{bmatrix},
\qquad
C=
\begin{bmatrix}
C_{uu} & C_{up} \\
C_{pu} & C_{pp}
\end{bmatrix},
$$

$$
K=
\begin{bmatrix}
K_{uu} & K_{up} \\
K_{pu} & K_{pp}
\end{bmatrix}.
$$

For this paper's formulation, the useful structure is

$$
\boxed{
M=
\begin{bmatrix}
M_{uu}&0 \\
M_{pu}&M_{pp}
\end{bmatrix}
}
$$

$$
\boxed{
C=
\begin{bmatrix}
C_{uu}&0 \\
0&C_{pp}
\end{bmatrix}
}
$$

$$
\boxed{
K=
\begin{bmatrix}
K_{uu}&K_{up} \\
0&K_{pp}
\end{bmatrix}.
}
$$

The paper itself compresses all these blocks into the global $$M,C,K$$, but those matrices come from the weak forms of the solid, acoustic, and interface equations. 

| Block    | Mathematical entry                                                               | Meaning                            | Typical MFEM construction         |
| -------- | -------------------------------------------------------------------------------- | ---------------------------------- | --------------------------------- |
| $$M_{uu}$$ | $$\displaystyle\int_\Omega \rho_s,w_i\cdot N_j^u,d\Omega$$                         | solid inertia                      | `VectorMassIntegrator`            |
| $$M_{up}$$ | $$0$$                                                                            | no $$\ddot p\to u$$ term             | none                              |
| $$M_{pu}$$ | $$\displaystyle-\int_{\Gamma_{as}}q_i(n_s\cdot N_j^u),d\Gamma$$                    | solid acceleration drives pressure | mixed/custom interface integrator |
| $$M_{pp}$$ | $$\displaystyle\int_\Omega \frac1{K_a}N_i^pN_j^p,d\Omega$$                         | acoustic inertia/compressibility   | `MassIntegrator`                  |
| $$C_{uu}$$ | $$\alpha_dM_{uu}+\beta_dK_{uu}$$                                                   | Rayleigh damping                   | vector mass + elasticity          |
| $$C_{up}$$ | $$0$$                                                                            | none                               | none                              |
| $$C_{pu}$$ | $$0$$                                                                            | none                               | none                              |
| $$C_{pp}$$ | $$\displaystyle\int_{\Gamma_{ar}}\frac1{\rho_ac_a}N_i^pN_j^p,d\Gamma$$             | absorbing boundary                 | boundary mass-type integrator     |
| $$K_{uu}$$ | $$\displaystyle\int_\Omega \varepsilon(w_i):\mathbb C:\varepsilon(N_j^u),d\Omega$$ | elastic stiffness                  | `ElasticityIntegrator`            |
| $$K_{up}$$ | $$\displaystyle-\int_{\Gamma_{as}}(w_i\cdot n_a)N_j^p,d\Gamma$$                    | pressure loads solid               | mixed/custom interface integrator |
| $$K_{pu}$$ | $$0$$                                                                            | no displacement-level term         | none                              |
| $$K_{pp}$$ | $$\displaystyle\int_\Omega\frac1{\rho_a}\nabla N_i^p\cdot\nabla N_j^p,d\Omega$$    | acoustic propagation               | `DiffusionIntegrator`             |

The solid/interface and acoustic/interface conditions generating the two off-diagonal blocks are explicitly given by the paper as

$$
\sigma n_s=pn_a
$$

and

$$
n_a\cdot\nabla p
=

\rho_a
\frac{\partial^2(n_s\cdot u)}{\partial t^2}.
$$

 

---

# 2. The load vector

The global load is

$$
h=
\begin{bmatrix}
h_u \\ h_p
\end{bmatrix}.
$$

For this paper,

$$
\boxed{
h=
\begin{bmatrix}
0 \\ g
\end{bmatrix}
}
$$

because the explicit input is an acoustic incoming wave rather than a separately prescribed structural force. 

So in code you can keep the same mental distinction:

```cpp
h_u   // structural RHS
h_p   // acoustic RHS

// Conceptually: h = [h_u; h_p]
```

---

# 3. Once you have the matrices, forget FEM for a moment

This is an important conceptual split.

Before assembly:

$$
\text{PDE}
\rightarrow
\text{weak form}
\rightarrow
M,C,K.
$$

After assembly:

$$
\boxed{
M,C,K
}
$$

are just linear algebra objects.

Newmark does not care whether $$M$$ came from elasticity, acoustics, or magic.

It only sees

$$
M\ddot v+C\dot v+Kv=h.
$$

From this point onward, you are mostly doing:

* matrix-vector multiplication,
* matrix addition,
* scalar-matrix multiplication,
* linear solves,
* vector addition/subtraction.

---

# 4. Initial state

The paper assumes

$$
v^0=0,
\qquad
\dot v^0=0.
$$

Then it obtains the initial acceleration by solving

$$
\boxed{
M\ddot v^0=h^0.
}
$$



In code conceptually:

```cpp
v     = 0;
v_dot = 0;

Solve(M, h0, v_ddot);
```

Important:

```cpp
v_ddot = inverse(M) * h0;
```

is mathematically fine, but **do not explicitly compute $$M^{-1}$$**.

You solve

$$
Mx=h.
$$

So think:

```cpp
solver.SetOperator(M);
solver.Mult(h0, v_ddot);
```

---

# 5. Newmark constants

The paper uses

$$
\tilde\beta=\frac14,
\qquad
\tilde\gamma=\frac12.
$$



Then calculates

$$
a_1,\ldots,a_6.
$$

From the paper:

$$
a_1=1-\frac{\tilde\gamma}{\tilde\beta},
$$

$$
a_2=
\left(
1-\frac{\tilde\gamma}{2\tilde\beta}
\right)\Delta t,
$$

$$
a_3=
\frac{\tilde\gamma}
{\tilde\beta\Delta t},
$$

$$
a_4=
\frac1{\tilde\beta\Delta t},
$$

$$
a_5=
\frac1{2\tilde\beta}-1,
$$

$$
a_6=
\frac1{\tilde\beta\Delta t^2}.
$$

These are just scalar doubles in your implementation.

---

# 6. Build the Newmark effective matrix

This is probably the most important operation after assembling $$M,C,K$$.

The paper defines

$$
\boxed{
\hat K
=

K+a_6M+a_3C.
}
$$



So mathematically:

```text
Khat = K
     + a6*M
     + a3*C
```

You do this only when those matrices/time-step parameters change.

If

$$
M,C,K,\Delta t
$$

are constant during one forward transient solve, then

$$
\hat K
$$

is constant over all time steps.

That is very useful because you can potentially factor/precondition it once and reuse it.

---

# 7. Construct the effective RHS

At each new time step (n), the paper calculates

$$
\boxed{
\hat h^n
=

h^n
+
M
\left(
a_4\dot v^{n-1}
+a_5\ddot v^{n-1}
+a_6v^{n-1}
\right)
}
$$

$$
\boxed{
\qquad
+
C
\left(
-a_1\dot v^{n-1}
-a_2\ddot v^{n-1}
+a_3v^{n-1}
\right).
}
$$



Do it in pieces.

First vector:

$$
x_M=
a_4\dot v^{n-1}
+a_5\ddot v^{n-1}
+a_6v^{n-1}.
$$

Second vector:

$$
x_C=
-a_1\dot v^{n-1}
-a_2\ddot v^{n-1}
+a_3v^{n-1}.
$$

Then

$$
y_M=Mx_M,
$$

$$
y_C=Cx_C.
$$

Finally

$$
\boxed{
\hat h^n=h^n+y_M+y_C.
}
$$

Conceptually:

```cpp
xM = a4 * v_dot_old
   + a5 * v_ddot_old
   + a6 * v_old;

M.Mult(xM, yM);

xC = -a1 * v_dot_old
   - a2 * v_ddot_old
   + a3 * v_old;

C.Mult(xC, yC);

hhat = h_n;
hhat += yM;
hhat += yC;
```

This is all just vector arithmetic + matrix-vector products.

---

# 8. Solve for the new state $$v^n$$

Now solve

$$
\boxed{
\hat K v^n=\hat h^n.
}
$$

That is Eq. (16) in the paper. 

Conceptually:

```cpp
solver.SetOperator(Khat);
solver.Mult(hhat, v_new);
```

Now remember what $$v_n$$ contains:

$$
v^n=
\begin{bmatrix}
u^n \\
p^n
\end{bmatrix}.
$$

So one solve gives both

$$
u^n
$$

and

$$
p^n.
$$

---

# 9. Recover $$\dot v^n$$

The paper gives

$$
\boxed{
\dot v^n
=

a_1\dot v^{n-1}
+
a_2\ddot v^{n-1}
+
a_3(v^n-v^{n-1}).
}
$$



Code-level thought:

```cpp
delta_v = v_new;
delta_v -= v_old;

v_dot_new = a1 * v_dot_old
          + a2 * v_ddot_old
          + a3 * delta_v;
```

---

# 10. Recover $$\ddot v^n$$

Similarly,

$$
\boxed{
\ddot v^n
=

-a_4\dot v^{n-1}
-a_5\ddot v^{n-1}
+a_6(v^n-v^{n-1}).
}
$$



So:

```cpp
v_ddot_new =
    -a4 * v_dot_old
    -a5 * v_ddot_old
    +a6 * delta_v;
```

At that point you've completed one Newmark step.

---

# 11. Store the complete Newmark state

The paper then defines

$$
\boxed{
U^n=
\begin{bmatrix}
v^n \\
\dot v^n \\
\ddot v^n
\end{bmatrix}.
}
$$



Be careful with terminology:

$$
v^n=
\begin{bmatrix}
u^n \\
p^n
\end{bmatrix}
$$

whereas

$$
U^n=
\begin{bmatrix}
v^n \\
\dot v^n \\
\ddot v^n
\end{bmatrix}.
$$

So if

$$
v\in\mathbb R^{N},
$$

then

$$
U\in\mathbb R^{3N}.
$$

In code you don't necessarily need to physically concatenate these unless the adjoint/FFT code benefits from it.

You could simply store:

```cpp
struct State
{
    Vector v;
    Vector v_dot;
    Vector v_ddot;
};
```

That's mathematically equivalent.

---

# 12. Residual $$r_1^n$$

This checks whether the effective dynamic equation is satisfied.

The paper defines

$$
\boxed{
r_1^n
=

\left(K+a_6M+a_3C\right)v^n
-
\left(a_6M+a_3C\right)v^{n-1}
}
$$

$$
\boxed{
\quad
-

\left(a_4M-a_1C\right)\dot v^{n-1}
+
\left(a_2C-a_5M\right)\ddot v^{n-1}
-h^n.
}
$$



But because you've already defined

$$
\hat K=K+a_6M+a_3C
$$

and

$$
\hat h^n
$$

you can think much more simply:

$$
\boxed{
r_1^n
=

\hat K v^n-\hat h^n.
}
$$

That's much easier to debug.

Code:

```cpp
Khat.Mult(v_new, r1);
r1 -= hhat;
```

Then check

```cpp
r1.Norml2()
```

or relative form

$$
\frac{|r_1|_2}{|\hat h|_2}.
$$

After a good linear solve, this should be small.

---

# 13. Residual $$r_2^n$$

Paper:

$$
\boxed{
r_2^n
=

\dot v^n
-a_1\dot v^{n-1}
-a_2\ddot v^{n-1}
-a_3(v^n-v^{n-1}).
}
$$



But notice something:

this is literally

$$
\boxed{
\text{computed }\dot v^n
-

\text{Newmark-predicted }\dot v^n.
}
$$

So:

```cpp
r2 = v_dot_new;

r2.Add(-a1, v_dot_old);
r2.Add(-a2, v_ddot_old);

delta_v = v_new;
delta_v -= v_old;

r2.Add(-a3, delta_v);
```

If your update code is correct,

$$
r_2\approx0.
$$

In fact, if you calculate `v_dot_new` using exactly this formula, then $$r_2$$ should be near machine precision except for floating-point arithmetic.

---

# 14. Residual $$r_3^n$$

Paper:

$$
\boxed{
r_3^n
=

\ddot v^n
+a_4\dot v^{n-1}
+a_5\ddot v^{n-1}
-a_6(v^n-v^{n-1}).
}
$$



Code:

```cpp
r3 = v_ddot_new;

r3.Add(a4, v_dot_old);
r3.Add(a5, v_ddot_old);

r3.Add(-a6, delta_v);
```

Again,

$$
r_3\approx0.
$$

---

# 15. Complete residual vector

The paper collects them as

$$
\boxed{
R^n=
\begin{bmatrix}
r_1^n \\
r_2^n \\
r_3^n
\end{bmatrix}.
}
$$



You can physically make a big block vector:

```text
R =
[
    r1
    r2
    r3
]
```

or just keep them separate.

For debugging I'd actually keep:

```cpp
double norm_r1;
double norm_r2;
double norm_r3;
```

and print:

```text
time step 427
||r1|| = 3.2e-10
||r2|| = 1.8e-14
||r3|| = 7.4e-13
```

That immediately tells you which piece is broken.

---

# 16. The full forward loop

This is the whole transient solver conceptually:

```cpp
// ------------------------------------
// Initial state
// ------------------------------------

v      = 0.0;
v_dot  = 0.0;

Solve(M, h0, v_ddot);

// ------------------------------------
// Newmark effective matrix
// ------------------------------------

Khat = K + a6*M + a3*C;

solver.SetOperator(Khat);

// ------------------------------------
// Time loop
// ------------------------------------

for (int n = 1; n <= N; ++n)
{
    // current external load
    ComputeLoad(t[n], h);

    // effective RHS
    xM = a4*v_dot
       + a5*v_ddot
       + a6*v;

    xC = -a1*v_dot
       - a2*v_ddot
       + a3*v;

    M.Mult(xM, yM);
    C.Mult(xC, yC);

    hhat = h;
    hhat += yM;
    hhat += yC;

    // solve for v^n
    solver.Mult(hhat, v_new);

    // displacement/state increment
    delta_v = v_new;
    delta_v -= v;

    // Newmark derivatives
    v_dot_new =
          a1*v_dot
        + a2*v_ddot
        + a3*delta_v;

    v_ddot_new =
         -a4*v_dot
         -a5*v_ddot
         +a6*delta_v;

    // residuals
    r1 = Khat*v_new - hhat;

    r2 = v_dot_new
       - a1*v_dot
       - a2*v_ddot
       - a3*delta_v;

    r3 = v_ddot_new
       + a4*v_dot
       + a5*v_ddot
       - a6*delta_v;

    // save state if needed
    Save(v_new, v_dot_new, v_ddot_new);

    // advance
    v      = v_new;
    v_dot  = v_dot_new;
    v_ddot = v_ddot_new;
}
```

That is essentially the paper's Eqs. (12) through (25) turned into an implementation workflow.  

---

# 17. The four operations you actually need

Once the FEM assembly is finished, nearly everything boils down to four linear-algebra operations.

### Matrix-vector product

$$
y=Ax
$$

MFEM conceptually:

```cpp
A.Mult(x, y);
```

Used everywhere:

$$
Mv,\ Cv,\ Kv.
$$

### Vector linear combination

$$
y=ax+bz.
$$

Conceptually:

```cpp
y = x;
y *= a;
y.Add(b, z);
```

Used constantly in Newmark.

### Matrix linear combination

$$
\hat K=K+a_6M+a_3C.
$$

How exactly you form this depends on whether you're using:

* serial `SparseMatrix`,
* parallel `HypreParMatrix`,
* block operators,
* partial assembly/GPU operator form.

But mathematically it is always the same thing.

### Linear solve

$$
Ax=b.
$$

Do **not** do

$$
x=A^{-1}b
$$

explicitly.

Use a solver.

---

# 18. One distinction that will save you headaches

There are really **three layers** in your software.

### Layer 1: Physics

Defines

$$
M_{uu},M_{pu},M_{pp},C_{uu},C_{pp},K_{uu},K_{up},K_{pp}.
$$

This is where MFEM integrators matter.

### Layer 2: Time integration

Only sees

$$
M,C,K,h.
$$

Newmark shouldn't care how $$K_{uu}$$ was assembled.

### Layer 3: Optimization / adjoint

Sees

$$
U^0,U^1,\ldots,U^N
$$

and

$$
R^0,R^1,\ldots,R^N.
$$

The paper later exploits the fact that the residual at time (n) only depends on the current and previous states,

$$
\boxed{
R^n=A,U^n+B,U^{n-1}.
}
$$

That is what gives the adjoint system its backwards-in-time structure. 

So architecturally I would keep those three layers separate.

---

## Your compact cheat sheet

Keep this somewhere:

$$
\boxed{
v=
\begin{bmatrix}u \\ p\end{bmatrix}
}
$$

$$
\boxed{
M\ddot v+C\dot v+Kv=h
}
$$

$$
\boxed{
\hat K=K+a_6M+a_3C
}
$$

$$
\boxed{
\hat K v^n=\hat h^n
}
$$

$$
\boxed{
\dot v^n
=

a_1\dot v^{n-1}
+a_2\ddot v^{n-1}
+a_3(v^n-v^{n-1})
}
$$

$$
\boxed{
\ddot v^n
=

-a_4\dot v^{n-1}
-a_5\ddot v^{n-1}
+a_6(v^n-v^{n-1})
}
$$

and then

$$
\boxed{
r_1=\hat Kv^n-\hat h^n
}
$$

$$
\boxed{
r_2=
\dot v^n-
\text{Newmark velocity prediction}
}
$$

$$
\boxed{
r_3=
\ddot v^n-
\text{Newmark acceleration prediction}.
}
$$

That's the bridge from **MFEM assembly → matrices → Newmark → states → residuals**.


Yep. Here’s the upgraded version: **same lookup table, but now with the actual constructor arguments and MFEM implementation pattern**.

I checked MFEM’s docs for the relevant integrator signatures. `ElasticityIntegrator` takes Lamé coefficient objects $$(\lambda,\mu)$$; `VectorMassIntegrator`, `MassIntegrator`, `DiffusionIntegrator`, and `BoundaryMassIntegrator` all take MFEM coefficient objects. [1]

## Matrix construction lookup table

| Block    | Mathematical definition                                                         | Inputs you must have                             | MFEM integrator                                        | MFEM implementation                                                          |
| -------- | ------------------------------------------------------------------------------- | ------------------------------------------------ | ------------------------------------------------------ | ---------------------------------------------------------------------------- |
| $$M_{uu}$$ | $$\displaystyle (M_{uu})_{ij}=\int_\Omega \rho_s N_i^u\cdot N_j^u\,d\Omega$$ | `fes_u`, $$\rho_s(x)$$                           | `VectorMassIntegrator(Coefficient &q)`                 | `M_uu.AddDomainIntegrator(new VectorMassIntegrator(rho_s));`                 |
| $$M_{up}$$ | $$0$$                                                                           | none                                             | none                                                   | no block                                                                     |
| $$M_{pu}$$ | $$\displaystyle-\int_{\Gamma_{as}}N_i^p(n_s\cdot N_j^u)d\Gamma$$                  | `fes_u`, `fes_p`, interface marker, normal $$n_s$$ | custom mixed boundary integrator                       | custom `MixedBilinearForm`                                                   |
| $$M_{pp}$$ | $$\displaystyle\int_\Omega \frac1{K_a}N_i^pN_j^p,d\Omega$$                        | `fes_p`, $$1/K_a(x)$$                              | `MassIntegrator(Coefficient &q)`                       | `M_pp.AddDomainIntegrator(new MassIntegrator(inv_Ka));`                      |
| $$C_{uu}$$ | $$\alpha_dM_{uu}+\beta_dK_{uu}$$                                                  | $$\alpha_d,\beta_d,\rho_s,\lambda,\mu$$            | vector mass + elasticity                               | two domain integrators                                                       |
| $$C_{up}$$ | $$0$$                                                                           | none                                             | none                                                   | no block                                                                     |
| $$C_{pu}$$ | $$0$$                                                                           | none                                             | none                                                   | no block                                                                     |
| $$C_{pp}$$ | $$\displaystyle\int_{\Gamma_{ar}}\frac1{\rho_ac_a}N_iN_j,d\Gamma$$                | `fes_p`, $$1/(\rho_ac_a)$$, boundary marker        | `BoundaryMassIntegrator(Coefficient &q)`               | `C_pp.AddBoundaryIntegrator(new BoundaryMassIntegrator(inv_rho_c), marker);` |
| $$K_{uu}$$ | $$\displaystyle\int_\Omega \varepsilon(w_i):\mathbb C:\varepsilon(N_j^u)d\Omega$$ | `fes_u`, $$\lambda(x),\mu(x)$$                     | `ElasticityIntegrator(Coefficient &l, Coefficient &m)` | `K_uu.AddDomainIntegrator(new ElasticityIntegrator(lambda, mu));`            |
| $$K_{up}$$ | $$\displaystyle-\int_{\Gamma_{as}}(w_i\cdot n_a)N_j^p,d\Gamma$$                   | `fes_p`, `fes_u`, interface marker, normal       | custom mixed boundary integrator                       | custom `MixedBilinearForm`                                                   |
| $$K_{pu}$$ | $$0$$                                                                           | none                                             | none                                                   | no block                                                                     |
| $$K_{pp}$$ | $$\displaystyle\int_\Omega\frac1{\rho_a}\nabla N_i^p\cdot\nabla N_j^p,d\Omega$$   | `fes_p`, $$1/\rho_a(x)$$                           | `DiffusionIntegrator(Coefficient &q)`                  | `K_pp.AddDomainIntegrator(new DiffusionIntegrator(inv_rho_a));`              |
| $$h_p$$    | incident-wave boundary integral                                                 | `fes_p`, $$\dot p_{in}(t)$$, boundary marker       | `LinearForm` boundary integrator                       | boundary RHS assembly                                                        |

The standard MFEM constructors are documented exactly this way. For example, `ElasticityIntegrator(lambda, mu)` implements

$$
a(u,v)
=

(\lambda,\operatorname{div}u,\operatorname{div}v)
+
(2\mu\varepsilon(u),\varepsilon(v)),
$$

which is precisely your isotropic elasticity weak form. [1]

---

# 1. Inputs you define first

Assuming 3D:

```cpp
int dim = mesh.Dimension();  // 3
int order = 1;

H1_FECollection fec(order, dim);

FiniteElementSpace fes_u(&mesh, &fec, dim); // ux,uy,uz
FiniteElementSpace fes_p(&mesh, &fec);      // scalar pressure
```

Material inputs:

```cpp
double E      = 50e6;
double nu     = 0.4;
double rho_s0 = 1000.0;

double rho_a0 = 1.21;
double c_a    = 343.0;

double K_a0 = rho_a0 * c_a * c_a;
```

For 3D elasticity,

```cpp
double lambda0 =
    E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));

double mu0 =
    E / (2.0 * (1.0 + nu));
```

Then MFEM wants these as `Coefficient`s:

```cpp
ConstantCoefficient lambda(lambda0);
ConstantCoefficient mu(mu0);
ConstantCoefficient rho_s(rho_s0);

ConstantCoefficient inv_Ka(1.0 / K_a0);
ConstantCoefficient inv_rho_a(1.0 / rho_a0);
ConstantCoefficient inv_rho_c(1.0 / (rho_a0 * c_a));
```

Later, your topology optimization will replace many of these `ConstantCoefficient`s with spatially varying coefficients.

---

# 2. $$M_{uu}$$

Mathematics:

$$
\boxed{
(M_{uu})_{ij}
=

\int_\Omega
\rho_s N_i^u\cdot N_j^u,d\Omega.
}
$$

Inputs:

$$
\rho_s(x),\qquad V_u.
$$

MFEM:

```cpp
BilinearForm M_uu(&fes_u);

M_uu.AddDomainIntegrator(
    new VectorMassIntegrator(rho_s)
);

M_uu.Assemble();
M_uu.Finalize();
```

MFEM documents `VectorMassIntegrator(Coefficient &q)` as the vector bilinear form

$$
(Q u,v).
$$

[2]

So here

$$
Q=\rho_s.
$$

---

# 3. $$K_{uu}$$

Mathematics:

$$
\boxed{
(K_{uu})_{ij}
=

\int_\Omega
\varepsilon(N_i^u):
\mathbb C:
\varepsilon(N_j^u)
,d\Omega.
}
$$

Inputs:

$$
\lambda(x),\quad\mu(x),\quad V_u.
$$

MFEM:

```cpp
BilinearForm K_uu(&fes_u);

K_uu.AddDomainIntegrator(
    new ElasticityIntegrator(lambda, mu)
);

K_uu.Assemble();
K_uu.Finalize();
```

Constructor:

```cpp
ElasticityIntegrator(Coefficient &lambda,
                     Coefficient &mu)
```

MFEM then computes the strain internally. [1]

You do **not** manually make the six strain components.

---

# 4. $$C_{uu}$$

Mathematically,

$$
\boxed{
C_{uu}
=

\alpha_dM_{uu}
+
\beta_dK_{uu}.
}
$$

Suppose:

```cpp
double alpha_d = ...;
double beta_d  = ...;
```

Create the scaled coefficients:

```cpp
ConstantCoefficient alpha_rho(
    alpha_d * rho_s0
);

ConstantCoefficient beta_lambda(
    beta_d * lambda0
);

ConstantCoefficient beta_mu(
    beta_d * mu0
);
```

Then:

```cpp
BilinearForm C_uu(&fes_u);

C_uu.AddDomainIntegrator(
    new VectorMassIntegrator(alpha_rho)
);

C_uu.AddDomainIntegrator(
    new ElasticityIntegrator(beta_lambda,
                             beta_mu)
);

C_uu.Assemble();
C_uu.Finalize();
```

This directly assembles

$$
\alpha_d
\int\rho_s w\cdot u
+
\beta_d
\int\varepsilon(w):C:\varepsilon(u).
$$

---

# 5. $$M_{pp}$$

Mathematics:

$$
\boxed{
(M_{pp})_{ij}
=

\int_\Omega
\frac1{K_a}
N_i^p N_j^p,d\Omega.
}
$$

Inputs:

$$
1/K_a(x),\qquad V_p.
$$

MFEM:

```cpp
BilinearForm M_pp(&fes_p);

M_pp.AddDomainIntegrator(
    new MassIntegrator(inv_Ka)
);

M_pp.Assemble();
M_pp.Finalize();
```

Constructor:

```cpp
MassIntegrator(Coefficient &q)
```

where here

$$
q=\frac1{K_a}.
$$

MFEM documents this constructor directly. [3]

---

# 6. $$K_{pp}$$

Mathematics:

$$
\boxed{
(K_{pp})_{ij}
=

\int_\Omega
\frac1{\rho_a}
\nabla N_i^p\cdot\nabla N_j^p
,d\Omega.
}
$$

Inputs:

$$
1/\rho_a(x),\qquad V_p.
$$

MFEM:

```cpp
BilinearForm K_pp(&fes_p);

K_pp.AddDomainIntegrator(
    new DiffusionIntegrator(inv_rho_a)
);

K_pp.Assemble();
K_pp.Finalize();
```

Constructor:

```cpp
DiffusionIntegrator(Coefficient &q)
```

implements

$$
(Q\nabla u,\nabla v).
$$

Here

$$
Q=\frac1{\rho_a}.
$$

[4]

---

# 7. $$C_{pp}$$

From the absorbing boundary:

$$
n\cdot\nabla p
+
\frac1{c_a}\dot p
=

\frac2{c_a}\dot p_{in}.
$$

You get

$$
\boxed{
(C_{pp})_{ij}
=

\int_{\Gamma_{ar}}
\frac1{\rho_ac_a}
N_i^pN_j^p,d\Gamma.
}
$$

Inputs:

* `fes_p`
* coefficient $$1/(\rho_ac_a)$$
* boundary marker identifying $$\Gamma_{ar}$$

For example:

```cpp
Array<int> absorbing_marker(mesh.bdr_attributes.Max());
absorbing_marker = 0;

// Suppose attribute 3 is the absorbing boundary
absorbing_marker[3 - 1] = 1;
```

Then:

```cpp
BilinearForm C_pp(&fes_p);

C_pp.AddBoundaryIntegrator(
    new BoundaryMassIntegrator(inv_rho_c),
    absorbing_marker
);

C_pp.Assemble();
C_pp.Finalize();
```

MFEM's documented constructor is

```cpp
BoundaryMassIntegrator(Coefficient &q)
```

and it is specifically a boundary mass operator. [5]

---

# 8. $$K_{up}$$: pressure $$\rightarrow$$ structure

Here you leave the world of standard same-space integrators.

Mathematically,

$$
\boxed{
(K_{up})_{ij}
=

-\int_{\Gamma_{as}}
(N_i^u\cdot n_a)
N_j^p
,d\Gamma.
}
$$

Notice the input and output spaces differ:

$$
p
\in V_p
$$

goes to

$$
f_u\in V_u.
$$

So the dimensions are

$$
\boxed{
K_{up}\in\mathbb R^{N_u\times N_p}.
}
$$

Inputs:

* pressure trial FE space `fes_p`
* displacement test FE space `fes_u`
* interface $$\Gamma_{as}$$
* normal $$n_a$$

Conceptually:

```cpp
MixedBilinearForm K_up(&fes_p, &fes_u);
```

then something like

```cpp
K_up.AddBoundaryIntegrator(
    new YourPressureToStructureIntegrator(),
    interface_marker
);
```

You will probably need a **custom mixed boundary integrator** because the operation is specifically

$$
p\mapsto p,n.
$$

The core element-level operation is:

```text
for each interface quadrature point:
    evaluate pressure shape Np_j
    evaluate displacement test shape Nu_i
    evaluate normal n

    K_up(i,j) +=
        -(Nu_i dot n) * Np_j
        * weight
```

This is one of the blocks I would not hide from yourself initially.

---

# 9. $$M_{pu}$$: structure acceleration $$\rightarrow$$ pressure

Mathematically,

$$
\boxed{
(M_{pu})_{ij}
=

-\int_{\Gamma_{as}}
N_i^p
(n_s\cdot N_j^u)
,d\Gamma.
}
$$

Dimensions:

$$
\boxed{
M_{pu}\in\mathbb R^{N_p\times N_u}.
}
$$

Inputs:

* displacement trial space `fes_u`
* pressure test space `fes_p`
* interface marker
* normal

Conceptually:

```cpp
MixedBilinearForm M_pu(&fes_u, &fes_p);
```

with custom integrator:

```cpp
M_pu.AddBoundaryIntegrator(
    new YourStructureToPressureIntegrator(),
    interface_marker
);
```

At quadrature level:

```text
M_pu(i,j) +=
    -Np_i * (n dot Nu_j)
    * weight
```

And, depending on your normal conventions,

$$
M_{pu}
$$

should be very closely related to

$$
K_{up}^T.
$$

That gives you a good debugging check.

---

# 10. Global matrices

After individual blocks exist,

$$
M=
\begin{bmatrix}
M_{uu}&0 \\
M_{pu}&M_{pp}
\end{bmatrix},
$$

$$
C=
\begin{bmatrix}
C_{uu}&0 \\
0&C_{pp}
\end{bmatrix},
$$

$$
K=
\begin{bmatrix}
K_{uu}&K_{up} \\
0&K_{pp}
\end{bmatrix}.
$$

For serial explicit matrices, your block entries eventually become `SparseMatrix`-type operators.

For parallel full assembly, you generally work with `HypreParMatrix`; MFEM's own parallel example forms the true-DOF system as a `HypreParMatrix`. [6]

You can still organize them as blocks first.

---

# 11. Compact MFEM constructor cheat sheet

This is probably the part worth screenshotting.

| Physics                    | MFEM constructor                    | Argument meaning                              |
| -------------------------- | ----------------------------------- | --------------------------------------------- |
| solid mass                 | `VectorMassIntegrator(rho_s)`       | `rho_s : Coefficient&`                        |
| solid stiffness            | `ElasticityIntegrator(lambda, mu)`  | `lambda, mu : Coefficient&`                   |
| acoustic mass              | `MassIntegrator(inv_Ka)`            | `inv_Ka = 1/Ka`                               |
| acoustic stiffness         | `DiffusionIntegrator(inv_rho_a)`    | `inv_rho_a = 1/rho_a`                         |
| acoustic absorbing damping | `BoundaryMassIntegrator(inv_rho_c)` | `inv_rho_c = 1/(rho_a*c_a)`                   |
| pressure → structure       | custom mixed boundary integrator    | pressure shape, vector test shape, normal     |
| structure → pressure       | custom mixed boundary integrator    | vector trial shape, scalar test shape, normal |

And the common pattern is always

```cpp
BilinearForm A(&fes);

A.AddDomainIntegrator(
    new SomeIntegrator(coefficient)
);

A.Assemble();
A.Finalize();
```

or for boundary physics:

```cpp
A.AddBoundaryIntegrator(
    new SomeBoundaryIntegrator(coefficient),
    boundary_marker
);
```

For parallel:

```cpp
ParBilinearForm A(&pfes);
```

but the physics integrator itself usually remains the same.

The core mental lookup is:

$$
\rho_s
\rightarrow
\boxed{\texttt{VectorMassIntegrator}}
$$

$$
\lambda,\mu
\rightarrow
\boxed{\texttt{ElasticityIntegrator}}
$$

$$
1/K_a
\rightarrow
\boxed{\texttt{MassIntegrator}}
$$

$$
1/\rho_a
\rightarrow
\boxed{\texttt{DiffusionIntegrator}}
$$

$$
1/(\rho_ac_a)
\rightarrow
\boxed{\texttt{BoundaryMassIntegrator}}.
$$

That gives you almost all diagonal blocks of the coupled vibroacoustic system; the two interface blocks are the custom part.

[1]: https://docs.mfem.org/4.8/classmfem_1_1ElasticityIntegrator.html?utm_source=chatgpt.com "MFEM: mfem::ElasticityIntegrator Class Reference"
[2]: https://docs.mfem.org/4.8/classmfem_1_1VectorMassIntegrator.html?utm_source=chatgpt.com "MFEM: mfem::VectorMassIntegrator Class Reference"
[3]: https://docs.mfem.org/4.8/classmfem_1_1MassIntegrator.html?utm_source=chatgpt.com "MFEM: mfem::MassIntegrator Class Reference"
[4]: https://docs.mfem.org/4.8/classmfem_1_1DiffusionIntegrator.html?utm_source=chatgpt.com "MFEM: mfem::DiffusionIntegrator Class Reference"
[5]: https://docs.mfem.org/4.8/classmfem_1_1BoundaryMassIntegrator.html?utm_source=chatgpt.com "MFEM: mfem::BoundaryMassIntegrator Class Reference"
[6]: https://docs.mfem.org/html/ex0p_8cpp_source.html?utm_source=chatgpt.com "MFEM: examples/ex0p.cpp Source File"
