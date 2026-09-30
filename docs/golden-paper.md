# Topology optimization of transient vibroacoustic problems for broadband filter design using cut elements

Cetin B. Dilgen and Niels Aage

Centre for Acoustic-Mechanical Micro Systems (CAMM), Department of Mechanical Engineering, Technical University of Denmark, Nils Koppels Allé, Building 404, DK-2800, Denmark

*Finite Elements in Analysis & Design* 234 (2024), article 104123. [https://doi.org/10.1016/j.finel.2024.104123](https://doi.org/10.1016/j.finel.2024.104123)

Received 13 July 2023; revised 12 January 2024; accepted 18 January 2024; available online 9 February 2024.

Copyright 2024 The Authors. Open access under the [Creative Commons Attribution 4.0 license](https://creativecommons.org/licenses/by/4.0/).

> Conversion note: Converted from `golden-paper.pdf` with PyMuPDF. The prose was taken from the embedded text layer; equations were checked against rendered pages and transcribed to LaTeX; figures were extracted at source resolution (with vector-only figures rendered at 3x); tables and references were reconstructed. Original PDF page numbers are retained as comments. Yellow highlight annotations in the supplied PDF are summarized after the article.

## Abstract

The focus of this article is on shape and topology optimization of transient vibroacoustic problems. The main contribution is a transient problem formulation that enables optimization over wide ranges of frequencies with complex signals, which are often of interest in industry. The work employs time-domain methods to realize wide-band optimization in the frequency domain. To this end, the objective function is defined in the frequency domain, where the frequency response of the system is obtained through a fast Fourier transform (FFT) of the transient response. The work utilizes a parametric level-set approach to implicitly define the geometry, in which the zero level describes the interface between acoustic and structural domains. A cut-element method captures the geometry on a fixed background mesh through a special integration scheme that accurately resolves the interface. This allows accurate solutions to strongly coupled vibroacoustic systems without remeshing at each design update. The present work relies on efficient gradient-based optimizers, with the discrete adjoint method used to calculate sensitivities of objective and constraint functions. A thorough explanation is given of the consistent sensitivity calculation involving the FFT operation needed to define the objective function in the frequency domain. Finally, the developed framework is applied to various vibroacoustic filter designs, and the optimization results are verified using commercial finite-element software with a steady-state time-harmonic formulation.

**Keywords:** Vibroacoustics; cut finite elements; immersed boundary methods; transient optimization; level-set methods; shape optimization.

<!-- PDF page 1 -->

## 1. Introduction

Topology optimization [1] is a numerical method to determine optimal material distributions that minimizes a given performance criteria under a set of constraints. The method has gained increasing popularity across many areas in both research and industry since it allows for innovative designs through free material distribution. Considerable number of studies on topology optimization paved the way for giga scale structural optimization for static structural mechanics problems [2]. The method has also been applied to the optimization of fluid systems [3,4] as well as several multi-physics applications [5,6]. Considering dynamics, after the introduction of the adjoint method for transient problems [7,8] examples of topology optimization under transient loadings can be found in the works of [9-11]. Moreover, optimizing for complex signals containing wide range of frequency content is of great interest in industry, for example, optimizing parts of a hearing aid device which requires a good performance expectancy over a wide frequency window. Commonly, frequency domain modeling and optimization only deals with discrete frequencies which are usually selected from a limited window of the frequency band. Optimization can quickly become excessively expensive when many frequencies

<!-- PDF page 2 -->

are considered in order to widen the zone of influence of optimization on the frequency response of the system. Examples of topology optimization in frequency domain can be found considering eigenvalue problems [12], optics [13], acoustics [14-17] and vibroacoustics [18-24]. Here it should also be noted that the difficulty in choosing the right objective function is much complicated, and often an ambiguous task, for dynamic problems [25]. Using a time-dependent problem formulation presents a promising alternative to address this issue since the optimization can be carried out using complex and compact signals that contains broad ranges of frequencies. This idea can be seen in the works of [26-29] where a representative time-domain input pulse is selected to excite a broad frequency range in order to carry out transient topology optimization of antennas. However, this approach still does not provide the full control on the broad-band response in the frequency domain. This is because the objective function is defined in the time domain and the optimization only indirectly effects the frequency content of the signal that is being optimized.

Although density based topology optimization methods provides the largest degree of design freedom, the ersatz material model presents several issues for multi-physics problems that are strongly coupled through the interface. The main reasons for this can be listed as the lack of physical interpretation of the intermediate densities and the stair-case (pixelized) boundary description. Generally, strongly coupled problems, i.e. vibroacoustics, require accurate modeling of the interface in order to correctly capture and model the interactions between the two physics. As an alternative to density methods, level set based methods [30] show great promise wrt coupled multi-physics problems. These methods implicitly defines the geometry by an iso-level of the level set function, which is usually taken as the zero level contour of the level set function. However, when such methods are used with an ersatz material approach [31], the level set function is - similar to density methods - mapped onto a piece-wise constant density field and the interface is still represented with an interpolated gray area. Hence, such level set methods suffers the same drawbacks as the density based topology optimization for coupled problems. Alternatively, the geometry defined by the zero level of the level set function can also be captured with body fitted meshes [32] in which a very accurate modeling of the coupled physics can be realized depending on the quality of the elements along the re-meshed interface. This approach commonly uses the solution of a Hamilton Jacobi type equation to update the design by moving the interface based on the calculated shape sensitivities. However, due to the re-meshing operation at each design iteration, the approach is not efficient for parallel computing frameworks. Moreover, numerical noise may be introduced in the sensitivities due to the extrapolation from the fitted mesh onto the background mesh. Examples of classical level set approach for the optimization of vibroacoustic systems can be found in [33-35]. A detailed comparative review of density, level set and evolutionary based optimization methods for vibroacoustics can be found in the work of [36].

Cumbersome re-meshing operation can be avoided when the level set approach is coupled with immersed boundary methods [37] to capture the geometry. The present work utilizes the immersed boundary cut element method presented in [38] to model the exact (piece-wise linear) boundary represented by the zero contour. Throughout the optimization, a fixed mesh is used and the accurate modeling of the interface elements is realized through a special integration scheme. The employed cut element method can be categorized as a special (simplified) case of CutFEM without stabilization [39], finite cell method [40] or X-FEM without enrichment [41]. For the optimization, the utilized cut element method is coupled with the so-called explicit level set approach [42,43] in which the nodal level set values are directly tied to the mathematical design variables. This approach enables the utilization of nonlinear programming tools, such as the Method of Moving Asymptotes (MMA) algorithm [44] that is used in the current work, and in turn allows for general optimization frameworks where multiple objective and constraint functions easily can be considered in the optimization.

The current work focuses on topology, or generalized shape, optimization of transient vibroacoustic problems where a time dependent problem formulation allows the realization of wideband performance optimization in the frequency domain. Throughout the work, the frequency response of the coupled vibroacoustic system is obtained through a fast Fourier transform (FFT) algorithm applied to the transient response of the system. This allows the objective function to be defined in frequency domain where the optimization can directly tailor the response of the coupled system in order to realize different filter shapes. The performance of the optimized design in realizing pass-band and stop-band for the considered frequency range relies on several, individual resonators and reflectors vibrating according to the underlying acoustic-structural interactions. To the best of the authors’ knowledge, utilization of time-domain methods to realize wideband optimization in the frequency domain have not been demonstrated before for structural optimization of strongly coupled vibroacoustic problems. Moreover, the work utilizes the discrete adjoint method for calculating the gradients of the objective function [45,46]. A thorough explanation of the consistent sensitivity calculation through the discrete adjoint framework that involves the FFT operation, which is needed to define the objective function in frequency domain, is also among the contributions of the present work. The remainder of the paper is organized as follows. Section 2 introduces the level set geometry representation, the vibroacoustic governing equations along with the immersed boundary formulation and spatial and temporal discretizations. The optimization problem as well as the sensitivity analysis using the discrete adjoint method are introduced in Section 3. Section 4 describes the numerical setup that is used for the considered examples throughout the work. The numerical examples are presented in Section 5 where the developed framework is applied for the optimization of various vibroacoustic filter designs.

## 2. Theory and methods

### 2.1. Geometry representation

As the aim of this work is to perform structural optimization for vibroacoustic design problems, it is natural to start by introducing the geometry representation. Throughout this work, the geometry is represented by a scalar valued function s, i.e. a level set function, which is used to identify the acoustic and structural domains embedded inside the total computational domain Ω= Ωs∪Ωa as

<!-- PDF page 3 -->

![Figure 1](golden-paper-assets/figure-01.png)

*Fig. 1. An example level set function showing the embedded physical domains. (a) shows the rule that is used for specifying different physical domains embedded with the level set function. (b) finite element mesh where white color shows the uncut elements in the acoustic domain, gray color shows the uncut elements in the solid region and the blue color shows the cut elements.*

illustrated in Fig. 1(a). This work follows the same geometry representation as used in [46,47] meaning that the iso-level is chosen as the zero-level of the level set function and hence, that the following rule is used to determine acoustic and structural material phases

<a id="eq-1"></a>

$$
\begin{cases}\bar{s}(\mathbf{x}) > 0, & \mathbf{x}\in\Omega_s \quad \text{(structural domain)},\\ \bar{s}(\mathbf{x}) = 0, & \mathbf{x}\in\Gamma_{as} \quad \text{(interface)},\\ \bar{s}(\mathbf{x}) < 0, & \mathbf{x}\in\Omega_a \quad \text{(acoustic domain)}.\end{cases} \tag{1}
$$

This allows for a versatile geometry description capable of representing complex geometries using the rule given in Eq. (1) where positive level set values represent the structural domain and negative values identify the acoustic domain. Such an approach is especially well-suited for acoustic-structure interaction problems, where the zero iso-level of the function s provides a clear definition of the interface Γas between the two physical domain.

### 2.2. Governing equations

The vibroacoustic system considered in this work consists of plane stress linear elasticity and the linear acoustic wave equation formulated in the time domain as shown in Fig. 2. In order to realize an efficient parallel modeling and optimization framework, we once again emphasize that this work utilizes an immersed boundary cut element method in which the solutions to both physics are obtained in the entire computational domain Ω. This is achieved by application of a fictitious domain approach in which, for the structural domain, a void phase with low stiffness is defined in the acoustic region Ωa. For the solution of the acoustic pressure in its fictitious domain (structural domain Ωs), a rigid phase is defined by modifying the material properties of the acoustic medium. To this end, considering a transient motion, the linear elasticity equation governing the structural response is written as

$$
\begin{aligned}
\rho_s(\mathbf{x})\frac{\partial^2\mathbf{u}}{\partial t^2}-\nabla\!\cdot\boldsymbol{\sigma}(\mathbf{x})+\rho_s(\mathbf{x})\alpha_d\frac{\partial\mathbf{u}}{\partial t}-\nabla\!\cdot\left(\beta_d\frac{\partial\boldsymbol{\sigma}(\mathbf{x})}{\partial t}\right)=0 \quad \text{in }\Omega \tag{2} \\
\mathbf{u}=0 \quad \text{on }\Gamma_{sd} \tag{3} \\
\mathbf{n}_s\cdot\boldsymbol{\sigma}=0 \quad \text{on }\Gamma_{sn} \tag{4} \\
\mathbf{n}_s\cdot\boldsymbol{\sigma}=p\mathbf{n}_a \quad \text{on }\Gamma_{as} \tag{5}
\end{aligned}
$$

where $\mathbf{u}$ is the displacement vector, $p$ is the acoustic pressure, $\rho_s(\mathbf{x})$ is the spatially varying density of the solid, $\mathbf{n}_a$ is the normal vector pointing outwards from the acoustic region, and $\mathbf{n}_s$ is the normal vector defined at the interface pointing outwards from the structural domain. To reflect loss, the work considers structural damping using Rayleigh damping, where $\alpha_d$ and $\beta_d$ are the usual Rayleigh damping parameters. Furthermore, the spatially varying Cauchy stress vector is $\boldsymbol{\sigma}(\mathbf{x})=\mathcal{C}(E_s(\mathbf{x}),\nu)\boldsymbol{\epsilon}$, where the constitutive matrix $\mathcal{C}(E_s,\nu)$ is a function of the spatially varying Young’s modulus $E_s(\mathbf{x})$ and Poisson’s ratio $\nu$, and the strain vector is $\boldsymbol{\epsilon}=[\partial u_1/\partial x,\partial u_2/\partial y,\partial u_1/\partial y+\partial u_2/\partial x]^T$. Equation (3) defines the fully clamped condition, Eq. (4) the traction-free condition, and Eq. (5) the coupling to the acoustic medium. To calculate the transient structural response in both the structural domain and its void phase (the acoustic domain), the solid material properties are altered as

<a id="eq-6"></a>

$$
E_s(\mathbf{x})=\alpha(\mathbf{x})\widetilde{E}_s,\qquad \rho_s(\mathbf{x})=\alpha(\mathbf{x})\widetilde{\rho}_s \tag{6}
$$

<!-- PDF page 4 -->

![Figure 2](golden-paper-assets/figure-02.png)

*Fig. 2. Schematic illustration of the coupled acoustic-structural system. Physical domains, the coupled interface and the boundary conditions are showed.*

where the tilde superscript specifies the original material properties and the parameter α(x) is a dimensionless contrast parameter that is unity for the structural domain and taken as $10^{-8}$ for the void phase of the structure. The acoustic pressure on the other hand is governed by the transient Helmholtz equation which is written as

$$
\begin{aligned}
\frac{1}{K_a(\mathbf{x})}\frac{\partial^2p}{\partial t^2}-\frac{1}{\rho_a(\mathbf{x})}\nabla^2p=0 \quad \text{in }\Omega \tag{7} \\
\mathbf{n}_a\cdot\nabla p=0 \quad \text{on }\Gamma_{ad} \tag{8} \\
\mathbf{n}_a\cdot\nabla p=\rho_a\frac{\partial^2(\mathbf{n}_s\cdot\mathbf{u})}{\partial t^2} \quad \text{on }\Gamma_{as} \tag{9} \\
\mathbf{n}_a\cdot\nabla p+\frac{1}{c_a}\frac{\partial p}{\partial t}=\frac{2}{c_a}\frac{\partial p_{in}}{\partial t} \quad \text{on }\Gamma_{ar} \tag{10}
\end{aligned}
$$

where $\rho_a(\mathbf{x})$ is the spatially varying density of the acoustic medium and $c_a$ is the speed of sound in that medium. The spatially varying bulk modulus is $K_a(\mathbf{x})=\rho_a(\mathbf{x})c_a^2$. The boundary conditions for the transient acoustic-pressure solution are the hard-wall condition in Eq. (8), the acoustic coupling condition in Eq. (9), and the absorbing boundary condition in Eq. (10). The absorbing condition is written for plane-wave radiation, where $p_{in}$ denotes the transient incoming acoustic wave. As with the structural equation, the transient acoustic pressure is obtained in both the acoustic domain and its rigid phase (the structural domain) by changing the acoustic material properties as

<a id="eq-11"></a>

$$
K_a(\mathbf{x})=\frac{\widetilde{K}_a}{\alpha(\mathbf{x})},\qquad \rho_a(\mathbf{x})=\frac{\widetilde{\rho}_a}{\alpha(\mathbf{x})} \tag{11}
$$

The dimensionless contrast parameter α(x) is similar to that used for the structural and takes the values of unity for the acoustic domain and $10^{-8}$ for the rigid phase of the acoustic solution. A complete illustration of the resulting coupled acoustic-structure system considered in this work is given in Fig. 2 along with all the used boundary conditions. Note that the use of a single fictitious scaling parameter for both stiffness and density is known to cause issues with spurious modes in the fictitious domains that can lead to unphysical phenomena. However, throughout extensive numerical experiments, it was found that this did not pose any problems for the proposed method and we have therefore decided to use a single fixed fictitious domain parameter.

### 2.3. Discretization

This section describes the spatial and temporal discretizations for the coupled vibroacoustic system introduced in the previous section The spatial discretization is obtained using the standard continuous Galerkin method [48] using Q4 elements and a special integration scheme is employed for the cut elements and the coupling interface, shown in blue and black, respectively, in Fig. 1. In short, the utilized cut element approach employs over-integration using a Gaussian quadrature rule for modeling the interface boundary through weighted integration. To this end, a triangulation is carried out in the identified cut Q4 elements to correctly place the integration points in the sub-elements as well as on the interface in order to integrate the coupling boundary conditions, Eqs. (5) and (9), inside the parent Q4 element. The reader is referred to [46] for details regarding all necessary integrals. The resulting semi-discrete form of the coupled system then reads

<a id="eq-12"></a>

$$
\mathbf{M}\ddot{\mathbf{v}}^n+\mathbf{C}\dot{\mathbf{v}}^n+\mathbf{K}\mathbf{v}^n=\mathbf{h}^n \tag{12}
$$

where M is the mass matrix, C is the damping matrix and K is the stiffness matrix. The superscript n in Eq. (12) denotes the current time step. The combined state and load vectors, v and h, are given as

<a id="eq-13"></a>

$$
\mathbf{v}=\begin{bmatrix}\mathbf{u}\\\mathbf{p}\end{bmatrix},\qquad \mathbf{h}=\begin{bmatrix}\mathbf{0}\\\mathbf{g}\end{bmatrix} \tag{13}
$$

<!-- PDF page 5 -->

where u and p are state vectors for structural displacement and acoustic pressure solutions, respectively. The vector g denotes the source vector for the acoustic equation which is active when the absorption boundary condition (Eq. (10)) is utilized for radiating a plane wave. The weak form of the coupled system and the identification of the individual element matrices given in Eq. (12) are thoroughly described in [46].

For the temporal discretization, the current work employs an implicit time stepping scheme, namely the Newmark algorithm [49]. The method expresses the first and the second time derivative of the solution vector v as

$$
\begin{aligned}
\dot{\mathbf{v}}^n=a_1\dot{\mathbf{v}}^{n-1}+a_2\ddot{\mathbf{v}}^{n-1}+a_3\left(\mathbf{v}^n-\mathbf{v}^{n-1}\right) \tag{14} \\
\ddot{\mathbf{v}}^n=-a_4\dot{\mathbf{v}}^{n-1}-a_5\ddot{\mathbf{v}}^{n-1}+a_6\left(\mathbf{v}^n-\mathbf{v}^{n-1}\right) \tag{15}
\end{aligned}
$$

where the parameters a1 to a6 are the so-called Newmark parameters. In order to derive final and complete linear system of equations which is solved in order to determine the solution vector vn for the current time n, the definitions given in Eqs. (14) and (15) are substituted into the semi-discrete form of the coupled system (Eq. (12)). The resulting equation reads

<a id="eq-16"></a>

$$
\widehat{\mathbf{K}}\mathbf{v}^n=\widehat{\mathbf{h}}^n \tag{16}
$$

where ̂ K is an effective stiffness matrix and ̂ hn is the effective load vector, identified as

$$
\begin{aligned}
\widehat{\mathbf{K}}=\mathbf{K}+a_6\mathbf{M}+a_3\mathbf{C} \tag{17} \\
\widehat{\mathbf{h}}^n=\mathbf{h}^n+\mathbf{M}\left(a_4\dot{\mathbf{v}}^{n-1}+a_5\ddot{\mathbf{v}}^{n-1}+a_6\mathbf{v}^{n-1}\right)+\mathbf{C}\left(-a_1\dot{\mathbf{v}}^{n-1}-a_2\ddot{\mathbf{v}}^{n-1}+a_3\mathbf{v}^{n-1}\right) \tag{18}
\end{aligned}
$$

Furthermore, the Newmark parameters are identified as

<a id="eq-19"></a>

$$
\begin{aligned}a_1&=1-\frac{\widetilde{\gamma}}{\widetilde{\beta}}, & a_2&=\left(1-\frac{\widetilde{\gamma}}{2\widetilde{\beta}}\right)\Delta t, & a_3&=\frac{\widetilde{\gamma}}{\widetilde{\beta}\Delta t},\\ a_4&=\frac{1}{\widetilde{\beta}\Delta t}, & a_5&=\frac{1}{2\widetilde{\beta}}-1, & a_6&=\frac{1}{\widetilde{\beta}\Delta t^2}.\end{aligned} \tag{19}
$$

where Δt is the time step size. The current work uses a specific family of the Newmark algorithm [50] in which the parameters ̃ β and ̃ γ are selected so that the employed time discretization becomes unconditionally stable. ̃ β and ̃ γ are given as

<a id="eq-20"></a>

$$
\widetilde{\beta}=\frac14,\qquad \widetilde{\gamma}=\frac12 \tag{20}
$$

The presented work assumes the initial conditions v0 and ̇ v0 are zero which is a minor simplification compared to [46]. The initial condition for the second time derivative of the solution vector ̈ v0 is therefore found by setting the v0 and ̇ v0 to zero in Eq. (12) and solving the following equation

<a id="eq-21"></a>

$$
\mathbf{M}\ddot{\mathbf{v}}^0=\mathbf{h}^0 \tag{21}
$$

To simplify notation when formulating the optimization problem, the vectors of state variables and residuals are collected in two single vectors Un and Rn, respectively, which are given as

<a id="eq-22"></a>

$$
\mathbf{R}^n=\begin{bmatrix}\mathbf{r}_1^n\\\mathbf{r}_2^n\\\mathbf{r}_3^n\end{bmatrix},\qquad \mathbf{U}^n=\begin{bmatrix}\mathbf{v}^n\\\dot{\mathbf{v}}^n\\\ddot{\mathbf{v}}^n\end{bmatrix} \tag{22}
$$

Here the individual residual vectors $\mathbf{r}_1^n$, $\mathbf{r}_2^n$, and $\mathbf{r}_3^n$ are identified from the discretized coupled system in Eqs. (14)-(18) as

$$
\begin{aligned}
\mathbf{r}_1^n=[\mathbf{K}+a_6\mathbf{M}+a_3\mathbf{C}]\mathbf{v}^n-[a_6\mathbf{M}+a_3\mathbf{C}]\mathbf{v}^{n-1}-[a_4\mathbf{M}-a_1\mathbf{C}]\dot{\mathbf{v}}^{n-1}+[a_2\mathbf{C}-a_5\mathbf{M}]\ddot{\mathbf{v}}^{n-1}-\mathbf{h}^n \tag{23} \\
\mathbf{r}_2^n=\dot{\mathbf{v}}^n-a_1\dot{\mathbf{v}}^{n-1}-a_2\ddot{\mathbf{v}}^{n-1}-a_3[\mathbf{v}^n-\mathbf{v}^{n-1}] \tag{24} \\
\mathbf{r}_3^n=\ddot{\mathbf{v}}^n+a_4\dot{\mathbf{v}}^{n-1}+a_5\ddot{\mathbf{v}}^{n-1}-a_6[\mathbf{v}^n-\mathbf{v}^{n-1}] \tag{25}
\end{aligned}
$$

The solution of the coupled vibroacoustic system has been implemented in C++ using the PETSc library [51-53] for parallel data management. Moreover, the parallel direct solver MUMPS [54,55] is utilized for the solution of Eq. (16). Here it is noted that the validation study of the developed transient vibroacoustic framework is presented in [46].

## 3. Optimization formulation

### 3.1. Design parameterization

Having established the physical model and how it is capable of handling arbitrary geometries, the next step is to introduce the design parameterization. To achieve this, a mathematical design variable, s, is introduced and then tied to the previously defined level set function s through the operations presented here. The design variable is, similar to the physical design field s, defined on nodal points in the computational mesh and is bound between zero and one as this allows for easy use of the MMA optimizer, i.e.

<a id="eq-26"></a>

$$
0\le s\le1 \tag{26}
$$

<!-- PDF page 6 -->

Although this scaling is appropriate for the optimizer used in this work, it can lead to rapid speeds at which the zero iso-level can change in one design update. This can be alleviated with a mesh dependent mapping which ensures that the iso-level does not evolve too rapidly during the optimization by mapping the design variable into a new variable ̃ s which has the upper and lower bounds corresponding to half the element size [56] as

<a id="eq-27"></a>

$$
-0.5h_e\le\widetilde{s}(s)\le0.5h_e \tag{27}
$$

Lastly, a smoothing filter is applied to regularize and stabilize the optimization process and to obtain the physical design variable s. This is achieved by use of PDE filter [57] with Neumann boundary conditions, i.e.

<a id="eq-28"></a>

$$
-r^2\nabla^2\bar{s}_c+\bar{s}_c=\widetilde{s}_c \tag{28}
$$

where r is the filter radius and the subscript c denotes the variables that are defined in cell centers since the filter PDE is solved using a finite volume implementation. Therefore two additional smoothing steps are implied whenever the filter equation is applied due to interpolations between nodes of the mesh and cell centers (from ̃ s to ̃ sc and from sc to s). Here it is noted that the reason the filter PDE is solved with a finite volume implementation is that with using a finite volume method, a numerically stable result can be obtained with any filter radius value (it can even be set to zero). Finally, it should be remarked that no length scale is imposed as this is deemed outside the scope of the current work since the focus is on broadband frequency control.

### 3.2. Optimization problem formulation

To control structural and acoustic behavior over a wide range of frequencies, the optimization problem is cast in frequency space. The temporal state response is transformed using the FFT, yielding the frequency-domain state vector $\mathbf{U}_f^m$. Superscript $m$ denotes the frequency point and subscript $f$ distinguishes the vector from its temporal counterpart. Because of the FFT, $\mathbf{U}_f^m$ is complex. Formally, the discrete Fourier transform is

<a id="eq-29"></a>

$$
\mathbf{U}_f^m=\sum_{n=0}^{N-1}\mathbf{U}^n e^{-i\frac{2\pi}{N}mn} \tag{29}
$$

where $i=\sqrt{-1}$ and $N$ is the total number of time steps. A Hanning window is applied to the temporal signal before it is passed through the FFT, and the work uses FFTW [58]. The generic nested optimization problem is

$$
\begin{aligned}
\min_{\mathbf{s}}\quad \Phi=\sum_{m=0}^{M}\phi^m\!\left(\mathbf{U}_f^m(\bar{\mathbf{s}})\right) \tag{30} \\
\text{s.t.}\quad \mathbf{R}^n\!\left(\bar{\mathbf{s}},\mathbf{U}^n(\bar{\mathbf{s}})\right)=0,\quad n=0,1,\ldots,N \tag{31} \\
\psi_i\!\left(\mathbf{U}_f^m(\bar{\mathbf{s}})\right)\le0,\quad i=0,1,\ldots,K \tag{32} \\
s_{min}\le\mathbf{s}\le s_{max} \tag{33}
\end{aligned}
$$

where $\Phi$ is the frequency-domain objective and $M$ is the total number of discrete frequencies considered in it. The functions $\psi_i(\mathbf{U}_f^m(\bar{\mathbf{s}}))$ are the $K$ additional optimization constraints. The design vector is $\mathbf{s}$, with lower and upper bounds $s_{min}$ and $s_{max}$ set to 0 and 1 by Eq. (26). The residual equations must hold at every design step and depend on the physical design vector $\bar{\mathbf{s}}$, as shown in Eq. (31).

The optimization problem stated in Eqs. (30) to (33) is solved using the standard Method of Moving Asymptotes (MMA) algorithm [44]. Specifically, the present work includes the parallel implementation of MMA from [59].

### 3.3. Sensitivity analysis

Sensitivity information is required in order to utilize a gradient based optimizer for solving the problem in Eqs. (30)-(33), and this section provides the necessary details for a general discrete function defined in the frequency domain based on a discrete temporal response, i.e. Φ= Φ(Uf(U(s))). We remark that a common trend in time dependent sensitivity analysis is to use the so-called semi-discrete adjoint approach, e.g. [60], in which the problem is considered discrete in space but continuous in time. However, the semi-discrete approach is not suitable for cases where the objective function is defined discretely in the frequency domain using FFT. This is because the semi-discrete approach derives the adjoint equation assuming the objective function is defined in time domain. This means that using a discrete FFT cannot be incorporated within the semi-discrete approach. Recently the work of [46] also showed that even for pure transient optimization problems, the semi-discrete method may not produce consistent sensitivities which can result in gradients having wrong signs. Hence, for calculating consistent and exact sensitivities, the work utilizes a fully discrete sensitivity analysis for calculating the gradient of the objective function with respect to the design variable. However, it should be noted that if a continuous Fourier transform was used, the semi-discrete approach could still be used in which both the temporal and the frequency response are considered as continuous variables [61].

<!-- PDF page 7 -->

The derivation of the fully discrete sensitivity analysis is initiated with an augmentation of the objective function by the residual equations and a vector of Lagrangian multipliers Λ. Moreover, note that summation of multiple frequencies have been left out in the following to allow for a more condensed description, but that extension to multiple frequencies is straightforward. The Lagrangian function  then reads

<a id="eq-34"></a>

$$
\mathcal{L}=\Phi\!\left(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}}))\right)+\sum_{n=0}^{N}\boldsymbol{\Lambda}^{nT}\mathbf{R}^n\!\left(\bar{\mathbf{s}},\mathbf{U}^n(\bar{\mathbf{s}})\right) \tag{34}
$$

where it should be highlighted that the objective function is written as a function of discrete state variables in the frequency domain Uf which are an explicit function of the discrete transient state variables U. Due to the Newmark algorithm, similar to the state variables, the vector of Lagrangian multipliers also consists of three fields as

<a id="eq-35"></a>

$$
\boldsymbol{\Lambda}^n=\begin{bmatrix}\boldsymbol{\lambda}^n\\\dot{\boldsymbol{\lambda}}^n\\\ddot{\boldsymbol{\lambda}}^n\end{bmatrix} \tag{35}
$$

The derivative of the Lagrangian function with respect to the physical design variable s can now be written as

<a id="eq-36"></a>

$$
\frac{d\mathcal{L}}{d\bar{\mathbf{s}}}=\sum_{n=0}^{N}\left\{\left(\frac{\partial\Phi}{\partial\mathbf{U}_f}\frac{\partial\mathbf{U}_f}{\partial\mathbf{U}}\right)^n\frac{\partial\mathbf{U}^n}{\partial\bar{\mathbf{s}}}+\boldsymbol{\Lambda}^{nT}\left[\frac{\partial\mathbf{R}^n}{\partial\bar{\mathbf{s}}}+\frac{\partial\mathbf{R}^n}{\partial\mathbf{U}^n}\frac{\partial\mathbf{U}^n}{\partial\bar{\mathbf{s}}}\right]\right\} \tag{36}
$$

The first term on the right-hand side of Eq. (36), $(\partial\Phi/\partial\mathbf{U}_f)(\partial\mathbf{U}_f/\partial\mathbf{U})$, is the partial derivative $\partial\Phi^n/\partial\mathbf{U}$ with respect to the transient state and therefore contains the chain rule connecting the frequency and time domains. First $\partial\Phi/\partial\mathbf{U}_f$ is calculated. Because $\mathbf{U}_f$ is complex, this derivative is complex and is evaluated as

<a id="eq-37"></a>

$$
\frac{\partial\Phi}{\partial\mathbf{U}_f}=\frac{\partial\Phi}{\partial\mathbf{U}_{f,r}}+i\frac{\partial\Phi}{\partial\mathbf{U}_{f,i}} \tag{37}
$$

where subscripts $r$ and $i$ denote the real and imaginary parts. Equation (37) is evaluated for every frequency $m$ in the considered range. The term $\partial\mathbf{U}_f/\partial\mathbf{U}$ links the frequency and time domains. Since the discrete Fourier transform is linear, this partial derivative is the transform itself. Therefore, the derivative of the objective with respect to the transient state is

<a id="eq-38"></a>

$$
\frac{\partial\Phi^n}{\partial\mathbf{U}}=\left(\frac{\partial\mathbf{U}_f^T}{\partial\mathbf{U}}\frac{\partial\Phi}{\partial\mathbf{U}_f}\right)^n \tag{38}
$$

where $(\partial\mathbf{U}_f/\partial\mathbf{U})^T$ applies the inverse discrete Fourier transform. The operations in Eq. (38) are: (1) calculate $\partial\Phi/\partial\mathbf{U}_f$ and (2) apply the inverse transform to obtain $\partial\Phi^n/\partial\mathbf{U}$ in the time domain. The sensitivity becomes real after the inverse FFT. Formally,

<a id="eq-39"></a>

$$
\frac{\partial\Phi^n}{\partial\mathbf{U}}=\frac1N\sum_{m=0}^{N-1}\frac{\partial\Phi^m}{\partial\mathbf{U}_f}e^{i\frac{2\pi}{N}mn} \tag{39}
$$

which also shows how multiple frequencies are included.

Having introduced the chain rule describing the link between frequency and time domains, in order to continue the sensitivity analysis, the derivative of the Lagrangian function with respect to the physical design variable (Eq. (36)) is rewritten as

<a id="eq-40"></a>

$$
\frac{d\mathcal{L}}{d\bar{\mathbf{s}}}=\sum_{n=0}^{N}\left\{\boldsymbol{\Lambda}^{nT}\frac{\partial\mathbf{R}^n}{\partial\bar{\mathbf{s}}}+\left[\frac{\partial\Phi^n}{\partial\mathbf{U}}+\boldsymbol{\Lambda}^{nT}\frac{\partial\mathbf{R}^n}{\partial\mathbf{U}^n}\right]\frac{\partial\mathbf{U}^n}{\partial\bar{\mathbf{s}}}\right\} \tag{40}
$$

Because the Lagrangian vector can be chosen freely, the bracketed term in Eq. (40) is set to zero, avoiding direct calculation of $\partial\mathbf{U}^n/\partial\bar{\mathbf{s}}$. This gives the adjoint equation

<a id="eq-41"></a>

$$
\left(\frac{\partial\mathbf{R}}{\partial\mathbf{U}}\right)^T\boldsymbol{\Lambda}=-\frac{\partial\Phi}{\partial\mathbf{U}} \tag{41}
$$

where superscript $n$ is dropped to emphasize that Eq. (41) contains every time step. For a linear set of residuals, the Jacobian $\partial\mathbf{R}/\partial\mathbf{U}$ has the following general form:

<!-- PDF page 8 -->

<a id="eq-42"></a>

$$
\frac{\partial\mathbf{R}}{\partial\mathbf{U}}=\begin{bmatrix}\mathbf{A}_0^{\mathbf{U}^0}&&&&\\\mathbf{B}_1^{\mathbf{U}^0}&\mathbf{A}_1^{\mathbf{U}^1}&&&\\&\ddots&\ddots&&\\&&\mathbf{B}_{N-1}^{\mathbf{U}^{N-2}}&\mathbf{A}_{N-1}^{\mathbf{U}^{N-1}}&\\&&&\mathbf{B}_{N}^{\mathbf{U}^{N-1}}&\mathbf{A}_{N}^{\mathbf{U}^{N}}\end{bmatrix} \tag{42}
$$

Here, subscripts denote the current time step and superscripts denote the corresponding state variables. The submatrices in Eq. (42) represent a time-integration scheme that depends on the current and previous time steps. Thus the residual at time step $n$ can be written as

<a id="eq-43"></a>

$$
\mathbf{R}^n=\mathbf{A}\mathbf{U}^n+\mathbf{B}\mathbf{U}^{n-1} \tag{43}
$$

The submatrices $\mathbf{A}$ and $\mathbf{B}$ remain constant at each time step and can be identified from Eqs. (23)-(25); their explicit Newmark forms are given in [46]. The initial submatrix $\mathbf{A}_0$ follows from the initial conditions. Because Eq. (41) transposes the residual Jacobian, the adjoint is solved in reverse pseudo-time:

$$
\begin{aligned}
\mathbf{A}^T\boldsymbol{\Lambda}^{N}=-\frac{\partial\Phi^{N}}{\partial\mathbf{U}} \tag{44} \\
\mathbf{A}^T\boldsymbol{\Lambda}^{N-1}=-\frac{\partial\Phi^{N-1}}{\partial\mathbf{U}}-\mathbf{B}^T\boldsymbol{\Lambda}^{N} \tag{45} \\
\left(\mathbf{A}_0\right)^T\boldsymbol{\Lambda}^{0}=-\frac{\partial\Phi^{0}}{\partial\mathbf{U}}-\mathbf{B}^T\boldsymbol{\Lambda}^{1} \tag{46}
\end{aligned}
$$

After the adjoint equation is solved for the Lagrangian variables $\boldsymbol{\Lambda}^n$, the final sensitivity of the objective function is calculated as

<a id="eq-47"></a>

$$
\frac{d\Phi}{d\bar{\mathbf{s}}}=\boldsymbol{\Lambda}^{0T}\left[\frac{\partial\mathbf{A}_0}{\partial\bar{\mathbf{s}}}\mathbf{U}^0\right]+\sum_{n=1}^{N}\boldsymbol{\Lambda}^{nT}\left[\frac{\partial\mathbf{A}}{\partial\bar{\mathbf{s}}}\mathbf{U}^n+\frac{\partial\mathbf{B}}{\partial\bar{\mathbf{s}}}\mathbf{U}^{n-1}\right] \tag{47}
$$

For Newmark time stepping, explicit definitions of $\partial\mathbf{A}_0/\partial\bar{\mathbf{s}}$, $\partial\mathbf{A}/\partial\bar{\mathbf{s}}$, and $\partial\mathbf{B}/\partial\bar{\mathbf{s}}$ are given in [46]. Equation (47) requires storage of the transient forward states. Checkpointing could reduce memory use at the cost of another forward analysis. The gradient calculation is performed only for cut elements, since these matrix derivatives are zero elsewhere. Equation (47) gives the gradient with respect to the physical design variable $\bar{\mathbf{s}}$; the gradient with respect to the mathematical design variable $\mathbf{s}$ follows from

<a id="eq-48"></a>

$$
\frac{d\Phi}{d\mathbf{s}}=\frac{d\Phi}{d\bar{\mathbf{s}}}\frac{\partial\bar{\mathbf{s}}}{\partial\bar{\mathbf{s}}_c}\frac{\partial\bar{\mathbf{s}}_c}{\partial\widetilde{\mathbf{s}}_c}\frac{\partial\widetilde{\mathbf{s}}_c}{\partial\widetilde{\mathbf{s}}}\frac{\partial\widetilde{\mathbf{s}}}{\partial\mathbf{s}} \tag{48}
$$

The factors in Eq. (48) connect the physical design variable $\bar{\mathbf{s}}$ to the mathematical design variable $\mathbf{s}$ and are applied in reverse order. Here $\partial\widetilde{\mathbf{s}}/\partial\mathbf{s}$ changes the mathematical-variable bounds, $\partial\widetilde{\mathbf{s}}_c/\partial\widetilde{\mathbf{s}}$ interpolates from nodes to cell centers, $\partial\bar{\mathbf{s}}_c/\partial\widetilde{\mathbf{s}}_c$ is the PDE-filter derivative detailed in [57], and $\partial\bar{\mathbf{s}}/\partial\bar{\mathbf{s}}_c$ interpolates from cell centers back to mesh nodes.

Here it is noted that, for each optimization case that is considered for the current work, the calculated sensitivities are checked against a first order finite difference calculation. In the worst case, the difference between the calculated sensitivity and the finite difference computation was well below 0.1% since the utilized discrete adjoint method always yields exact and consistent sensitivities.

## 4. Numerical setup

Here, the numerical setup used for the optimization of transient vibroacoustic filter designs is presented. The section also introduces the objective function along with the computational domain and material properties that are used throughout the work. The goal of the optimization is to design a structure inside an acoustic channel which acts as, at a certain frequency range, a wave stopper while also allowing the incoming wave to pass at a different predefined frequency set which will be achieved with the internal reflections and the underlying acoustic-structural interactions. In order to define the objective function which is used for

<!-- PDF page 9 -->

the optimization of acoustic filter designs for a wide frequency range, a measure of transmission is needed to be defined. To achieve this, after the transient response of the state variables are obtained from the solution of the transient coupled vibroacoustic system, the transmitted acoustic pressure signal is integrated at the outlet of the considered acoustic duct as

<a id="eq-49"></a>

$$
\widehat{p}(t)=\int_{\Gamma_{out}}p(t)\,d\Gamma \tag{49}
$$

the frequency response of the transmitted acoustic pressure is found by the discrete Fourier transform

<a id="eq-50"></a>

$$
\widehat{p}(f)=\operatorname{FFT}(\widehat{p}(t)) \tag{50}
$$

and the transmission S(f) is then defined as

<a id="eq-51"></a>

$$
S(f)=\frac{\widehat{p}(f)}{\widehat{p}_0(f)} \tag{51}
$$

where the subscript 0 denotes the transmitted acoustic pressure when there is no structure in the acoustic duct which is calculated the same way as ̂ p(f). As it can be seen from the above Eq. (51), when the value of S(f) is unity for a particular frequency a full transmission is achieved. Meaning that the recorded amplitude of the transmitted acoustic pressure for an empty acoustic duct at a certain frequency is equal to that of an acoustic duct with a vibrating structure present in it. Moreover when the value of S(f) is zero for a certain frequency, there is no transmission realized compared to the transmission of the empty duct. In other words, the vibrating structure does not transmit acoustic pressure towards to outlet of the duct.

In order to reflect the so-called pass-band and stop-band regions in the considered frequency span, two objective functions are considered which are written as

<a id="eq-52"></a>

$$
\Phi_1\!\left(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}}))\right)=\sum_{f=n_1}^{n_2}\frac{(S(f)-a)^2}{a^2},\qquad a=1 \tag{52}
$$

<a id="eq-53"></a>

$$
\Phi_2\!\left(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}}))\right)=\sum_{f=n_3}^{n_4}\frac{(S(f)-b)^2}{b^2} \tag{53}
$$

where the minimization of the function Φ1 fits the frequency response of the transmitted acoustic pressure to that of an empty acoustic duct hence realizes a full transmission in a frequency window defined between n1 and n2. The minimization of Φ2 on the other hand lowers the amplitude of the frequency response of the transmitted acoustic pressure compared to the response of an empty acoustic duct in a frequency range defined between n3 and n4. The order of magnitude difference between the transmitted acoustic pressure in the stop-band region and the empty acoustic duct is defined by the parameter b in Eq. (53). The study on the selection of b in order to realize an effective zero-transmission and its effect on the overall optimization performance are given in Section 5.1. Remark that the proposed functions in Eqs. (52) and (53) were chosen from several candidate functions as they performed best for the filter design problem. Among the tested candidate formulations, we also tried to minimize the unweighted difference between S(f) and a together with the difference between S(f) and b and an even simpler approach in which S(f) was maximized for the pass bands and minimized for the stop bands. However, none of these approaches worked as well as the weighted least squares type formulation. Moreover, it should be emphasized that solving dynamic problems over wide frequency ranges presents a truly challenging design problem with many local minima due to the non-convexity. Hence, there may be better problem formulations available, but out of all the formulation investigated, the one presented next is the one that proved best for the filter design problems.

In order to realize an optimization problem where multiple objective functions are equally minimized, the problem can be formally written in a so-called min-max formulation as

<a id="eq-54"></a>

$$
\begin{aligned}\min_{\mathbf{s}}\quad&\max\left\{\Phi_1(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}}))),\Phi_2(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}})))\right\}\\\text{s.t.}\quad&\mathbf{R}^n(\bar{\mathbf{s}},\mathbf{U}^n(\bar{\mathbf{s}}))=0,\quad n=0,1,\ldots,N\\&0\le\mathbf{s}\le1\end{aligned} \tag{54}
$$

However, since the min-max formulation is not differentiable an additional variable is introduced and the optimization problem is cast as a bound formulation as

$$
\begin{aligned}
\min_{\mathbf{s},z}\quad z \tag{55} \\
\text{s.t.}\quad\mathbf{R}^n(\bar{\mathbf{s}},\mathbf{U}^n(\bar{\mathbf{s}}))=0,\quad n=0,1,\ldots,N \tag{56} \\
\Phi_1(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}})))<z \tag{57} \\
\Phi_2(\mathbf{U}_f(\mathbf{U}(\bar{\mathbf{s}})))<z \tag{58} \\
0\le\mathbf{s}\le1 \tag{59}
\end{aligned}
$$

where the additional variable z is the upper bound for the optimization. The formulation realizes the minimization of the upper bound z where the objective functions Φ1 and Φ2 are defined as constraints in the optimization problem, hence effectively minimizing both Φ1 and Φ2. In what follows, the functions Φ1 and Φ2 are therefore referred to as constraint functions. Note that no external box constraints are imposed on z as this is included using the internal bound variable in MMA.

<!-- PDF page 10 -->

![Figure 3](golden-paper-assets/figure-03.png)

*Fig. 3. Schematic illustration of the optimization case for the design of acoustic filters showing the boundary conditions of the optimization problem. Gray color shows the design domain. Incoming acoustic white noise and an example desired filter shape at the outlet are also shown.*

![Figure 4](golden-paper-assets/figure-04.png)

*Fig. 4. Frequency content of the incoming white noise for the optimization of acoustic filters.*

**Table 1. Material properties considered for the structure.**

| $E$ [Pa] | $\nu$ | $\rho_s$ [kg/m³] |
|---:|---:|---:|
| $50\times10^6$ | 0.4 | 1000 |

A schematic illustration of the numerical setup for the optimization of acoustic filter designs is given in Fig. 3. As it can be seen from the figure, both top and bottom boundaries of the acoustic duct are set as hard-wall condition for the acoustic pressure while the structure is considered to be clamped. For the acoustic domain, both right and leftmost boundaries are treated as absorbing conditions. The incoming acoustic plane wave from the leftmost boundary is also shown in the figure. In order to excite broad frequency range in the coupled system, the incoming plane wave is realized as a white noise with random acoustic pressure values between -1 Pa and 1 Pa. Moreover, the figure also illustrates an example filter shape at the rightmost boundary (at the acoustic duct outlet) where the acoustic transmission of the design S(f) is calculated in frequency domain and a certain filter shape is applied to it. Here it is noted that the presented filter shape in the schematic illustration is given as an example. The work considers various different filters for the optimization which are presented in the following sections.

The overall calculation time for the considered cases throughout the work is chosen as 0.02 s as it can also be seen from the incoming transient acoustic plane wave plot given in Fig. 3. In order to adequately resolve the coupled vibroacoustic problem in time, the time step size is set to Δt= $2 \times 10^{-5}$ s. The frequency content of the white noise applied at the inlet of the acoustic duct to excite broad frequency range in the system is given in Fig. 4. As it can be seen from the figure, the incoming acoustic wave has an approximately constant energy content across all the frequencies present in the signal.

For the optimization, the acoustic domain is taken as air while the structure is considered to be a rubber-like material. The material properties used for the structure and acoustic domains are listed in the Tables 1 and 2, respectively.

As described in Section 2.2, the work also considers Rayleigh damping for structural damping. Damping for structure is utilized both for the added stabilization of modeling and the subsequent optimization and to reflect the loss mechanism of the real world.

<!-- PDF page 11 -->

**Table 2. Material properties considered for the acoustic domain.**

| $c_a$ [m/s] | $\rho_a$ [kg/m³] |
|---:|---:|
| 343 | 1.21 |

The Rayleigh parameters αd and βd are calculated according to [62] as

$$
\begin{aligned}
\alpha_d=2\zeta\frac{\omega_1\omega_2}{\omega_1+\omega_2} \tag{60} \\
\beta_d=2\zeta\frac{1}{\omega_1+\omega_2} \tag{61}
\end{aligned}
$$

here ζ is called the damping ratio and is taken to be ζ= 0.1. Moreover, the work assumes that the two natural frequencies ω1 and ω2 are ω1 = 1600 2πrad∕s and ω2 = 2200 2πrad∕s. Throughout the work, the computational domain given in Fig. 3 is meshed with structured quad elements in which each element has an edge length of $2 \times 10^{-3}$ m, i.e. a total of 12 500 elements. For optimization, the filter radius r is set to $8 \times 10^{-3}$ m.

Throughout the work, the utilized MMA algorithm for solving the optimization problem presented in Eqs. (55) to (59) uses the asymptote parameters of 0.5, 0.7 and 1.2 which are used for controlling the initial adaptation, decrease and increase of the asymptotes, respectively. The penalty parameter which is used for constraints in MMA algorithm is chosen as 1000. For all of the optimization cases that are considered, the work does not consider a specific stopping criteria. Instead, the optimization is simply run for a fixed number of iterations. This choice is made due to the following reasons. First, since the performance of the design is extremely sensitive to the small design perturbations, as is the case for many dynamic optimization problems [16], small oscillations in the constraint functions occur throughout the optimization process. Secondly, as we use the MMA z for the bound variable, we do not have explicit control on the move limits for the objective variable, which can also lead to oscillatory behavior. Hence, the presented optimization results are obtained using a fixed number of iterations. Moreover, the considered optimization problem does not include a volume constraint on the structure. Since, having only a slab of solid material in the design domain does not form a trivial answer to the optimization problem.

## 5. Numerical examples

To demonstrate the capabilities and limitations of the proposed optimization framework, a series of numerical experiments are conducted in order to examine the performance of the chosen constraint function, the dependence on initial configurations, its application to filter design as well as a validation study using commercial finite software. Remark that the final validation of the proposed time-domain cut element optimization framework and its optimization result is obtained by comparing the response to the result of a COMSOL analysis using a fully coupled vibroacoustic time harmonic (steady state) simulation.

### 5.1. Constraint function study

The considered constraint function, i.e. Eqs. (52) and (53), is first studied in order to illuminate the influence of the input parameters. As it is introduced in the previous Section 4, the pass-band and stop-band regions for the constraint functions are controlled with the parameters a and b, respectively. Minimizing the constraint function Φ1 with having the a parameter as unity fits the calculated transmission value S(f) of the structure inside an acoustic duct to 1, effectively realizing full transmission in the considered frequency range. Ideally, the parameter b on the other hand needs to be set as 0 in order to realize zero transmission S(f) = 0 in the frequency range defining the stop-band region of the filter that is considered for the optimization. However, as it can be seen from the Eqs. (52) and (53), an inverse weighting is utilized in the constraint functions. Throughout our numerical experiments it has been found that the utilized inverse weighting provided designs with superior performances compared to the designs obtained using constraint functions without any weighting. Hence, a small positive number is used for the b parameter in order to avoid division by zero in the constraint function Φ2. This section investigates the effect of the b parameter on the optimization and the final performance of the optimized design.

Moreover, the section considers a low-pass acoustic filter design for the study. The constraint function Φ1 defining the pass-band region operates on the frequencies between 1000 Hz ≤f≤2500 Hz. For the stop-band, the constraint function Φ2 on the other hand is chosen to be active on the frequencies between 2500 Hz < f≤4000 Hz. Three different b parameters are selected to carry out the study which are $1 \times 10^{-2}$, $1 \times 10^{-3}$ and $1 \times 10^{-4}$. Remark that several additional values of b were considered in our numerical experiments, but were discarded as they led to poor numerical performance. Further discussion of this follows at the end of this section.

Fig. 5(a) shows the initial configuration that is used for the optimization. The physical design variables s that specifies the initial design is obtained first by calculating the following expression

<a id="eq-62"></a>

$$
s_v=\cos\left(\frac{r_1\pi\mathbf{x}}{l_x}\right)\cos\left(\frac{r_2\pi\mathbf{y}}{l_y}\right)+0.1 \tag{62}
$$

<!-- PDF page 12 -->

![Figure 5](golden-paper-assets/figure-05.png)

*Fig. 5. Optimization for an acoustic low-pass filter design. (a) initial configuration used in the study. (b) Figure showing the low-pass filters for the study where three different levels of transmission S(f) values are considered for the stop-band of the filters which are $1 \times 10^{-2}$, $1 \times 10^{-3}$ and $1 \times 10^{-4}$ (shown with black solid lines). Figure also shows the transmission S(f) of the initial configuration with a dashed gray line.*

**Table 3. Calculated constraint values $\Phi_1$ and $\Phi_2$ for various $b$ parameters.**

| $b$ | $\Phi_1$ | $\Phi_2$ |
|---:|---:|---:|
| $1\times10^{-2}$ | 0.0455159 | 576902 |
| $1\times10^{-3}$ | 0.0455159 | $5.8754\times10^7$ |
| $1\times10^{-4}$ | 0.0455159 | $5.8860\times10^9$ |

where x and y are the nodal coordinates of the mesh in the design domain, lx and ly is taken to be 0.1 and r1 and r2 is chosen as 7. After calculating sv, the mathematical design variables s are obtained with the following rule as

<a id="eq-63"></a>

$$
s_i=\begin{cases}0,&s_{v,i}\ge0.01,\\1,&s_{v,i}<0.01.\end{cases} \tag{63}
$$

With applying the design parameterization described in Section 3.1, the physical design variables s containing the initial design is obtained. Physically, the initial design given in Fig. 5(a) can be considered as an infinitely long acoustic duct in the out of plane direction where the two dimensional approximation realizes its cross section view from the mid-section of the channel.

Fig. 5(b) shows the desired acoustic low-pass filters with the three different levels of stop-band in which the transmission S(f) is to be fitted to the values of $1 \times 10^{-2}$, $1 \times 10^{-3}$ and $1 \times 10^{-4}$. As it can be seen from the figure, the initial configuration nearly has a full transmission across the considered frequency range of 1000 Hz to 4000 Hz. Table 3 lists the calculated constraint values for the initial configuration for each optimization case. As expected, since the same initial design is utilized and the parameter a is set to a= 1 for each case, the values of Φ1 are the same. However, the values of Φ2 increase two orders of magnitude for each order of magnitude decrease in the b parameter. Since, the constraint values Φ1 and Φ2 are inversely weighted with the square of a and b, respectively.

The result of the comparative study is given Fig. 6. As it can be seen from the Figs. 6(a) to 6(c), although the optimized designs have highly complex topologies, a similar trend can be seen in how the structures clustered inside the design domain for each case. Also, due to the lack of feature size control in the optimization, minimum feature size is bound by the element size used in the computational mesh which can be seen in the thin connections and small island structures present in the optimized designs. Interestingly, the optimized low pass filter designs share little resemblance to classical low-pass filters, in which the channel width is extended for a short duration. Based on our numerical experiments, we conjecture that the performance of the optimized design relies on several, individual resonators and reflectors. However, visualizing this phenomena using the transient, multi-frequency input signal is quite difficult, if not impossible, and the reader is referred to the post-validation in Section 5.4 where a low pass filter is analyzed in COMSOL using a time-harmonic analysis. It is worth noting that the optimized filters are tailored for plane

<!-- PDF page 13 -->

![Figure 6](golden-paper-assets/figure-06.png)

*Fig. 6. Optimization for an acoustic low-pass filter design. (a) optimized design for b= $1 \times 10^{-2}$. (b) optimized design for b= $1 \times 10^{-3}$. (c) optimized design for b= $1 \times 10^{-4}$. (d) transmission of the optimized designs in the considered frequency range. Black line is the desired low-pass filter, blue line is the response of the design shown in Fig. 6(a), orange line is the response of the design shown in Fig. 6(b), green line is the response of the design shown in Fig. 6(c). (e) averaged SPL response of the designs calculated at the output, gray line is the empty acoustic duct, blue line is the response of the design shown in Fig. 6(a), orange line is the response of the design shown in Fig. 6(b), green line is the response of the design shown in Fig. 6(c).*

wave excitation using the fundamental mode. Hence, they are not expected to work for any other type of incident wave or mode in accordance to the findings of [17].

The frequency responses of the optimized designs can be seen in Fig. 6(d) where the calculated transmission S(f) at the outlet is plotted for all optimized designs. It can be seen from the figure that the calculated transmission S(f) for each case respects the desired low-pass filter shape for the considered frequency range. However, as the parameter b is decreased for having a lower transmitted acoustic pressure response at the stop-band region, the resulted optimized designs’ performances for the transition between the pass-band and stop-band regions also decrease. This effect can also be seen in the presented constraint function values Φ1 and Φ2 for each optimized design in Figs. 6(a) to 6(c) where the constraint values increased as the transmission value is decreased in the stop-band of the acoustic filter. As it can also be seen from the presented constraint values, optimized designs’ Φ1 values actually end up at higher values compared to the initial configuration which is due to the transition between the pass-band and stop-band regions for the considered low-pass filter.

<!-- PDF page 14 -->

![Figure 7](golden-paper-assets/figure-07.png)

*Fig. 7. Optimization for an acoustic low-pass filter design. (a) optimized design for b= $1 \times 10^{-5}$. (b) transmission of the optimized design in the considered frequency range. Black line is the desired low-pass filter, blue line is the response of the optimized design.*

Overall, compared to the initial transmission given in Fig. 5(b), it can be seen from Fig. 6(d) that the developed optimization framework can successfully tailor the frequency content obtained from the transient response of the coupled vibroacoustic system, effectively designing acoustic filters. Moreover, Fig. 6(e) compares the averaged sound pressure level (SPL) values at the outlet of the acoustic duct for the considered frequency range. Compared to the SPL response of the empty acoustic duct, the considered low-pass filter shape can also be seen from the SPL values of each optimized design. Here, the optimized designs have nearly the same averaged SPL response with the empty acoustic duct from 1000 Hz to approximately 2250 Hz and after a sharp transition into the stop-band region, transmission S(f) values of $1 \times 10^{-2}$, $1 \times 10^{-3}$ and $1 \times 10^{-4}$ roughly corresponds to the averaged SPL values of 20 dB, 0 dB and -20 dB for the stop-band region of the low-pass acoustic filter, respectively. To demonstrate what happens if choosing too small a value of b, the same optimization setup is again considered with a low-pass filter design where the b parameter is lowered to b= $1 \times 10^{-5}$. Fig. 7(a) shows the optimized design where a similar trend is seen compared to the design presented in Fig. 6(c). However, as it can be seen from the performance of the acoustic filter given in Fig. 7(b), optimization resulted in a low quality local-minimum. Meaning that the optimized design fails to lower the transmission to S(f) = $1 \times 10^{-5}$ for the stop-band region of the considered low-pass filter. If, on the other hand, b is increased the drop in SPL is too small to yield a functioning filter. Thus, considering the SPL values given in Fig. 6(e) and the corresponding performances for each b parameter, b= $1 \times 10^{-3}$ is deemed the most effective for optimizing acoustic filters within the developed framework and will be used for the remaining of the work.

Finally, in order to illustrate the evolution of the constraint functions’ (Φ1 and Φ2) throughout optimization iterations, Fig. 8(b), shows the constraint function history for the optimized design given in Fig. 6(a). As it can be seen from the figure, the optimization quickly gets the Φ1 to a stable level, which governs the performance in the pass-band, and realizes a significant reduction in the constraint function Φ2 and thus achieving a good performance in the stop-band. It is important to stress, that although the constraint function history does contain noticeable oscillations, the design itself converges to the final topology within approximately 300 iterations. The oscillations are therefore a consequence of the fact that even small changes in design can lead to significant changes in the constraint functions , which as already stated is a known issue with dynamics and structural optimization. To show that the constraint oscillations are not a consequence of an ill-defined level set function, i.e. a flat level set function for which even very small design variable changes will lead to large changes in the physical design, Fig. 8(a) show the final level set function. From this figure it is observed that the optimized level set function is free of regions with flat slopes which shows that the employed filtering is sufficient for the purpose of the presented work. Note that the flat regions at the inlet and outlet are by construction as these regions are non-designable. Furthermore, to complement the previously given evolution of the constraint functions, Fig. 9 shows the design evolution throughout the optimization for a selection of iterations for the same case. As it can be seen from the figure, the design converges to an approximate final topology rather quickly, and from then on the optimization mainly changes the boundaries of the structure. Finally, remark that all of the presented optimization cases have very similar objective/constraint function behaviors and for the sake of brevity, the constraint function history and design evolution graphs are only presented for one example.

<!-- PDF page 15 -->

![Figure 8](golden-paper-assets/figure-08.png)

*Fig. 8. (a) Optimized level set function s of the design given in Fig. 6(a), also showing the zero iso-level of the level set function from which the optimized design is obtained. (b) Constraint function history for the optimized design given in Fig. 6(a) run for 400 iterations.*

### 5.2. Initial guess study

This section carries out an initial guess study for the optimization of acoustic filters. Instead of the low-pass filter design that is presented in the previous Section 5.1, the section considers the optimization of high-pass acoustic filters. For the optimization, the constraint function Φ1 defining the pass-band of the high-pass filter operates on the frequencies between 2500 Hz ≤f≤4000 Hz. Whereas, Φ2 for the stop-band region of the high-pass filter is active between 1000 Hz ≤f< 2500 Hz. The desired high-pass filter is given in Fig. 10(d) in which the calculated transmission S(f) of the designs are to be lowered to a value S(f) = $1 \times 10^{-3}$ in the stop-band region of the filter.

For the current study, three different initial configurations will be considered. The first initial design is the same that was used in the previous section. The other two initial guesses are obtained with decreasing the r1 and r2 parameters given in Eq. (62) from 7 to r1, r2 = 6 and r1, r2 = 5, respectively. Decreasing the parameters r1 and r2 reduce the total number of circle structures in the initial configuration while making their size bigger.

Figs. 10(a) to 10(c) presents the initial configurations that are used for the optimization of acoustic high-pass filters. As it is seen from the figures, initial structures have sparsely clustered features in order to allow a high transmission across the frequencies that are considered in the high-pass filter given in Fig. 10(d). Initial structures’ features become larger from Figs. 10(a) to 10(c). The figures also presents the constraint values Φ1 and Φ2 calculated with the initial designs where similar results are obtained. This points out similar transmission responses between 1000 Hz and 4000 Hz for the initial configurations.

Fig. 10(e) presents the transmission S(f) responses calculated at the outlet of the acoustic duct over the considered frequency range. Here, the initial configurations given in Figs. 10(a) and 10(b) resulted in transmission responses that closely follow each other between 1000 Hz and 4000 Hz where the calculated transmission S(f) values are clustered around unity. Meaning that the initial configuration for the first two cases allows for nearly full transmission in the frequency range that is considered for the optimization. Likewise, the initial design given in Fig. 10(c) also allows for frequencies between 1000 Hz and 2800 Hz to pass. However, after approximately around 2800 Hz, the last initial design has a lowered transmission response as it can also be seen from Fig. 10(e) which corresponds to the pass-band region of the considered high-pass acoustic filter.

<!-- PDF page 16 -->

![Figure 9](golden-paper-assets/figure-09.png)

*Fig. 9. Presentation of the design evolution throughout the optimization for the case given in Fig. 6(a).*

The results of the optimization for the initial guess study are presented in Fig. 11. Moreover, the optimized designs can be seen in Figs. 11(a) to 11(c) which are the optimized results of the initial designs given in Figs. 10(a) to 10(c), respectively. Here, the designs given in Figs. 11(a) and 11(b) have a similar structure layout where the optimized structures in the design domain separated the domain roughly into three acoustic partitions. Again, it is interesting to note that the optimized high pass filters do not resemble classical designs with short side branches to the main channel. Optimized design given in Fig. 11(c) resulted in the largest features compared to the first two cases. Also, from the investigation of the constraint values Φ1 and Φ2 given in Figs. 11(a) to 11(c), it is seen that the first two optimized designs successfully captured the high-pass filter response whereas the last optimization failed in the pass-band region of the considered high-pass filter shape. This is also visually shown in Fig. 11(d) where the calculated transmission S(f) for each optimized design is plotted over the considered frequency range. It can be seen in the figure that the

<!-- PDF page 17 -->

![Figure 10](golden-paper-assets/figure-10.png)

*Fig. 10. Optimization for an acoustic high-pass filter design. Figures (a), (b) and (c) are the initial guess designs utilized for the study. (d) Figure showing the desired high-pass filter. (e) transmission of the initial guess designs in the considered frequency range, blue line is the response of the design shown in Fig. 10(a), orange line is the response of the design shown in Fig. 10(b), green line is the response of the design shown in Fig. 10(c).*

optimized designs given in Figs. 11(a) and 11(b) perform successfully as acoustic high-pass filters with lowering the transmission to S(f) = $1 \times 10^{-3}$ in the stop-band and, after a sharp transition around 2500 Hz, realizing nearly full transmission in the pass-band region of the high-pass filter.

Furthermore, Fig. 11(d) also shows the response of the design given in Fig. 11(c) in which the design successfully realizes the stop-band of the high-pass filter. However, after the transition into the pass-band, the design’s performance deteriorates as the calculated transmission S(f) lowers in the pass-band. Fig. 11(e) compares the designs’ averaged SPL values at the outlet of the acoustic duct for the considered frequency range against the response of an empty acoustic channel. Here for the optimized designs in Figs. 11(a) and 11(b), the SPL values in the stop-band are clustered around 0 dB and goes up to around 60 dB in the pass-band with closely following the response of an empty acoustic channel. The failure of the design given in Fig. 11(c) can also be seen from the figure where the SPL values diverge from the response of an empty acoustic channel in the pass-band of the high-pass filter.

Overall, the developed transient optimization framework for coupled vibroacoustic problems is successfully applied for the design of acoustic high-pass filters. Initial configuration given in Fig. 10(c) resulted in a failed design, which is most likely due to the fact

<!-- PDF page 18 -->

![Figure 11](golden-paper-assets/figure-11.png)

*Fig. 11. Optimization for an acoustic high-pass filter design. (a) optimized design for the initial guess shown in Fig. 10(a). (b) optimized design for the initial guess shown in Fig. 10(b). (c) optimized design for the initial guess shown in Fig. 10(c). (d) transmission of the optimized designs in the considered frequency range. Black line is the desired high-pass filter, blue line is the response of the design in shown Fig. 11(a), orange line is the response of the design shown in Fig. 11(b), green line is the response of the design shown in Fig. 11(c). (e) averaged SPL response of the designs calculated at the output, gray line is the empty acoustic duct, blue line is the response of the design shown in Fig. 11(a), orange line is the response of the design shown in Fig. 11(b), green line is the response of the design shown in Fig. 11(c).*

that this initial guess does not provide full transmission for all considered frequencies. In other words, as the objective is measured on the output, the sensitivity will be zero if no signal is received at this port. This observation is further strengthen as the initial configurations that allow nearly full transmission (Figs. 10(a) and 10(b)) across all frequencies considered by the optimization, resulted in efficient acoustic high-pass filters. The same behavior is also seen in other works on filter design, see e.g. [63] on the design of microwave waveguide filters.

### 5.3. Band-pass and band-stop acoustic filter designs

Having validated the proposed constraint function formulation, this section concerns the design of specific filter characteristics. The section firstly carries out the optimization for the band-pass filter where the constraint function Φ1, defining the pass-band

<!-- PDF page 19 -->

![Figure 12](golden-paper-assets/figure-12.png)

*Fig. 12. Optimization for an acoustic band-pass filter design. (a) optimized design. (b) transmission of the optimized design in the considered frequency range. Black line is the desired band-pass filter, blue line is the response of the optimized design, gray dashed line is the response of the initial guess design. (c) averaged SPL response of the optimized design calculated at the output, gray line is the empty acoustic duct, blue line is the response of the optimized design.*

region of the band-pass filter, operates on the frequencies between 2500 Hz ≤f≤4000 Hz. Stop-band regions defined by the constraint function Φ2 are considered in the frequency window of 1000 Hz ≤f< 2500 Hz and 4000 Hz < f≤5500 Hz. Initial structure considered for the optimization is the same as in Fig. 10(a) where the initial constraint values are listed here as Φ1 = 0.0154458, Φ2 = $1.15859 \times 10^{8}$. Remark that the two examples presented in this section are run for 800 iterations.

Fig. 12(a) shows the optimized band-pass filter while the transmissions S(f) of the optimized design and the initial configuration are given in Fig. 12(b). As it can be seen from Fig. 12(b), the initial configuration has nearly full transmission across the frequencies 1000 Hz to 5500 Hz. The optimized design on the other hand closely follows the desired band-pass filter shape. Interestingly, the optimized band-pass filter (Fig. 12(a)) has a similar structural arrangement compared to the high-pass filter in Fig. 11(a) and in which both designs have similar transmission responses from 1000 Hz to 4000 Hz for a high-pass filter shape. The current design further tailors the frequency response of the coupled system to realize an additional stop-band from 4000 Hz to 5500 Hz. Also, from the optimized design’s constraint values given in Fig. 12(a) and the visual inspection of the transmission response in Fig. 12(b), it can be said that the performance of the pass-band region is slightly lowered compared to the designs presented in previous sections. This is mainly due to the added complexity of the optimization where an additional stop-band is considered to realize a band-pass filter.

Fig. 12(c) shows the calculated averaged SPL response at the outlet of the acoustic channel, comparing the optimized design and the empty acoustic channel. It can be seen from the figure that the optimized design’s calculated SPL response at the outlet is clustered around 0 dB for the stop-band regions of the band-pass filter. At the pass-band, the design’s SPL response closely follow the SPL response of an empty acoustic channel. Overall, the developed transient optimization framework is shown to successfully tailor the frequency response of the coupled vibroacoustic system with designing an efficient acoustic band-pass filter.

The framework is lastly applied for the design of a band-stop filter. The constraint function Φ1 for the pass-band regions of the considered band-stop filter operates on the frequency window of 1000 Hz ≤f< 2500 Hz and 4000 Hz < f≤5500 Hz. For the stop-band region, the constraint function Φ2 is active between the frequencies of 2500 Hz ≤f≤4000 Hz. Again, the

<!-- PDF page 20 -->

![Figure 13](golden-paper-assets/figure-13.png)

*Fig. 13. Optimization for an acoustic band-stop filter design. (a) optimized design. (b) transmission of the optimized design in the considered frequency range. Black line is the desired band-pass filter, blue line is the response of the optimized design, gray dashed line is the response of the initial guess design. (c) averaged SPL response of the optimized design calculated at the output, gray line is the empty acoustic duct, blue line is the response of the optimized design.*

same initial configuration as the previous band-pass filter optimization is utilized which resulted in initial constraint values of Φ1 = 0.0552177, Φ2 = $5.8754 \times 10^{7}$.

The optimized design is shown in Fig. 13(a). Moreover, the desired band-stop filter shape along with the transmissions S(f) of the optimized and initial designs are given in Fig. 13(b). When the optimized design is compared to the low-pass filter design given in Fig. 6(b), it can be said that the both designs have a similar performance between the frequencies from 1000 Hz to 4000 Hz with acting as an acoustic low-pass filter. Overall, it can be seen in Fig. 13(b) that the initial transmission S(f) is successfully tailored during the optimization in which the optimized design’s response closely follow the desired band-stop filter between the frequencies of 1000 Hz to 5500 Hz. The performance of the optimized acoustic band-stop filter is also seen from the averaged SPL values calculated at the outlet of the acoustic channel which is given in Fig. 13(c). The figure shows that pass-band regions attains the overall 60 dB SPL response while the stop-band region of the filter lowers the SPL response to approximately around 0 dB.

As was the case with the previous optimization results, the optimized band-pass and band-stop filters share little similarity to classical filter design, i.e. no clearly distinguishable Helmholtz resonators, narrow slits, channel extensions are seen.

### 5.4. Optimization and validation of an acoustic low-pass filter

This section concerns the optimization of an acoustic low-pass filter and aims to verify the performance of the optimized design using a commercial software. In order to achieve this, after the optimization is finished, the obtained design is extracted and both solid and acoustic regions are re-meshed using body-fitted, conforming meshes consisting of a total of 151 460 triangles with quadratic shape functions. This means that even the smallest islands seen in the optimized designs are meshed with several elements in order to ensure proper capturing of the physics. It should also be noted that, the optimized design is directly obtained from the physical design variable s and no smoothing operation has been applied on the interface before re-meshing. The widely used commercial software COMSOL Multiphysics [64] is used for the validation, in which the strongly coupled vibroacoustic

<!-- PDF page 21 -->

![Figure 14](golden-paper-assets/figure-14.png)

*Fig. 14. Optimization for an acoustic low-pass filter design. (a) optimized design. (b) transmission of the optimized design in the considered frequency range. Black line is the desired low-pass filter, blue line is the response of the optimized design, gray dashed line is the response of the initial guess design and the orange line is the COMSOL calculation done in frequency domain with body-fitted analysis.*

problem is solved on time-harmonic form. Thus, the displacement and acoustic pressure degrees of freedoms in the body-fitted mesh are only defined in their corresponding solid and acoustic domains and only share the coupled interface which is explicitly defined. That is, COMSOL employs no ersatz material model or special integration rules as is the case with the cut element method. Moreover, we should emphasize strongly that using time-harmonics, i.e. a steady-state formulation, to post evaluate the performance of the optimized designs which are obtained using a transient formulation, is a very hard test to pass. That is, the transient optimization problem implementation is likely to utilize any numerical and/or physical artifact in the underlying model in order to gain performance improvements. For the problems at hand, this includes differences in loading, exploitation of transients, constructive/destructive interference, dissipation in the time-stepping scheme, etc. None of these effects will be present in the time-harmonic formulation.

As it has been thoroughly introduced in previous sections, the transmission S(f) of the optimized design is calculated with applying the FFT operation on the transient response of the coupled system when the system is excited with the incoming white noise realized as pressure oscillations. In order to compare and validate the calculated transmission S(f) of the design against the body-fitted mesh analysis done in COMSOL, we utilize a time-harmonic frequency domain analysis to achieve the frequency-sweep where the system is excited with an incoming sinusoidal plane wave for each frequency. This means that the system is solved at discrete frequencies within the frequency range that is considered by the optimization to realize steady-state solutions at each frequency. The transmission S(f) is then calculated at each discrete frequency and compared against the one obtained with the developed transient framework. The rest of the boundary conditions considered in COMSOL are; both top and bottom boundaries of the acoustic channel are set as hard-wall condition for the acoustic pressure while the structure is considered to be clamped, the outlet of the acoustic channel is set as an absorbing boundary and the coupling boundary conditions are applied to both acoustic pressure and displacement variables at the coupled interface.

The acoustic low-pass filter is considered for the frequency range of 500 Hz ≤f≤3500 Hz where the pass-band acts on the frequencies 500 Hz ≤f≤2000 Hz and the stop-band is defined on the window 2000 Hz < f≤3500 Hz. The desired transmission for the stop-band is considered as S(f) = $1 \times 10^{-4}$ to be able to realize in average four orders of magnitude decrease in the amplitude of the transmitted acoustic pressure compared to that of an empty channel.

For optimization, the initial structure given in Fig. 5(a) is used which results in initial constraint values of Φ1 = 0.0385819, Φ2 = $5.8394 \times 10^{9}$. These values reflect nearly full transmission in the considered frequency range which can also be seen from Fig. 14(b) where the response of the initial guess design is plotted on the desired filter shape.

The result of the acoustic low-pass filter optimization is presented in Figs. 14 and 14(a) shows the optimized design. Similarly to the previous optimization studies, it can be seen that the optimized design resulted in significant topological changes compared to the initial configuration, i.e. several solid regions has merged and some has completely disappeared. Moreover, Fig. 14(b) presents the calculated transmission S(f) of the optimized design along with the transmission of the post-processed design obtained with frequency domain calculation using COMSOL. As it can be seen from the figure, the calculated transmission response obtained using

<!-- PDF page 22 -->

![Figure 15](golden-paper-assets/figure-15.png)

*Fig. 15. COMSOL calculation of time-harmonic frequency domain analysis of the optimized design for the acoustic low-pass filter showing the sound pressure level [dB] contours of the design. (a) The steady state result from the pass-band at f= 1100 Hz. (b) The steady state result from the stop-band at f= 2900 Hz.*

the transient framework with the cut element method and the body-fitted analysis done in frequency domain with COMSOL agrees well. For both analysis methods, the calculated transmission response closely follows the desired pass-band and stop-band regions. As expected, the transition from the pass-band into the stop-band (around 2000 Hz) causes a slight decrease in the performance of the pass-band region. However, the design successfully lowers the transmission to approximately around S(f) = $1 \times 10^{-4}$ at the stop-band. Despite the fact that the two analysis method cannot be directly compared, we here quantify the observed mismatch using an average error measure. That is, the absolute difference in the average transmission ⟨S(f)⟩ between the transient cut element analysis and the body-fitted mesh time-harmonic COMSOL analysis is computed in the pass-band and stop-band and denoted with (⋅) cut and (⋅) body, respectively.

$$
\left|\langle S(f)\rangle_{cut}-\langle S(f)\rangle_{body}\right|_{pass}=0.107
$$

$$
\left|\langle S(f)\rangle_{cut}-\langle S(f)\rangle_{body}\right|_{stop}=2.237\times10^{-4}
$$

where one should keep in mind that the pass-band has unity as perfect transmission whereas the stop-band should have a transmission of S(f) = $1 \times 10^{-4}$. Returning to the inspection of Fig. 14(b), we remark that even though the two analysis are performed with quite different modeling and analysis strategies, both methods leads to qualitatively similar transmission responses. To elaborate, the discrepancies in the responses of the transient and frequency domain methods can be explained from the fact that, the frequency domain analysis results in steady-state solutions at each discrete frequency whereas the developed framework utilizes a finite transient signal to calculate the transmission through the FFT operation. Since the transient system is excited with random pressure oscillations it would be computationally unreasonable to consider steady-state solutions as this would be too time consuming. The difference in modeling and analysis is therefore attributed the majority of the difference in response. Another factor that can cause discrepancies in the response when comparing two method is the meshing procedure in which when the design is extracted from the zero iso-level of s and meshed with unstructured meshes, a slight alteration of the interface is unavoidable. Overall, it can be said that, when analyzed with more commonly used methods such as time-harmonic frequency domain methods using body-fitted analysis, the optimized design also shows a good performance as an acoustic low-pass filter.

Figs. 15 and 16 present the corresponding SPL and displacement magnitude, respectively, obtained from the time-harmonic analysis of the optimized design using COMSOL. The figures consider two discrete frequencies f= 1100 Hz and f= 2900 Hz for pass-band and stop-band, respectively. Furthermore, Fig. 15(a) shows the SPL field of the optimized design where the pressure response is plotted at f= 1100 Hz (at the pass-band). As it can be seen from the figure, the design allows the incoming wave to pass resulting in approximately constant SPL response at around 110 dB. Corresponding displacement magnitude |u| [m] field is plotted in Fig. 16(a) in which complex deflections of the structure can be seen which means that the underlying acoustic-structural interactions are fully utilized to realize the desired transmission behavior of the design. On the other hand when the design is analyzed at f= 2900 Hz (at the stop-band), as seen from Fig. 15(b), the incoming wave is stopped and the design realizes SPL values of below 0 dB at the outlet of the acoustic channel. Interestingly, as it is seen from the corresponding displacement magnitude

<!-- PDF page 23 -->

![Figure 16](golden-paper-assets/figure-16.png)

*Fig. 16. COMSOL calculation of time-harmonic frequency domain analysis of the optimized design for the acoustic low-pass filter showing the displacement magnitude |u| [m] contours of the design. (a) The steady state result from the pass-band at f= 1100 Hz. (b) The steady state result from the stop-band at f= 2900 Hz.*

|u| [m] field in Fig. 16(b), the resulted deflections of the structure are at least one order of magnitude smaller than the solution presented at the pass-band. Meaning that the optimized design acts as a wave stopper primarily because of the internal reflections and the underlying acoustic-structural interactions play a smaller part.

Lastly, the optimized structure is further analyzed to assess the effects of small free features, i.e. island type inclusions, on the overall filter performance. In order to carry out the study, two isolated parts of the design are selected, where the first one is the smallest feature in the design and the other is a relatively big part of the overall design. Fig. 17(a) shows the selected regions marked with red and green, respectively. Each part is then removed from the analysis, one by one, and the two new designs are analyzed using a time-harmonic analysis in COMSOL for the full frequency range of interest, i.e. 500 Hz to 3500 Hz. The result of the comparative study is given in Fig. 17(b) in which it can be seen that excluding the smallest island part of the design does not change the performance of the design. This can be explained from the fact that within the frequency range of 500 Hz to 3500 Hz, the wavelength of air is significantly larger than the size of the considered part. Thus, it does not make any difference if the part is included in the design or not. Unsurprisingly, excluding the second part from the design results in significant performance reduction especially in the stop-band region since the internal reflections of the channel gets disturbed which results in lowered performance.

These extremely small island structures (the red colored part shown in Fig. 17(a)) also have been consistently present in the previous optimization cases. The lack of a volume constraint together with the highly non-convex nature of the optimization problem can be considered as the biggest factors leading to the existence of small features since the optimization becomes unbounded in the amount of material. Moreover, when the features become sufficiently small they become transparent to the filter performance, and hence, the sensitivities goes towards zero. This again means that the optimizer has not reason to remove or move the small features. It is important to note that these small features can also adversely affect the oscillatory behavior of the objective function history (see Fig. 8). Several approaches could be used to suppress the appearance of small features. For example, a volume constraint could alleviate the problem. However, in this case, one would need to tune the target volume fraction allowed to ensure that the volume constraint would not affect the filter performance. Another approach would be to impose a minimum length scale using e.g. geometric constraints [65] and/or the robust design approach [38,66]. However, since the current study is focused on the proposed transient optimization methodology and not on manufacturability, both of these suggestions are left as suggestions for future work.

## 6. Discussion and conclusion

The article presents the utilization of time-domain methods to realize wideband optimization in frequency domain. The developed framework carries out generalized shape optimization of transient vibroacoustic problems in which the optimization considers acoustic filter designs. In order to achieve this, the objective and constraint functions are defined in frequency domain where the FFT operation is utilized to obtain the frequency response from the transient response of the coupled system. Throughout the work,

<!-- PDF page 24 -->

![Figure 17](golden-paper-assets/figure-17.png)

*Fig. 17. (a) Figure shows the locations of the modifications to the design. The first modification is the red colored island is removed and the rest of the structure (including the green colored part) is analyzed. The second one is the green colored part is removed and the rest of the structure (including the red colored part) is analyzed. (b) Transmission of the structure in the considered frequency range where black line is the target low-pass filter, blue line is the response of the original design, red dashed line is the response of the first modified design and the green line is the response of the second modification. All calculations have been done using COMSOL in frequency domain with body-fitted analysis.*

the level set approach is utilized for the geometry description where its zero iso-level specifies the interface between acoustic and structural domains. An immersed boundary method, i.e. the cut element method, is employed for capturing the geometry which operates on a fixed background mesh. The method uses a special integration scheme to accurately resolve the interface between the two physics without the addition of extra degrees of freedom to the system. Hence, the employed cut element method is suitable to include into the existing parallel FEM frameworks with ease. Moreover, the work utilizes the discrete adjoint method for carrying out the sensitivity analysis in order to calculate the gradients of the constraint functions. The derivation of the sensitivity analysis is kept general to allow for the inclusion of different time integration schemes in which the handling of the FFT operation is explained to be able to define objective and constraint functions in frequency domain. Furthermore, a study for the utilized optimization formulation is carried out to assess the effect of the inverse weighting that is used in the constraint function where a design of acoustic low-pass filter is considered. It has been found that having b= $1 \times 10^{-3}$ for the constraint function Φ2 to realize the stop-band region of the considered filter resulted in a relatively sharp transition between the pass-band and the stop-band, realizing an effective filter performance. The selected b parameter also reduces the averaged SPL values at the outlet of the acoustic channel to approximately around 0 dB in the stop-band. An initial guess study is then carried out the determine the effect of different initial configurations to the end design considering the optimization of an acoustic high-pass filters. It has been illustrated that with using initial configurations that allow for nearly full transmission in the frequency range that is considered for the optimization, efficient acoustic high-pass designs are obtained. The outcomes from both the constraint functions and the initial guess studies are then utilized in optimization for more complex acoustic band-pass and band-stop filters. Here it is shown that the proposed optimization formulation is capable of producing filters with up to 60 dB difference between pass-bands and stop-bands. Furthermore, the result of the time-harmonic, steady-state COMSOL validation study demonstrate the applicability of the developed transient framework for broadband applications considering coupled vibroacoustic systems. Overall, the developed transient optimization framework is shown to successfully tailor the frequency response of the coupled vibroacoustic system for the design of acoustic band-pass and band-stop filters. The presented work will pave the way for the optimization of acoustic devices to realize efficient wideband operation.

The proposed optimization framework presents several interesting directions which should be investigated in greater detail. The choice of constraint functions, especially their scaling, should be further examined. Although the proposed constraint functions leads to working filters with controllable characteristics, it would be desirable to determine constraint functions that results in less oscillations over the duration of the optimization process. One could also consider continuation schemes to remedy this issue, e.g. schemes in which the design variable move limits are tightened as the optimization progresses. This would also allow to include a better stopping criteria based on the constraint function evolution and the physical design change. It would also be interesting

<!-- PDF page 25 -->

to expand the design parameterization to include manufacturing requirements. This includes imposition of minimum length scales using e.g. geometric constraints [65] or the robust design approach [38,66]. Moreover, extension to 3D and treatment of the freely flying structures must also be addressed before manufacturing and application to industrially relevant problems. The latter could be done through the inclusion of additional connectivity constraints such as non-zero structural eigenfrequency requirements, the virtual temperature method or similar approaches.

## Replication of results

For replicating the presented examples, all necessary information is given in the corresponding sections. Moreover, the code can be obtained from the authors upon reasonable request.

## CRediT authorship contribution statement

Cetin B. Dilgen: Writing - original draft, Writing - review & editing. Niels Aage: Supervision, Writing - review & editing.

## Declaration of competing interest

All authors declare that they have no conflicts of interest.

## Data availability

For replicating the presented examples, all necessary information is given in the corresponding sections. Moreover, the code can be obtained from the authors upon reasonable request.

## References

1. M.P. Bendsøe, O. Sigmund, Topology Optimization - Theory, Methods, and Applications, Springer Verlag, 2003.

2. N. Aage, E. Andreassen, B.S. Lazarov, O. Sigmund, Giga-voxel computational morphogenesis for structural design, Nature 550 (7674) (2017) 84-86, https://doi.org/10.1038/nature23911.

3. E.A. Kontoleontos, E.M. Papoutsis-Kiachagias, A.S. Zymaris, D.I. Papadimitriou, K.C. Giannakoglou, Adjoint-based constrained topology optimization for viscous flows, including heat transfer, Eng. Optim. 45 (8) (2013) 941-961, https://doi.org/10.1080/0305215X.2012.717074.

4. C.B. Dilgen, S.B. Dilgen, D.R. Fuhrman, O. Sigmund, B.S. Lazarov, Topology optimization of turbulent flows, Comput. Methods Appl. Mech. Engrg. 331 (2018) 363-393, https://doi.org/10.1016/j.cma.2017.11.029.

5. O. Sigmund, Design of multiphysics actuators using topology optimization - Part I, Comput. Methods Appl. Mech. Engrg. 190 (49-50) (2001) 6577-6604, https://doi.org/10.1016/s0045-7825(01)00251-1.

6. J. Alexandersen, O. Sigmund, N. Aage, Large scale three-dimensional topology optimisation of heat sinks cooled by natural convection, Int. J. Heat Mass Transfer 100 (2016) 876-891, https://doi.org/10.1016/j.ijheatmasstransfer.2016.05.013.

7. E.J. Haug, H.S. Arora, Design sensitivity analysis of elastic mechanical systems, Comput. Methods Appl. Mech. Engrg. 15 (1) (1978) 35-62.

8. P. Michaleris, D.A. Tortorelli, C.A. Vidal, Tangent operators and design sensitivity formulations for transient non-linear coupled problems with applications to elastoplasticity, Internat. J. Numer. Methods Engrg. 37 (14) (1994) 2471-99, 2471-2499, https://doi.org/10.1002/nme.1620371408.

9. C.B.W. Pedersen, Crashworthiness design of transient frame structures using topology optimization, Comput. Methods Appl. Mech. Engrg. 193 (6-8) (2004) 653-678, https://doi.org/10.1016/j.cma.2003.11.001.

10. Y. Li, K. Saitou, N. Kikuchi, Topology optimization of thermally actuated compliant mechanisms considering time-transient effect, Finite Elem. Anal. Des. 40 (11) (2004) 1317-1331, https://doi.org/10.1016/j.finel.2003.05.002.

11. S. Turteltaub, Optimal non-homogeneous composites for dynamic loading, Struct. Multidiscip. Optim. 30 (2) (2005) 101-112, https://doi.org/10.1007/s00158-004-0502-0.

12. P. Seyranian, E. Lund, N. Olhoff, Multiple eigenvalues in structural optimization problems, Struct. Optim. 8 (4) (1994) 207-227, https://doi.org/10.1007/BF01742705.

13. J.S. Jensen, O. Sigmund, Topology optimization for nano-photonics, Laser Photon. Rev. 5 (2) (2011) 308-321, https://doi.org/10.1002/lpor.201000014.

14. M.B. Dühring, J.S. Jensen, O. Sigmund, Acoustic design by topology optimization, J. Sound Vib. 317 (3-5) (2008) 557-575, https://doi.org/10.1016/j.jsv.2008.03.042.

15. J. Park, S. Wang, Noise reduction for compressors by modes control using topology optimization of eigenvalue, J. Sound Vib. 315 (4-5) (2008) 836-848, https://doi.org/10.1016/j.jsv.2008.01.064.

16. R.E. Christiansen, O. Sigmund, Experimental validation of systematically designed acoustic hyperbolic meta material slab exhibiting negative refraction, Appl. Phys. Lett. 109 (10) (2016) 101905, https://doi.org/10.1063/1.4962441.

17. A.H. Bokhari, A. Mousavi, B. Niu, E. Wadbro, Topology optimization of an acoustic diode? Struct. Multidiscip. Optim. 63 (6) (2021) 2739-2749, https://doi.org/10.1007/s00158-020-02832-9.

18. G.H. Yoon, J.S. Jensen, O. Sigmund, Topology optimization of acoustic-structure interaction problems using a mixed finite element formulation, Internat. J. Numer. Methods Engrg. 70 (9) (2007) 1049-1075, https://doi.org/10.1002/nme.1900.

19. W. Vicente, R. Picelli, R. Pavanello, Y. Xie, Topology optimization of frequency responses of fluid-structure interaction systems, Finite Elem. Anal. Des. 98 (2015) 1-13, https://doi.org/10.1016/j.finel.2015.01.009, URL http://linkinghub.elsevier.com/retrieve/pii/S0168874X15000104.

20. Y. Noguchi, T. Yamada, T. Yamamoto, K. Izui, S. Nishiwaki, Topological derivative for an acoustic-elastic coupled system based on two-phase material model, Mech. Eng. Lett. 2 (2016) 16-00246-16-00246, https://doi.org/10.1299/mel.16-00246, URL https://www.jstage.jst.go.jp/article/mel/2/0/2_16-00246/_article.

21. G. Fujii, M. Takahashi, Y. Akimoto, Acoustic cloak designed by topology optimization for acoustic-elastic coupled systems, Appl. Phys. Lett. 118 (10) (2021) 8-14, https://doi.org/10.1063/5.0040911.

22. J. Kook, J.H. Chang, A high-level programming language implementation of topology optimization applied to the acoustic-structure interaction problem, Struct. Multidiscip. Optim. 2001 (2001) (2021) https://doi.org/10.1007/s00158-021-03052-5.

23. D. Giannini, M. Schevenels, E.P. Reynders, Optimization of material thickness distribution in single and double partition panels for maximized sound insulation, Struct. Multidiscip. Optim. 66 (12) (2023) 1-18, https://doi.org/10.1007/s00158-023-03682-x.

24. L. Xu, W. Zhang, Z. Liu, X. Guo, Topology optimization of acoustic-mechanical structures for enhancing sound quality, Acta Mech. Solida Sin. 36 (5) (2023) 612-623, https://doi.org/10.1007/s10338-023-00408-w.

25. C. JOG, Topology design of structures subjected to periodic loading, J. Sound Vib. 253 (3) (2002) 687-709, https://doi.org/10.1006/jsvi.2001.4075, URL https://linkinghub.elsevier.com/retrieve/pii/S0022460X01940751.

26. T. Nomura, K. Sato, K. Taguchi, T. Kashiwa, S. Nishiwaki, Structural topology optimization for the design of broadband dielectric resonator antennas using the finite difference time domain technique, Internat. J. Numer. Methods Engrg. 71 (11) (2007) 1261-1296, https://doi.org/10.1002/nme.1974.

27. E. Hassan, E. Wadbro, M. Berggren, Topology optimization of metallic antennas, Ieee Trans. Antennas Propag. 62 (5) (2014) 6750741, https://doi.org/10.1109/tap.2014.2309112, 2488-2500.

28. E. Hassan, D. Noreland, R. Augustine, E. Wadbro, M. Berggren, Topology optimization of planar antennas for wideband near-field coupling, Ieee Trans. Antennas Propag. 63 (9) (2015) 7134720, https://doi.org/10.1109/TAP.2015.2449894, 4208-4213.

29. J. Hyun, H.A. Kim, Transient level-set topology optimization of a planar acoustic lens working with short-duration pulse, J. Acoust. Soc. Am. 149 (5) (2021) 3010-3026, https://doi.org/10.1121/10.0004819, URL https://asa.scitation.org/doi/10.1121/10.0004819.

30. S. Osher, J. Sethian, Fronts propagating with curvature-dependent speed - algorithms based on Hamilton-Jacobi formulations, J. Comput. Phys. 79 (1) (1988) 12-49, https://doi.org/10.1016/0021-9991(88)90002-2.

31. S. Wang, M.Y. Wang, Radial basis functions and level set method for structural topology optimization, Internat. J. Numer. Methods Engrg. 65 (12) (2006) 2060-2090, https://doi.org/10.1002/nme.1536.

32. G. Allaire, C. Dapogny, P. Frey, Shape optimization with a level set based mesh evolution method, Comput. Methods Appl. Mech. Engrg. 282 (2014) 22-53, https://doi.org/10.1016/j.cma.2014.08.028.

33. L. Shu, M. Yu Wang, Z. Ma, Level set based topology optimization of vibrating structures for coupled acoustic-structural dynamics, Comput. Struct. 132 (2014) 34-42, https://doi.org/10.1016/j.compstruc.2013.10.019.

34. H. Isakari, T. Kondo, T. Takahashi, T. Matsumoto, A level-set-based topology optimisation for acoustic-elastic coupled problems with a fast BEM-FEM solver, Comput. Methods Appl. Mech. Engrg. 315 (2017) 501-521, https://doi.org/10.1016/j.cma.2016.11.006.

35. J. Desai, A. Faure, G. Michailidis, G. Parry, R. Estevez, Topology optimization in acoustics and elasto-acoustics via a level-set method, J. Sound Vib. 420 (2018) 73-103, https://doi.org/10.1016/j.jsv.2018.01.032.

36. C.B. Dilgen, S.B. Dilgen, N. Aage, J.S. Jensen, Topology optimization of acoustic mechanical interaction problems: a comparative review, Struct. Multidiscip. Optim. 60 (2) (2019) 779-801, https://doi.org/10.1007/s00158-019-02236-4.

37. J.A. Sethian, A. Wiegmann, Structural boundary design via level set and immersed interface methods, J. Comput. Phys. 163 (2) (2000) 489-528, https://doi.org/10.1006/jcph.2000.6581.

38. C.S. Andreasen, M.O. Elingaard, N. Aage, Level set topology and shape optimization by density methods using cut elements with length scale control, Struct. Multidiscip. Optim. 62 (2) (2020) 685-707, https://doi.org/10.1007/s00158-020-02527-1, URL http://link.springer.com/10.1007/s00158-020-02527-1.

39. E. Burman, S. Claus, P. Hansbo, M.G. Larson, A. Massing, CutFEM: Discretizing geometry and partial differential equations, Internat. J. Numer. Methods Engrg. 104 (7) (2015) 472-501, https://doi.org/10.1002/nme.4823.

40. A. Düster, J. Parvizian, Z. Yang, E. Rank, The finite cell method for three-dimensional problems of solid mechanics, Comput. Methods Appl. Mech. Engrg. 197 (45-48) (2008) 3768-3782, https://doi.org/10.1016/j.cma.2008.02.036.

41. C. Daux, N. Moës, J. Dolbow, N. Sukumar, T. Belytschko, Arbitrary branched and intersecting cracks with the extended finite element method, Internat. J. Numer. Methods Engrg. 48 (12) (2000) 1741-1760, https://doi.org/10.1002/1097-0207(20000830)48:12<1741::AID-NME956>3.0.CO;2-L.

42. M.J. De Ruiter, F. Van Keulen, Topology optimization using a topology description function, Struct. Multidiscip. Optim. 26 (6) (2004) 406-416, https://doi.org/10.1007/s00158-003-0375-7.

43. S. Kreissl, K. Maute, Levelset based fluid topology optimization using the extended finite element method, Struct. Multidiscip. Optim. 46 (3) (2012) 311-326, https://doi.org/10.1007/s00158-012-0782-8.

44. K. Svanberg, The method of moving asymptotes-a new method for structural optimization, Internat. J. Numer. Methods Engrg. 24 (2) (1987) 359-373, doi: 10.1002/nme.1620240207, 10.1002/(ISSN)1097-0207.

45. N. Pollini, O. Lavan, O. Amir, Adjoint sensitivity analysis and optimization of hysteretic dynamic systems with nonlinear viscous dampers, Struct. Multidiscip. Optim. 57 (6) (2018) 2273-2289, https://doi.org/10.1007/s00158-017-1858-2, URL http://link.springer.com/10.1007/s00158-017-1858-2.

46. C.B. Dilgen, N. Aage, Generalized shape optimization of transient vibroacoustic problems using cut elements, Internat. J. Numer. Methods Engrg. 122 (6) (2021) 1578-1601, https://doi.org/10.1002/nme.6591, URL https://onlinelibrary.wiley.com/doi/10.1002/nme.6591.

47. S.B. Dilgen, J.S. Jensen, N. Aage, Shape optimization of the time-harmonic response of vibroacoustic devices using cut elements, Finite Elem. Anal. Des. 196 (May) (2021) 103608, https://doi.org/10.1016/j.finel.2021.103608, https://linkinghub.elsevier.com/retrieve/pii/S0168874X21000925.

48. O. Zienkiewicz, R. Taylor, The Finite Element Method, Butterworth Heinemann, 2000, p. 459 s.

49. N.M. Newmark, A method of computation for structural dynamics, J. Eng. Mech. Div. 85 (3) (1959) 67-94.

50. B.P. Jacob, N.F.F. Ebecken, An optimized implementation of the Newmark/Newton-Raphson algorithm for the time integration of non-linear problems, Commun. Numer. Methods. Eng. 10 (12) (1994) 983-992, https://doi.org/10.1002/cnm.1640101204.

51. S. Balay, S. Abhyankar, M.F. Adams, J. Brown, P. Brune, K. Buschelman, L. Dalcin, A. Dener, V. Eijkhout, W.D. Gropp, D. Kaushik, M.G. Knepley, D.A. May, L.C. McInnes, R.T. Mills, T. Munson, K. Rupp, P. Sanan, B.F. Smith, S. Zampini, H. Zhang, H. Zhang, PETSc web page, 2018, URL http://www.mcs.anl.gov/petsc.

52. S. Balay, S. Abhyankar, M.F. Adams, J. Brown, P. Brune, K. Buschelman, L. Dalcin, A. Dener, V. Eijkhout, W.D. Gropp, D. Kaushik, M.G. Knepley, D.A. May, L.C. McInnes, R.T. Mills, T. Munson, K. Rupp, P. Sanan, B.F. Smith, S. Zampini, H. Zhang, H. Zhang, PETSc Users Manual, Tech. Rep. ANL-95/11 - Revision 3.10, Argonne National Laboratory, 2018, URL http://www.mcs.anl.gov/petsc.

53. S. Balay, W.D. Gropp, L.C. McInnes, B.F. Smith, Efficient management of parallelism in object oriented numerical software libraries, in: E. Arge, A.M. Bruaset, H.P. Langtangen (Eds.), Modern Software Tools in Scientific Computing, Birkhäuser Press, 1997, pp. 163-202.

54. P.R. Amestoy, I.S. Duff, J. Koster, J.-Y. L’Excellent, A fully asynchronous multifrontal solver using distributed dynamic scheduling, SIAM J. Matrix Anal. Appl. 23 (1) (2001) 15-41.

55. P.R. Amestoy, A. Guermouche, J.-Y. L’Excellent, S. Pralet, Hybrid scheduling for the parallel solution of linear systems, Parallel Comput. 32 (2) (2006) 136-156.

56. K. Maute, P. Coffin, Level set topology optimization of cooling and heating devices using a simplified convection model, Struct. Multidiscip. Optim. 53 (5) (2016) 985-1003, https://doi.org/10.1007/s00158-015-1343-8.

57. B.S. Lazarov, O. Sigmund, Filters in topology optimization based on Helmholtz-type differential equations, Internat. J. Numer. Methods Engrg. 86 (6) (2011) 765-781, https://doi.org/10.1002/nme.3072.

58. M. Frigo, S.G. Johnson, FFTW: An adaptive software architecture for the FFT, in: Proc. 1998 IEEE Intl. Conf. Acoustics Speech and Signal Processing, Vol. 3, IEEE, 1998, pp. 1381-1384.

59. N. Aage, B.S. Lazarov, Parallel framework for topology optimization using the method of moving asymptotes, Struct. Multidiscip. Optim. 47 (4) (2013) 493-505, https://doi.org/10.1007/s00158-012-0869-2.

60. J. Dahl, J.S. Jensen, O. Sigmund, Topology optimization for transient wave propagation problems in one dimension, Struct. Multidiscip. Optim. 36 (6) (2008) 585-595, https://doi.org/10.1007/s00158-007-0192-5.

61. P. Zhou, Y. Peng, J. Du, Topology optimization of bi-material structures with frequency-domain objectives using time-domain simulation and sensitivity analysis, Struct. Multidiscip. Optim. 63 (2) (2021) 575-593, https://doi.org/10.1007/s00158-020-02814-x, URL http://link.springer.com/10.1007/s00158-020-02814-x.

62. A.M. Puthanpurayil, O. Lavan, A.J. Carr, R.P. Dhakal, Elemental damping formulation: an alternative modelling of inherent damping in nonlinear dynamic analysis, Bull. Earthq. Eng. 14 (8) (2016) 2405-2434, https://doi.org/10.1007/s10518-016-9904-9.

63. N. Aage, V. Egede Johansen, Topology optimization of microwave waveguide filters, Internat. J. Numer. Methods Engrg. 112 (3) (2017) 283-300, https://doi.org/10.1002/nme.5551, URL http://doi.wiley.com/10.1002/nme.5551.

64. COMSOL multiphysics reference manual, version 5.5, 2020, www.comsol.com.

65. M. Zhou, B.S. Lazarov, F. Wang, O. Sigmund, Minimum length scale in topology optimization by geometric constraints, Comput. Methods Appl. Mech. Engrg. 293 (2015) 266-282, https://doi.org/10.1016/j.cma.2015.05.003, http://linkinghub.elsevier.com/retrieve/pii/S0045782515001693.

66. F. Wang, B.S. Lazarov, O. Sigmund, On projection methods, convergence and robust formulations in topology optimization, Struct. Multidiscip. Optim. 43 (6) (2011) 767-784, https://doi.org/10.1007/s00158-010-0602-y.

## Appendix A. Highlight annotations in the supplied PDF

The supplied PDF highlights the following implementation-relevant points:

- Time-domain excitation can cover broad frequency ranges compactly, but a time-domain objective only controls spectral content indirectly; this paper instead defines the objective directly in frequency space after an FFT.
- Level-set geometry is attractive for strongly coupled multiphysics, while body-fitted remeshing is expensive in parallel and can introduce sensitivity noise. The paper therefore combines an explicit level set with an immersed cut-element method on a fixed mesh.
- The structural model is plane-stress linear elasticity and the acoustic model is the linear wave equation, both in the time domain.
- A single fictitious-domain scaling parameter for stiffness and density can create spurious modes. The authors report not observing that failure in their experiments, but explicitly identify the risk.
- Spatial discretization uses continuous Galerkin Q4 elements with special integration of cut volumes and the acoustic-structural interface.
- Because a discrete FFT is part of the objective, the paper uses a fully discrete adjoint. The authors warn that a semi-discrete adjoint can produce inconsistent sensitivities and even wrong gradient signs.
- Manufacturing constraints, 3D extension, and freely flying structures are future work rather than demonstrated capabilities.
