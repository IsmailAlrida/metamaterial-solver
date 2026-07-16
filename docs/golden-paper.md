

<!-- Start of picture text -->
aos<br>P OU aes<br>ELSEVIER<br><!-- End of picture text -->

aos P OU aes ELSEVIER 





<!-- Start of picture text -->
aT<br>a<br>t i.<br><!-- End of picture text -->



<!-- Start of picture text -->
&updatesupdates<br><!-- End of picture text -->

&updatesupdates 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

are considered in order to widen the zone of influence of optimization on the frequency response of the system. Examples of topology optimization in frequency domain can be found considering eigenvalue problems [12], optics [13], acoustics [14–17] and vibroacoustics [18–24] <mark>. Here it should also be noted that the difficulty in choosing the right objective function is much complicated, and often an ambiguous task, for dynamic problems</mark> [25]. Using <mark>a time-dependent problem formulation presents a promising alternative</mark> to address this issue since the optimization can be carried out using complex and compact signals that contains broad ranges of frequencies. This idea can be seen in the works of [26–29] where a <mark>representative time-domain input pulse is selected to excite a broad frequency range in order to carry out transient topology optimization</mark> of antennas. However, this approach still <mark>does not provide the full control on the broad-band response in the frequency domain</mark> . This is because the <mark>objective function is defined in the time domain and the optimization only indirectly effects the frequency content of the signal</mark> that is being optimized. 

Although density based topology optimization methods provides the largest degree of design freedom, the ersatz material model presents several issues for multi-physics problems that are strongly coupled through the interface. The main reasons for this can be listed as the lack of physical interpretation of the intermediate densities and the stair-case (pixelized) boundary description. Generally, strongly coupled problems, i.e vibroacoustics, require accurate modeling of the interface in order to correctly capture and model the interactions between the two physics. As an alternative to density methods <mark>, level set based methods</mark> [30] show <mark>great promise wrt coupled multi-physics problems.</mark> These methods implicitly defines the geometry by an iso-level of the level set function, which is usually taken as the zero level contour of the level set function. However, when such methods are used with an ersatz material approach [31], the level set function is – similar to density methods – mapped onto a piece-wise constant density field and the interface is still represented with an interpolated gray area. Hence, such level set methods suffers the same drawbacks as the density based topology optimization for coupled problems. <mark>Alternatively, the geometry defined by the zero level of the level set function can also be captured with body fitted meshes [32] in which a very accurate modeling of the coupled physics can be realized depending on the quality of the elements along the re-meshed interface.</mark> This approach commonly uses the solution of a <mark>Hamilton Jacobi type equation to update the design</mark> by moving the interface based on the calculated shape sensitivities. However, due to the <mark>re-meshing operation at each design iteration, the approach is not efficient</mark> for parallel computing frameworks. Moreover, <mark>numerical noise may be introduced in the sensitivities</mark> due to the extrapolation from the fitted mesh onto the background mesh. Examples of classical level set approach for the optimization of vibroacoustic systems can be found in [33–35]. A detailed comparative review of density, level set and evolutionary based optimization methods for vibroacoustics can be found in the work of [36]. 

<mark>Cumbersome re-meshing operation can be avoided when the level set approach is coupled with immersed boundary methods</mark> [37] to capture the geometry. The present work utilizes the <mark>immersed boundary cut element method</mark> presented in [38] to model the exact (piece-wise linear) boundary represented by the zero contour. Throughout the optimization, a fixed mesh is used and the accurate modeling of the interface elements is realized through a special integration scheme. The employed cut element method can be categorized <mark>as a special (simplified) case of CutFEM without stabilization [39], finite cell method [40] or X-FEM without enrichment [41].</mark> For the optimization, the utilized cut element method is coupled with the <mark>so-called explicit level set approach</mark> [42,43] in which the nodal level set values ar <mark>e directly tied to the mathematical design variables</mark> . This approach enables the utilization of nonlinear programming tools, such as the Method of Moving Asymptotes (MMA) algorithm [44] that is used in the current work, and in turn allows for general optimization frameworks where multiple objective and constraint functions easily can be considered in the optimization. 

The current work focuses on topology, or generalized shape, optimization of transient vibroacoustic problems where a time dependent problem formulation allows the realization of wideband performance optimization in the frequency domain. Throughout the work, the frequency response of the coupled vibroacoustic system is obtained through a fast Fourier transform (FFT) algorithm applied to the transient response of the system. This allows the objective function to be defined in frequency domain where the optimization can directly tailor the response of the coupled system in order to realize different filter shapes. The performance of the optimized design in realizing pass-band and stop-band for the considered frequency range relies on several, individual resonators and reflectors vibrating according to the underlying acoustic–structural interactions. To the best of the authors’ knowledge, utilization of time-domain methods to realize wideband optimization in the frequency domain have not been demonstrated before for structural optimization of strongly coupled vibroacoustic problems. Moreover, the work <mark>utilizes the discrete adjoint method for calculating the gradients of the objective function</mark> [45,46]. A thorough explanation of the consistent sensitivity calculation through the discrete adjoint framework that involves the FFT operation, which is needed to define the objective function in frequency domain, is also among the contributions of the present work. The remainder of the paper is organized as follows. Section 2 introduces the level set geometry representation, the vibroacoustic governing equations along with the immersed boundary formulation and spatial and temporal discretizations. The optimization problem as well as the sensitivity analysis using the discrete adjoint method are introduced in Section 3. Section 4 describes the numerical setup that is used for the considered examples throughout the work. The numerical examples are presented in Section 5 where the developed framework is applied for the optimization of various vibroacoustic filter designs. 

# **2. Theory and methods** 

# _2.1. Geometry representation_ 

As the aim of this work is to perform structural optimization for vibroacoustic design problems, it is natural to start by introducing the geometry representation. Throughout this work, the geometry is represented by a scalar valued function _̄ 𝑠_ , i.e. a level set function, which is used to identify the acoustic and structural domains embedded inside the total computational domain _𝛺_ = _𝛺𝑠_ ∪ _𝛺𝑎_ as 

2 



<!-- Start of picture text -->
(a) (b)<br>8( _ x) > 7 a( x) =0 E8888S555 COSeneeeSSSSSses BeBeesae8<br>fier cuecezeces Geet<br>Pp} 6Cut element<br>fb g S a e  <°<br>ae oH coeeeeee. sues<br>aae GEESE S SSSSeeee e 55ee SEEESSS>=5<br>e S558 Geeeeeee saan<br>éy Ze As/ oo PTTSa(ReeeeeeSEGHERRERS HoH si 5 00> si 5 >0><br>mae ee<br>Seee coccccceee seme<br>eee coccecee gone<br>a(x) <0 SEES Geeeeeee SEEGER<br><!-- End of picture text -->

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 



**Fig. 2.** Schematic illustration of the coupled acoustic–structural system. Physical domains, the coupled interface and the boundary conditions are showed. 

where the tilde superscript specifies the original material properties and the parameter _𝛼_ ( **𝐱** ) is a dimensionless contrast parameter that is unity for the structural domain and taken as 10<sup>−8</sup> for the void phase of the structure. The acoustic pressure on the other hand is governed by the transient Helmholtz equation which is written as 



where _𝜌𝑎_ ( **𝐱** ) is the spatially varying density of the acoustic medium, _𝑐𝑎_ is the speed of sound in the acoustic medium. The spatially varying bulk modulus _𝐾𝑎_ ( **𝐱** ) for the acoustic domain is defined as _𝐾𝑎_ ( **𝐱** ) = _𝜌𝑎_ ( **𝐱** ) _𝑐𝑎_<sup>2.Theboundaryconditionsusedinthecurrent</sup> work for the transient acoustic pressure solution are the hard wall condition given in Eq. (8), the coupling boundary condition for the acoustic domain written in Eq. (9) and the absorbing boundary condition given in Eq. (10). The absorbing boundary condition is written with a plane wave radiation where _𝑝𝑖𝑛_ denotes the transient incoming acoustic wave. Similar to the structural equation, the transient response of the acoustic pressure is obtained in both the acoustic domain and its rigid phase (structural domain) and is calculated by changing the properties of the acoustic medium as 



The dimensionless contrast parameter _𝛼_ ( **𝐱** ) is similar to that used for the structural and takes the values of unity for the acoustic domain and 10<sup>−8</sup> for the rigid phase of the acoustic solution. A complete illustration of the resulting coupled acoustic–structure system considered in this work is given in Fig. 2 along with all the used boundary conditions <mark>. Note that the use of a single fictitious scaling parameter for both stiffness and density is known to cause issues with spurious modes in the fictitious domains that can lead to unphysical phenomena. However, throughout extensive numerical experiments, it was found that this did not pose any problems for the proposed method and we have therefore decided to use a single fixed fictitious domain parameter</mark> . 

# _2.3. Discretization_ 

This section describes the spatial and temporal discretizations for the coupled vibroacoustic system introduced in the previous sectio <mark>n The spatial discretization is obtained using the standard continuous Galerkin method</mark> [48] using Q4 elements and a special integration scheme is employed for the cut elements and the coupling interface, shown in blue and black, respectively, in Fig. 1. In short, the utilized cut element approach employs over-integration using a Gaussian quadrature rule for modeling the interface boundary through weighted integration. To this end, a triangulation is carried out in the identified cut Q4 elements to correctly place the integration points in the sub-elements as well as on the interface in order to integrate the coupling boundary conditions, Eqs. (5) and (9), inside the parent Q4 element. The reader is referred t <mark>o [46]</mark> for details regarding all necessary integrals. The resulting semi-discrete form of the coupled system then reads 



where **𝐌** is the mass matrix, **𝐂** is the damping matrix and **𝐊** is the stiffness matrix. The superscript _𝑛_ in Eq. (12) denotes the current time step. The combined state and load vectors, **𝐯** and **𝐡** , are given as 



4 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

where **𝐮** and **𝐩** are state vectors for structural displacement and acoustic pressure solutions, respectively. The vector **𝐠** denotes the source vector for the acoustic equation which is active when the absorption boundary condition (Eq. (10)) is utilized for radiating a plane wave. The weak form of the coupled system and the identification of the individual element matrices given in Eq. (12) are thoroughly described in [46]. 

For the temporal discretization, the current work employs an implicit time stepping scheme, namely the Newmark algorithm [49]. The method expresses the first and the second time derivative of the solution vector **𝐯** as 



where the parameters _𝑎_ 1 to _𝑎_ 6 are the so-called Newmark parameters. In order to derive final and complete linear system of equations which is solved in order to determine the solution vector **𝐯**<sup>_𝑛_</sup> for the current time _𝑛_ , the definitions given in Eqs. (14) and (15) are substituted into the semi-discrete form of the coupled system (Eq. (12)). The resulting equation reads 



where **𝐊** is an effective stiffness matrix and **𝐡**<sup>_𝑛_</sup> is the effective load vector, identified as 





where _𝛥𝑡_ is the time step size. The current work uses a specific family of the Newmark algorithm [50] in which the parameters _𝛽_ and _𝛾_ are selected so that the employed time discretization becomes unconditionally stable. _𝛽_ and _𝛾_ are given as 



The presented work assumes the initial conditions **𝐯**<sup>0</sup> and **𝐯**<sup>0</sup> are zero which is a minor simplification compared to [46]. The initial condition for the second time derivative of the solution vector **𝐯**<sup>0</sup> is therefore found by setting the **𝐯**<sup>0</sup> and **𝐯**<sup>0</sup> to zero in Eq. (12) and solving the following equation 



To simplify notation when formulating the optimization problem, the vectors of state variables and residuals are collected in two single vectors **𝐔**<sup>_𝑛_</sup> and **𝐑**<sup>_𝑛_</sup> , respectively, which are given as 



Here the individual residual vectors **𝐫**<sup>_𝑛_</sup><sup>**𝐫**</sup><sup>_𝑛_</sup><sup>**𝐫**</sup><sup>_𝑛_identifiedfromthediscretizedcoupledsystemgivenintheEqs.(14)to(18)</sup> 1<sup>,</sup> 2<sup>and</sup> 3<sup>are</sup> as 



The solution of the coupled vibroacoustic system has been implemented in C++ using the PETSc library [51–53] for parallel data management. Moreover, the parallel direct solver MUMPS [54,55] is utilized for the solution of Eq. (16). Here it is noted that the validation study of the developed transient vibroacoustic framework is presented in [46]. 

# **3. Optimization formulation** 

# _3.1. Design parameterization_ 

Having established the physical model and how it is capable of handling arbitrary geometries, the next step is to introduce the design parameterization. To achieve this, a mathematical design variable, _𝑠_ , is introduced and then tied to the previously defined level set function _𝑠_ through the operations presented here. The design variable is, similar to the physical design field _𝑠_ , defined on nodal points in the computational mesh and is bound between zero and one as this allows for easy use of the MMA optimizer, i.e. 

(26) 

0 ≤ _𝑠_ ≤ 1 

5 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

Although this scaling is appropriate for the optimizer used in this work, it can lead to rapid speeds at which the zero iso-level can change in one design update. This can be alleviated with a mesh dependent mapping which ensures that the iso-level does not evolve too rapidly during the optimization by mapping the design variable into a new variable _𝑠_ which has the upper and lower bounds corresponding to half the element size [56] as 



Lastly, a smoothing filter is applied to regularize and stabilize the optimization process and to obtain the physical design variable _𝑠_ . This is achieved by use of PDE filter [57] with Neumann boundary conditions, i.e. 



where _𝑟_ is the filter radius and the subscript _𝑐_ denotes the variables that are defined in cell centers since the filter PDE is solved using a finite volume implementation. Therefore two additional smoothing steps are implied whenever the filter equation is applied due to interpolations between nodes of the mesh and cell centers (from _̃ 𝑠_ to _̃ 𝑠𝑐_ and from _̄ 𝑠𝑐_ to _̄ 𝑠_ ). Here it is noted that the reason the filter PDE is solved with a finite volume implementation is that with using a finite volume method, a numerically stable result can be obtained with any filter radius value (it can even be set to zero). Finally, it should be remarked that no length scale is imposed as this is deemed outside the scope of the current work since the focus is on broadband frequency control. 

# _3.2. Optimization problem formulation_ 

To control the structural and acoustic behavior over a wide range of frequencies, we propose to cast the optimization problem in frequency space. This requires that the temporal state response is transformed, which is achieved through the fast Fourier transform (FFT) yielding the vector of state variables in the frequency domain, **𝐔**<sup>_𝑚_</sup> _𝑓_<sup>.Heresuperscript</sup><sup>_𝑚_referstofrequencypointandsubscript</sup> _𝑓_ is used to distinguish the vector from its temporal counterpart. Here it is noted that due to the FFT operation, the state vector in frequency domain **𝐔**<sup>_𝑚_</sup> _𝑓_<sup>consistsofcomplexnumbers.Formally,thediscreteFouriertransformisdefinedas</sup> 



where _𝑖_ is the imaginary unit defined as _𝑖_ = ~~√~~ −1 and _𝑁_ is the total number of time steps. Note that a Hanning window is applied to the temporal signal before passing it through the FFT and that the current work uses the FFTW library [58]. The generic nested optimization problem considered here can now be formulated as 



where _𝛷_ is the objective function defined in frequency domain and _𝑀_ is the total number of discrete frequencies that is considered in objective function. Moreover, _𝜓𝑖_ ( **𝐔**<sup>_𝑚_</sup> _𝑓_<sup>(</sup><sup>**𝐬**)</sup> ) denotes the _𝐾_ additional constraint functions considered for the optimization. The vector of 

design variables is denoted as **𝐬** where _𝑠𝑚𝑖𝑛_ and _𝑠𝑚𝑎𝑥_ are its lower and upper bounds (Eq. (26)) which are set as 0 and 1, respectively. The residual equations are required to be fulfilled at every design step, and is based on the vector of physical design variables **𝐬** as seen from the Eq. (31). 

The optimization problem stated in Eqs. (30) to (33) is solved using the standard Method of Moving Asymptotes (MMA) algorithm [44]. Specifically, the present work includes the parallel implementation of MMA from [59]. 

# _3.3. Sensitivity analysis_ 

Sensitivity information is required in order to utilize a gradient based optimizer for solving the problem in Eqs. (30)–(33), and this section provides the necessary details for a general discrete function defined in the frequency domain based on a discrete temporal response, i.e. _𝛷_ = _𝛷_<sup>(</sup> **𝐔** _𝑓_ ( **𝐔** ( **𝐬** ))<sup>)</sup> . We remark that a common trend in time dependent sensitivity analysis is to use the so-called semi-discrete adjoint approach, e.g. [60], in which the problem is considered discrete in space but continuous in time. However, the semi-discrete approach is not suitable for cases where the objective function is defined discretely in the frequency domain using FFT. This is because the semi-discrete approach derives the adjoint equation assuming the objective function is defined in time domain. This means that using a discrete FFT cannot be incorporated within the semi-discrete approach. Recently the work of [46] also showed that even for pure transient optimiz <mark>ation problems, the semi-discrete method may not produce consistent sensitivities which can result in gradients having wrong signs. Hence, for calculating consistent and exact sensitivities, the work utilizes a fully discrete sensitivity analysis for calculating the gradient of the objective function with respect to the design variable</mark> . However, it should be noted tha <mark>t if a continuous Fourier transform was used, the semi-discrete approach could still be used in which both the temporal and the frequency response are considered as continuous variables</mark> [61]. 

6 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

The derivation of the fully discrete sensitivity analysis is initiated with an augmentation of the objective function by the residual equations and a vector of Lagrangian multipliers **Λ** . Moreover, note that summation of multiple frequencies have been left out in the following to allow for a more condensed description, but that extension to multiple frequencies is straightforward. The Lagrangian function  then reads 



where it should be highlighted that the objective function is written as a function of discrete state variables in the frequency domain **𝐔** _𝑓_ which are an explicit function of the discrete transient state variables **𝐔** . Due to the Newmark algorithm, similar to the state variables, the vector of Lagrangian multipliers also consists of three fields as 



The derivative of the Lagrangian function with respect to the physical design variable **𝐬** can now be written as 



_𝑛_ domain.to the transient state variable, i.e.The termIn( _𝜕_ order _𝜕𝛷_ **𝐔** _𝑓 𝜕𝜕_ **𝐔𝐔** _<u>𝑓</u>_ to)calculateon the right hand side of Eq.<sup>_𝜕𝛷_</sup> _𝜕_ **𝐔** _𝑛_<sup>_𝜕𝛷_</sup> , _𝜕_ **𝐔** firstly _𝑛_ , which needs to contain the chain rule describing the connection between frequency and timethe term (36) _𝜕𝜕𝛷_ **𝐔** _𝑓_ is equivalent to the partial derivative of the objective function with respect<sup>iscalculated.Since</sup><sup>**𝐔**</sup><sup>_𝑓_consistsofcomplexnumbersdefinedinfrequency</sup> domain, the partial derivative _𝜕𝜕𝛷_ **𝐔** _𝑓_<sup>alsoconsistsofcomplexnumbersandthederivativeisthereforerealizedas</sup> 



where the subscripts _𝑟_ and _𝑖_ denote the real and imaginary parts of a complex number. Since multiple frequencies are included, one should in fact compute Eq. (37) for each frequency _𝑚_ in the considered range. The term _𝜕𝜕_ **𝐔𝐔** _<u>𝑓</u>_<sup>linksthefrequencydomaintotime</sup> domain. Since the applied discrete Fourier transform operation can be seen as a linear transformation, the partial derivative _𝜕𝜕_ **𝐔𝐔** _<u>𝑓</u>_ describes the discrete Fourier transform operation itself. Hence, the partial derivative of the objective function with respect to the transient state variable<sup>_𝜕𝛷_</sup> _𝑛_ is rewritten as _𝜕_ **𝐔** 



where _𝜕𝜕_ **𝐔𝐔** _<u>𝑓</u> 𝑇_ is simply the application of the inverse discrete Fourier transform operation. The order of operations in Eq. (38) can beoperationstated asonthe _𝜕𝜕𝛷_ **𝐔** _𝑓_ following<sup>toobtain</sup> two<sup>_𝜕𝛷_</sup> _𝜕_ **𝐔** _𝑛_ ,steps:now defined(1) calculatein timethedomain.partial Remarkderivativethat _𝜕𝜕𝛷_ **𝐔** the _𝑓_<sup>and</sup> sensitivity<sup>(2)apply</sup> expression<sup>theinverse</sup> becomes<sup>discrete</sup> real<sup>Fourier</sup> after application<sup>transform</sup> 

of the inverse FFT. For completeness, this process can be stated formally using the inverse discrete Fourier transform as 



which also shows how multiple frequencies are included. 

Having introduced the chain rule describing the link between frequency and time domains, in order to continue the sensitivity analysis, the derivative of the Lagrangian function with respect to the physical design variable (Eq. (36)) is rewritten as 



Using the fact that the Lagrangian vector can be freely chosen, the underlined part in the above equation is set to zero to prevent the calculation of the partial derivative<sup>_𝜕_</sup> _𝜕̄_<sup>**𝐔**</sup> **𝐬**<sup>_𝑛_.Thisgivesrisetotheadjointequation</sup> 



where the superscript _𝑛_ is dropped to highlight that the adjoint problem in Eq. (41) contains the all time steps considered. The partial derivative of the residual vector with respect to the state variables<sup>_𝜕_</sup> _𝜕_<sup>**𝐑**</sup> **𝐔**<sup>hasthefollowinggeneralformforalinearsetof</sup> 

7 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

residuals 



<!-- Start of picture text -->
⎡⎢ 𝐀 𝐔 0 𝟎 ⎤⎥<br>⎢⎢ 𝐁 𝐔 1 𝟎 𝐀 𝐔 1 𝟏 ⎥⎥<br>𝜕 𝐑 ⎢ ⋱ ⋱ ⎥<br>⎢ ⎥<br>𝜕 𝐔 = ⎢ ⋱ ⋱ ⎥<br>⎢ ⎥<br>⎢ 𝐁 𝐔 𝑁 𝐍 −1 − 𝟐 𝐀 𝑁 𝐔𝐍 −1 − 𝟏 ⎥<br>⎢⎣ 𝐁 𝑁 𝐔𝐍 − 𝟏 𝐀 𝐔 𝑁 𝐍 ⎥⎦<br><!-- End of picture text -->



Here, the subscripts show the current time step and the superscripts denote the corresponding state variables. Sub-matrices in<sup>_𝜕_</sup> _𝜕_<sup>**𝐑**</sup> **𝐔** (Eq. (42)) are written in general form which considers a time integration scheme that depends on the current time step and one previous time step. From this fact, the residual function for a time step _𝑛_ can be written as 

# **𝐑**<sup>_𝑛_</sup> = **𝐀𝐔**<sup>_𝑛_</sup> + **𝐁𝐔**<sup>_𝑛_−1</sup> 



Generally, as it can be seen from the above equation, the sub-matrices **𝐀** and **𝐁** remain constant for each time step and can be identified from the residual equations given in Eqs. (23) to (25). Explicit definitions of the sub-matrices **𝐀** and **𝐁** considering the Newmark algorithm can be found in [46]. Moreover, the sub-matrix **𝐀** 0 is easily identified from the initial conditions. Due to the transpose operation applied on _𝜕_<sup>_𝜕_</sup> **𝐔**<sup>**𝐑**fortheadjointequationgiveninEq.(41),thesolutionoftheadjointequationisrealizedfrom</sup> reverse pseudo time steps which are written as 



After the adjoint equation is solved for the Lagrangian variables **Λ**<sup>_𝑛_</sup> , the final sensitivity of the objective function is calculated as 



Considering a Newmark time stepping scheme, the explicit definitions of the partial derivatives<sup>_𝜕_</sup> _𝜕̄_<sup>**𝐀**</sup> **𝐬**<sup>**<u>𝟎</u>**,</sup><sup>_𝜕_</sup> _𝜕̄_<sup>**𝐀**</sup> **𝐬**<sup>and</sup><sup>_𝜕_</sup> _𝜕̄_<sup>**𝐁**</sup> **𝐬**<sup>aregivenin[46].</sup> As it can be seen from the gradient calculation in Eq. (47), state variables from the solution of the transient coupled vibroacoustic system needs to be stored in order to carry out discrete adjoint method for transient optimization problems. Alternatively, a checkpointing scheme could be applied to reduce memory usage at the cost of an additional forward analysis. It is noted that the gradient calculation given in Eq. (47) is only done for the cut elements since the partial derivatives<sup>_𝜕_</sup> _𝜕̄_<sup>**𝐀**</sup> **𝐬**<sup>**<u>𝟎</u>**,</sup><sup>_𝜕_</sup> _𝜕̄_<sup>**𝐀**</sup> **𝐬**<sup>and</sup><sup>_𝜕_</sup> _𝜕̄_<sup>**𝐁**</sup> **𝐬**<sup>arezeroelsewhere.</sup> 

The above sensitivity analysis provides the gradient of the objective function with respect to the physical design variable **𝐬** , whereas the gradient with respect to the mathematical design variable **𝐬** is obtained from application of the following chain rule 





As it can be seen from the above chain rule, the partial derivatives describe the link between the physical design variable **𝐬** and mathematical design variable **𝐬** . The partial derivatives in the chain rule are applied in reverse order in order to calculate the gradient of the objective function with respect to the design variable<sup>d</sup> d<sup>_𝛷_</sup> **𝐬**<sup>. Here, the partial derivative</sup><sup>_𝜕̃_</sup> _𝜕_<sup>**𝐬**</sup> **𝐬**<sup>describes changing the bounds on the</sup> mathematical design variable,<sup>_𝜕̃_</sup> _𝜕̃_<sup>**𝐬**</sup> **𝐬**<sup>**<u>𝐜</u>**describes the interpolating</sup><sup>**𝐬**from nodes to the element centers,</sup><sup>_𝜕̄_</sup> _𝜕̃_ **𝐬**<sup>**𝐬**</sup> **𝐜**<sup>**<u>𝐜</u>**is the chain rule regarding the</sup> employed PDE filter in which its implementation details are thoroughly given in [57] and _𝜕̄𝜕̄_ **𝐬𝐬𝐜**<sup>describestheinterpolationoperation</sup> from element centers to the nodes of the mesh. 

Here it is noted that, for each optimization case that is considered for the current work, the calculated sensitivities are checked against a first order finite difference calculation. In the worst case, the difference between the calculated sensitivity and the finite difference computation was well below 0.1% since the utilized discrete adjoint method always yields exact and consistent sensitivities. 

# **4. Numerical setup** 

Here, the numerical setup used for the optimization of transient vibroacoustic filter designs is presented. The section also introduces the objective function along with the computational domain and material properties that are used throughout the work. The goal of the optimization is to design a structure inside an acoustic channel which acts as, at a certain frequency range, a wave stopper while also allowing the incoming wave to pass at a different predefined frequency set which will be achieved with the internal reflections and the underlying acoustic–structural interactions. In order to define the objective function which is used for 

8 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

the optimization of acoustic filter designs for a wide frequency range, a measure of transmission is needed to be defined. To achieve this, after the transient response of the state variables are obtained from the solution of the transient coupled vibroacoustic system, the transmitted acoustic pressure signal is integrated at the outlet of the considered acoustic duct as 



where the subscript 0 denotes the transmitted acoustic pressure when there is no structure in the acoustic duct which is calculated the same way as _𝑝_ ( _𝑓_ ). As it can be seen from the above Eq. (51), when the value of _𝑆_ ( _𝑓_ ) is unity for a particular frequency a full transmission is achieved. Meaning that the recorded amplitude of the transmitted acoustic pressure for an empty acoustic duct at a certain frequency is equal to that of an acoustic duct with a vibrating structure present in it. Moreover when the value of _𝑆_ ( _𝑓_ ) is zero for a certain frequency, there is no transmission realized compared to the transmission of the empty duct. In other words, the vibrating structure does not transmit acoustic pressure towards to outlet of the duct. 

In order to reflect the so-called pass-band and stop-band regions in the considered frequency span, two objective functions are considered which are written as 



where the minimization of the function _𝛷_ 1 fits the frequency response of the transmitted acoustic pressure to that of an empty acoustic duct hence realizes a full transmission in a frequency window defined between _𝑛_ 1 and _𝑛_ 2. The minimization of _𝛷_ 2 on the other hand lowers the amplitude of the frequency response of the transmitted acoustic pressure compared to the response of an empty acoustic duct in a frequency range defined between _𝑛_ 3 and _𝑛_ 4. The order of magnitude difference between the transmitted acoustic pressure in the stop-band region and the empty acoustic duct is defined by the parameter _𝑏_ in Eq. (53). The study on the selection of _𝑏_ in order to realize an effective zero-transmission and its effect on the overall optimization performance are given in Section 5.1. Remark that the proposed functions in Eqs. (52) and (53) were chosen from several candidate functions as they performed best for the filter design problem. Among the tested candidate formulations, we also tried to minimize the unweighted difference between _𝑆_ ( _𝑓_ ) and _𝑎_ together with the difference between _𝑆_ ( _𝑓_ ) and _𝑏_ and an even simpler approach in which _𝑆_ ( _𝑓_ ) was maximized for the pass bands and minimized for the stop bands. However, none of these approaches worked as well as the weighted least squares type formulation. Moreover, it should be emphasized that solving dynamic problems over wide frequency ranges presents a truly challenging design problem with many local minima due to the non-convexity. Hence, there may be better problem formulations available, but out of all the formulation investigated, the one presented next is the one that proved best for the filter design problems. 

In order to realize an optimization problem where multiple objective functions are equally minimized, the problem can be formally written in a so-called min–max formulation as 





However, since the min–max formulation is not differentiable an additional variable is introduced and the optimization problem is cast as a bound formulation as 

|min<br>**𝐬**_, 𝑧_|_𝑧_|(55)|
|---|---|---|
|s.t.|**𝐑**<sup>_𝑛_</sup>(**𝐬**_,_**𝐔**<sup>_𝑛_</sup>(**𝐬**)) = 0_,_<br>for_𝑛_= 0_,_1_,_…_, 𝑁_|(56)|
||_𝛷_1<br>(**𝐔**_𝑓_(**𝐔**(**𝐬**)))_< 𝑧_|(57)|
||_𝛷_2<br>(**𝐔**_𝑓_(**𝐔**(**𝐬**)))_< 𝑧_|(58)|
||0≤**𝐬**≤1|(59)|



where the additional variable z is the upper bound for the optimization. The formulation realizes the minimization of the upper bound _𝑧_ where the objective functions _𝛷_ 1 and _𝛷_ 2 are defined as constraints in the optimization problem, hence effectively minimizing both _𝛷_ 1 and _𝛷_ 2. In what follows, the functions _𝛷_ 1 and _𝛷_ 2 are therefore referred to as constraint functions. Note that no external box constraints are imposed on _𝑧_ as this is included using the internal bound variable in MMA. 

9 



<!-- Start of picture text -->
n,Vp = 0 u=0 nagVp= 0<br>Qe Qs Qa<br>Pin 0.1m Pout<br>0.3m 0.1m<br>nagVp = 0 u=0 n Vp = 0<br>7 10°<br>aila | wn o<br>fs, A 0 1" = w<br>00.005 0.01 0.015 0.02 6 | |<br>t{s] 10,000 3000 4000 5000 6000<br>f [Hz]<br><!-- End of picture text -->



<!-- Start of picture text -->
10 "!<br>oe<br>& 10 7<br>Q<br>10 °<br>1 0<br>0 0.5 1 1.5 2<br>f [Hz] «104<br><!-- End of picture text -->

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

**Table 2** 

Material <u>properties</u> considered for the acoustic domain. 

|_𝑐𝑎_[m∕s]<br>343|_𝜌𝑎_[kg∕m<sup>3</sup>]<br>1.21|
|---|---|



The Rayleigh parameters _𝛼𝑑_ and _𝛽𝑑_ are calculated according to [62] as 



here _𝜁_ is called the damping ratio and is taken to be _𝜁_ = 0 _._ 1. Moreover, the work assumes that the two natural frequencies _𝜔_ 1 and _𝜔_ 2 are _𝜔_ 1 = 1600 2 _𝜋_ rad∕s and _𝜔_ 2 = 2200 2 _𝜋_ rad∕s. Throughout the work, the computational domain given in Fig. 3 is meshed with structured quad elements in which each element has an edge length of 2 × 10<sup>−3</sup> m, i.e. a total of 12 500 elements. For optimization, the filter radius _𝑟_ is set to 8 × 10<sup>−3</sup> m. 

Throughout the work, the utilized MMA algorithm for solving the optimization problem presented in Eqs. (55) to (59) uses the asymptote parameters of 0.5, 0.7 and 1.2 which are used for controlling the initial adaptation, decrease and increase of the asymptotes, respectively. The penalty parameter which is used for constraints in MMA algorithm is chosen as 1000. For all of the optimization cases that are considered, the work does not consider a specific stopping criteria. Instead, the optimization is simply run for a fixed number of iterations. This choice is made due to the following reasons. First, since the performance of the design is extremely sensitive to the small design perturbations, as is the case for many dynamic optimization problems [16], small oscillations in the constraint functions occur throughout the optimization process. Secondly, as we use the MMA _𝑧_ for the bound variable, we do not have explicit control on the move limits for the objective variable, which can also lead to oscillatory behavior. Hence, the presented optimization results are obtained using a fixed number of iterations. Moreover, the considered optimization problem does not include a volume constraint on the structure. Since, having only a slab of solid material in the design domain does not form a trivial answer to the optimization problem. 

# **5. Numerical examples** 

To demonstrate the capabilities and limitations of the proposed optimization framework, a series of numerical experiments are conducted in order to examine the performance of the chosen constraint function, the dependence on initial configurations, its application to filter design as well as a validation study using commercial finite software. Remark that the final validation of the proposed time-domain cut element optimization framework and its optimization result is obtained by comparing the response to the result of a COMSOL analysis using a fully coupled vibroacoustic time harmonic (steady state) simulation. 

# _5.1. Constraint function study_ 

The considered constraint function, i.e. Eqs. (52) and (53), is first studied in order to illuminate the influence of the input parameters. As it is introduced in the previous Section 4, the pass-band and stop-band regions for the constraint functions are controlled with the parameters _𝑎_ and _𝑏_ , respectively. Minimizing the constraint function _𝛷_ 1 with having the _𝑎_ parameter as unity fits the calculated transmission value _𝑆_ ( _𝑓_ ) of the structure inside an acoustic duct to 1, effectively realizing full transmission in the considered frequency range. Ideally, the parameter _𝑏_ on the other hand needs to be set as 0 in order to realize zero transmission _𝑆_ ( _𝑓_ ) = 0 in the frequency range defining the stop-band region of the filter that is considered for the optimization. However, as it can be seen from the Eqs. (52) and (53), an inverse weighting is utilized in the constraint functions. Throughout our numerical experiments it has been found that the utilized inverse weighting provided designs with superior performances compared to the designs obtained using constraint functions without any weighting. Hence, a small positive number is used for the _𝑏_ parameter in order to avoid division by zero in the constraint function _𝛷_ 2. This section investigates the effect of the _𝑏_ parameter on the optimization and the final performance of the optimized design. 

Moreover, the section considers a low-pass acoustic filter design for the study. The constraint function _𝛷_ 1 defining the pass-band region operates on the frequencies between 1000 Hz ≤ _𝑓_ ≤ 2500 Hz. For the stop-band, the constraint function _𝛷_ 2 on the other hand is chosen to be active on the frequencies between 2500 Hz _< 𝑓_ ≤ 4000 Hz. Three different _𝑏_ parameters are selected to carry out the study which are 1 × 10<sup>−2</sup> , 1 × 10<sup>−3</sup> and 1 × 10<sup>−4</sup> . Remark that several additional values of _𝑏_ were considered in our numerical experiments, but were discarded as they led to poor numerical performance. Further discussion of this follows at the end of this section. 

Fig. 5(a) shows the initial configuration that is used for the optimization. The physical design variables **𝐬** that specifies the initial design is obtained first by calculating the following expression 



11 



<!-- Start of picture text -->
(a)<br>& 1 0 ° TTT<br>7” 4-2 |<br>‘0 ! |<br>1000 1500 2000 2500 3000 3500 4000<br>f [Hz]<br>(b)<br><!-- End of picture text -->



<!-- Start of picture text -->
@ -@ ae 6 :<br>ee°<br>ee )<br>ee<br>| . @°-@ -e@<br>(a) ®; = 5.06428,®2 = 4.99466<br>4<br>s<br>cen a<br>(b) ®; = 10.3386,2 = 10.5572<br>(c) ©; = 14.5365, 2 = 16.9208<br>80<br>& i Ey 40<br>AH 10 192 we - == 20 \<br>\ am<br>1 0% — = - 20<br>- 40<br>1000 1500 2000 2500 3000 3500 4000 1000 1500 2000 2500 3000 3500 4000<br><!-- End of picture text -->



<!-- Start of picture text -->
(a)<br>0 y=<br>aS 10 2<br>DN<br>10 %<br>1000 1500 2000 2500 3000 3500 4000<br>f [Hz]<br>(b)<br><!-- End of picture text -->



<!-- Start of picture text -->
s_bar<br>-1.0e-03 -6.0e-4 -4.0e-4 -2.0e-4 0.0 3.2e-04<br>mm | — |<br>= Nw w<br>\ w ‘ ap a<br>» "a;<br>az<br>Ls<br>(a)<br><!-- End of picture text -->



<!-- Start of picture text -->
—— 9,<br>104 — %)<br>10? | MAL LaNadhaadabs,Lae i a<br>10° |<br>07<br>0 100 200 300 400<br># of iterations<br><!-- End of picture text -->



<!-- Start of picture text -->
(a) Design iteration = 0<br>e r eo. &<br>@e e ee<br>e oe e - ee<br>ee : e @ t )<br>ee @ e 8<br>. . e e e<br>(b) Design iteration = 10<br>@e@o @ sad<br>e @e ee e<br>° e e b/ @ee<br>eeee > Pd > @ )<br>(c) Design iteration = 20<br>me ee 5 ry t )<br>e .* @<br>(d) Design iteration = 40<br>@-e a @ 2<br>e@<br>e<br>° e<br>v.; * @ @-e@ e<br>(e) Design iteration = 100<br>‘. ‘e’ A a ees<br>> ee ees :<br>e ®, a @e@ -e@<br>(f) Design iteration = 300<br>_* ‘e 7 a ( i<br>a ° @ P.S<br>%, os @-@@ -e<br>(g) Design iteration = 400<br><!-- End of picture text -->



<!-- Start of picture text -->
(a) ®; = 0.0154458,62 = 5.71289 x 107<br>(b) ®y = 1.0096, 62 = 5.89415 x 107<br>(c) 6; = 0.0573009,B2 = 5.71361 x 107<br>° oss1 LA |AsscrpagatledSW Wd on i} “AY \e<br>S S 09<br>n 2 n<br>10 0 . 85<br>0.8<br>1 04 0 . 75<br>1000 1500 2000 2500 3000 3500 4000 1000 1500 2000 2500 3000 3500 4000<br>f [Hz] f [Hz]<br>(d) (e)<br><!-- End of picture text -->



<!-- Start of picture text -->
(a) ®; = 13.7815,®2 = 11.6695<br>(b) &, = 9.31977, 62 = 10.0032<br><!-- End of picture text -->



<!-- Start of picture text -->
(c) ®; = 50.7457, Ba = 19.9735<br>10° FIMNF SI w 8060 W AV ~<a tng<br>xz<br>c j a 40 f<br>S 192 —_ , : 20 . a fen<br>OIE PNY > 90 np 4)<br>10 4 2 0 ix<br>- 40<br>1000 1500 2000 2500 3000 3500 4000 1000 1500 2000 2500 3000 3500 4000<br>f [Hz] f [Hz]<br>(d) (e)<br><!-- End of picture text -->



<!-- Start of picture text -->
@<br>Vv<br>(a) B, = 20.4625,by = 24.4682<br>10° LA/ ~*<br>&<br>DN 19 2<br>ISy -- <> J "7 Wa TK<br>1 0<br>1000 1500 2000 2500 3000 3500 4000 4500 5000 5500<br>f [Hz]<br>(b)<br>80<br>a<br>2B 60 a Vf AV;<br>4<br>Q 20<br>Nn<br>0<br>- 20<br>1000 1500 2000 2500 3000 3500 4000 4500 5000 5500<br>f [Hz]<br>(c)<br><!-- End of picture text -->



<!-- Start of picture text -->
r 0 e s e<br>@ee<br>e<br>ie) e ‘ @ . e @ D @@<br>e @ eo e tT<br>e eU.°<br>(a) ©; = 24.8701, 2 = 60.3181<br>= 1 0 ° ~~ —DFIS<br>mn 10 2 /<br>We pA 9<br>~ NI<br>10 4<br>1000 1500 2000 2500 3000 3500 4000 4500 5000 5500<br>f [Hz]<br>(b)<br>8060 WAL1 ly al dlaA<br>40<br>—<br>a 20<br>mn<br>0<br>- 20<br>1000 1500 2000 2500 3000 3500 4000 4500 5000 5500<br>f [Hz]<br>(c)<br><!-- End of picture text -->



<!-- Start of picture text -->
(a) ®, = 14.4407, ®2 = 79.0555<br><!-- End of picture text -->



<!-- Start of picture text -->
10° SS ~\<br>sy 1010 -4 7 \y. NAMA van fp<br>V4<br>500 1000 1500 2000 2500 3000 3500<br>f [Hz]<br>(b)<br><!-- End of picture text -->



<!-- Start of picture text -->
-20 0 20 0 60 80 100 115<br>(a)<br>(b)<br><!-- End of picture text -->



<!-- Start of picture text -->
Umag<br>0.0de+00 5e-9 le-8 1.5e-8 2e-8 2.5e-8 3.0e-08<br>me —<br>(a)<br>(b)<br><!-- End of picture text -->





<!-- Start of picture text -->
(a)<br>10° = > Sasa<br>2<br>S - 2 |<br>mH 10<br>—— Target<br>——— Comsol OR |<br>4<br>10 ° = = =C omsol Mod1 \ [\ /\ /\<br>—— Comsol Mod2 4 7 T7/ V<br>500 1000 1500 2000 2500 3000 3500<br>f [Hz]<br>(b)<br><!-- End of picture text -->

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

to expand the design parameterization to include manufacturing requirements. This includes imposition of minimum length scales using e.g. geometric constraints [65] or the robust design approach [38,66]. <mark>Moreover, extension to 3D and treatment of the freely flying structures must also be addressed before manufacturing and application to industrially relevant problems.</mark> The latter could be done through the inclusion of additional connectivity constraints such as non-zero structural eigenfrequency requirements, the virtual temperature method or similar approaches. 

# **Replication of results** 

For replicating the presented examples, all necessary information is given in the corresponding sections. Moreover, the code can be obtained from the authors upon reasonable request. 

# **CRediT authorship contribution statement** 

**Cetin B. Dilgen:** Writing – original draft, Writing – review & editing. **Niels Aage:** Supervision, Writing – review & editing. 

# **Declaration of competing interest** 

All authors declare that they have no conflicts of interest. 

# **Data availability** 

For replicating the presented examples, all necessary information is given in the corresponding sections. Moreover, the code can be obtained from the authors upon reasonable request. 

# **References** 

- [1] M.P. Bendsøe, O. Sigmund, Topology Optimization - Theory, Methods, and Applications, Springer Verlag, 2003. 

- [2] N. Aage, E. Andreassen, B.S. Lazarov, O. Sigmund, Giga-voxel computational morphogenesis for structural design, Nature 550 (7674) (2017) 84–86, http://dx.doi.org/10.1038/nature23911. 

- [3] E.A. Kontoleontos, E.M. Papoutsis-Kiachagias, A.S. Zymaris, D.I. Papadimitriou, K.C. Giannakoglou, Adjoint-based constrained topology optimization for viscous flows, including heat transfer, Eng. Optim. 45 (8) (2013) 941–961, http://dx.doi.org/10.1080/0305215X.2012.717074. 

- [4] C.B. Dilgen, S.B. Dilgen, D.R. Fuhrman, O. Sigmund, B.S. Lazarov, Topology optimization of turbulent flows, Comput. Methods Appl. Mech. Engrg. 331 (2018) 363–393, http://dx.doi.org/10.1016/j.cma.2017.11.029. 

- [5] O. Sigmund, Design of multiphysics actuators using topology optimization - Part I, Comput. Methods Appl. Mech. Engrg. 190 (49–50) (2001) 6577–6604, http://dx.doi.org/10.1016/s0045-7825(01)00251-1. 

- [6] J. Alexandersen, O. Sigmund, N. Aage, Large scale three-dimensional topology optimisation of heat sinks cooled by natural convection, Int. J. Heat Mass Transfer 100 (2016) 876–891, http://dx.doi.org/10.1016/j.ijheatmasstransfer.2016.05.013. 

- [7] E.J. Haug, H.S. Arora, Design sensitivity analysis of elastic mechanical systems, Comput. Methods Appl. Mech. Engrg. 15 (1) (1978) 35–62. 

- [8] P. Michaleris, D.A. Tortorelli, C.A. Vidal, Tangent operators and design sensitivity formulations for transient non-linear coupled problems with applications to elastoplasticity, Internat. J. Numer. Methods Engrg. 37 (14) (1994) 2471–99, 2471–2499, http://dx.doi.org/10.1002/nme.1620371408. 

- [9] C.B.W. Pedersen, Crashworthiness design of transient frame structures using topology optimization, Comput. Methods Appl. Mech. Engrg. 193 (6–8) (2004) 653–678, http://dx.doi.org/10.1016/j.cma.2003.11.001. 

- [10] Y. Li, K. Saitou, N. Kikuchi, Topology optimization of thermally actuated compliant mechanisms considering time-transient effect, Finite Elem. Anal. Des. 40 (11) (2004) 1317–1331, http://dx.doi.org/10.1016/j.finel.2003.05.002. 

- [11] S. Turteltaub, Optimal non-homogeneous composites for dynamic loading, Struct. Multidiscip. Optim. 30 (2) (2005) 101–112, http://dx.doi.org/10.1007/ s00158-004-0502-0. 

- [12] P. Seyranian, E. Lund, N. Olhoff, Multiple eigenvalues in structural optimization problems, Struct. Optim. 8 (4) (1994) 207–227, http://dx.doi.org/10. 1007/BF01742705. 

- [13] J.S. Jensen, O. Sigmund, Topology optimization for nano-photonics, Laser Photon. Rev. 5 (2) (2011) 308–321, http://dx.doi.org/10.1002/lpor.201000014. 

- [14] M.B. Dühring, J.S. Jensen, O. Sigmund, Acoustic design by topology optimization, J. Sound Vib. 317 (3–5) (2008) 557–575, http://dx.doi.org/10.1016/j. jsv.2008.03.042. 

- [15] J. Park, S. Wang, Noise reduction for compressors by modes control using topology optimization of eigenvalue, J. Sound Vib. 315 (4–5) (2008) 836–848, http://dx.doi.org/10.1016/j.jsv.2008.01.064. 

- [16] R.E. Christiansen, O. Sigmund, Experimental validation of systematically designed acoustic hyperbolic meta material slab exhibiting negative refraction, Appl. Phys. Lett. 109 (10) (2016) 101905, http://dx.doi.org/10.1063/1.4962441. 

- [17] A.H. Bokhari, A. Mousavi, B. Niu, E. Wadbro, Topology optimization of an acoustic diode? Struct. Multidiscip. Optim. 63 (6) (2021) 2739–2749, http://dx.doi.org/10.1007/s00158-020-02832-9. 

- [18] G.H. Yoon, J.S. Jensen, O. Sigmund, Topology optimization of acoustic-structure interaction problems using a mixed finite element formulation, Internat. J. Numer. Methods Engrg. 70 (9) (2007) 1049–1075, http://dx.doi.org/10.1002/nme.1900. 

- [19] W. Vicente, R. Picelli, R. Pavanello, Y. Xie, Topology optimization of frequency responses of fluid–structure interaction systems, Finite Elem. Anal. Des. 98 (2015) 1–13, http://dx.doi.org/10.1016/j.finel.2015.01.009, URL http://linkinghub.elsevier.com/retrieve/pii/S0168874X15000104. 

- [20] Y. Noguchi, T. Yamada, T. Yamamoto, K. Izui, S. Nishiwaki, Topological derivative for an acoustic-elastic coupled system based on two-phase material model, Mech. Eng. Lett. 2 (2016) 16–00246–16–00246, http://dx.doi.org/10.1299/mel.16-00246, URL https://www.jstage.jst.go.jp/article/mel/ 2/0/2{_}16-00246/_article. 

- [21] G. Fujii, M. Takahashi, Y. Akimoto, Acoustic cloak designed by topology optimization for acoustic-elastic coupled systems, Appl. Phys. Lett. 118 (10) (2021) 8–14, http://dx.doi.org/10.1063/5.0040911. 

- [22] J. Kook, J.H. Chang, A high-level programming language implementation of topology optimization applied to the acoustic-structure interaction problem, Struct. Multidiscip. Optim. 2001 (2001) (2021) http://dx.doi.org/10.1007/s00158-021-03052-5. 

25 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

- [23] D. Giannini, M. Schevenels, E.P. Reynders, Optimization of material thickness distribution in single and double partition panels for maximized sound insulation, Struct. Multidiscip. Optim. 66 (12) (2023) 1–18, http://dx.doi.org/10.1007/s00158-023-03682-x. 

- [24] L. Xu, W. Zhang, Z. Liu, X. Guo, Topology optimization of acoustic–mechanical structures for enhancing sound quality, Acta Mech. Solida Sin. 36 (5) (2023) 612–623, http://dx.doi.org/10.1007/s10338-023-00408-w. 

- [25] C. JOG, Topology design of structures subjected to periodic loading, J. Sound Vib. 253 (3) (2002) 687–709, http://dx.doi.org/10.1006/jsvi.2001.4075, URL https://linkinghub.elsevier.com/retrieve/pii/S0022460X01940751. 

- [26] T. Nomura, K. Sato, K. Taguchi, T. Kashiwa, S. Nishiwaki, Structural topology optimization for the design of broadband dielectric resonator antennas using the finite difference time domain technique, Internat. J. Numer. Methods Engrg. 71 (11) (2007) 1261–1296, http://dx.doi.org/10.1002/nme.1974. 

- [27] E. Hassan, E. Wadbro, M. Berggren, Topology optimization of metallic antennas, Ieee Trans. Antennas Propag. 62 (5) (2014) 6750741, http://dx.doi.org/ 10.1109/tap.2014.2309112, 2488–2500. 

- [28] E. Hassan, D. Noreland, R. Augustine, E. Wadbro, M. Berggren, Topology optimization of planar antennas for wideband near-field coupling, Ieee Trans. Antennas Propag. 63 (9) (2015) 7134720, http://dx.doi.org/10.1109/TAP.2015.2449894, 4208–4213. 

- [29] J. Hyun, H.A. Kim, Transient level-set topology optimization of a planar acoustic lens working with short-duration pulse, J. Acoust. Soc. Am. 149 (5) (2021) 3010–3026, http://dx.doi.org/10.1121/10.0004819, URL https://asa.scitation.org/doi/10.1121/10.0004819. 

- [30] S. Osher, J. Sethian, Fronts propagating with curvature-dependent speed - algorithms based on Hamilton-Jacobi formulations, J. Comput. Phys. 79 (1) (1988) 12–49, http://dx.doi.org/10.1016/0021-9991(88)90002-2. 

- [31] S. Wang, M.Y. Wang, Radial basis functions and level set method for structural topology optimization, Internat. J. Numer. Methods Engrg. 65 (12) (2006) 2060–2090, http://dx.doi.org/10.1002/nme.1536. 

- [32] G. Allaire, C. Dapogny, P. Frey, Shape optimization with a level set based mesh evolution method, Comput. Methods Appl. Mech. Engrg. 282 (2014) 22–53, http://dx.doi.org/10.1016/j.cma.2014.08.028. 

- [33] L. Shu, M. Yu Wang, Z. Ma, Level set based topology optimization of vibrating structures for coupled acoustic-structural dynamics, Comput. Struct. 132 (2014) 34–42, http://dx.doi.org/10.1016/j.compstruc.2013.10.019. 

- [34] H. Isakari, T. Kondo, T. Takahashi, T. Matsumoto, A level-set-based topology optimisation for acoustic–elastic coupled problems with a fast BEM–FEM solver, Comput. Methods Appl. Mech. Engrg. 315 (2017) 501–521, http://dx.doi.org/10.1016/j.cma.2016.11.006. 

- [35] J. Desai, A. Faure, G. Michailidis, G. Parry, R. Estevez, Topology optimization in acoustics and elasto-acoustics via a level-set method, J. Sound Vib. 420 (2018) 73–103, http://dx.doi.org/10.1016/j.jsv.2018.01.032. 

- [36] C.B. Dilgen, S.B. Dilgen, N. Aage, J.S. Jensen, Topology optimization of acoustic mechanical interaction problems: a comparative review, Struct. Multidiscip. Optim. 60 (2) (2019) 779–801, http://dx.doi.org/10.1007/s00158-019-02236-4. 

- [37] J.A. Sethian, A. Wiegmann, Structural boundary design via level set and immersed interface methods, J. Comput. Phys. 163 (2) (2000) 489–528, http://dx.doi.org/10.1006/jcph.2000.6581. 

- [38] C.S. Andreasen, M.O. Elingaard, N. Aage, Level set topology and shape optimization by density methods using cut elements with length scale control, Struct. Multidiscip. Optim. 62 (2) (2020) 685–707, http://dx.doi.org/10.1007/s00158-020-02527-1, URL www.topopt.dtu.dkhttp://link.springer.com/10. 1007/s00158-020-02527-1. 

- [39] E. Burman, S. Claus, P. Hansbo, M.G. Larson, A. Massing, CutFEM: Discretizing geometry and partial differential equations, Internat. J. Numer. Methods Engrg. 104 (7) (2015) 472–501, http://dx.doi.org/10.1002/nme.4823. 

- [40] A. Düster, J. Parvizian, Z. Yang, E. Rank, The finite cell method for three-dimensional problems of solid mechanics, Comput. Methods Appl. Mech. Engrg. 197 (45–48) (2008) 3768–3782, http://dx.doi.org/10.1016/j.cma.2008.02.036. 

- [41] C. Daux, N. Moës, J. Dolbow, N. Sukumar, T. Belytschko, Arbitrary branched and intersecting cracks with the extended finite element method, Internat. J. Numer. Methods Engrg. 48 (12) (2000) 1741–1760, http://dx.doi.org/10.1002/1097-0207(20000830)48:12<1741::AID-NME956>3.0.CO;2-L. 

- [42] M.J. De Ruiter, F. Van Keulen, Topology optimization using a topology description function, Struct. Multidiscip. Optim. 26 (6) (2004) 406–416, http://dx.doi.org/10.1007/s00158-003-0375-7. 

- [43] S. Kreissl, K. Maute, Levelset based fluid topology optimization using the extended finite element method, Struct. Multidiscip. Optim. 46 (3) (2012) 311–326, http://dx.doi.org/10.1007/s00158-012-0782-8. 

- [44] K. Svanberg, The method of moving asymptotes—a new method for structural optimization, Internat. J. Numer. Methods Engrg. 24 (2) (1987) 359–373, doi: 10.1002/nme.1620240207, 10.1002/(ISSN)1097-0207. 

- [45] N. Pollini, O. Lavan, O. Amir, Adjoint sensitivity analysis and optimization of hysteretic dynamic systems with nonlinear viscous dampers, Struct. Multidiscip. Optim. 57 (6) (2018) 2273–2289, http://dx.doi.org/10.1007/s00158-017-1858-2, URL http://link.springer.com/10.1007/s00158-017-1858-2. 

- [46] C.B. Dilgen, N. Aage, Generalized shape optimization of transient vibroacoustic problems using cut elements, Internat. J. Numer. Methods Engrg. 122 (6) (2021) 1578–1601, http://dx.doi.org/10.1002/nme.6591, URL https://onlinelibrary.wiley.com/doi/10.1002/nme.6591. 

- [47] S.B. Dilgen, J.S. Jensen, N. Aage, Shape optimization of the time-harmonic response of vibroacoustic devices using cut elements, Finite Elem. Anal. Des. 196 (May) (2021) 103608, http://dx.doi.org/10.1016/j.finel.2021.103608, https://linkinghub.elsevier.com/retrieve/pii/S0168874X21000925. 

- [48] O. Zienkiewicz, R. Taylor, The Finite Element Method, Butterworth Heinemann, 2000, p. 459 s. 

- [49] N.M. Newmark, A method of computation for structural dynamics, J. Eng. Mech. Div. 85 (3) (1959) 67–94. 

- [50] B.P. Jacob, N.F.F. Ebecken, An optimized implementation of the Newmark/Newton-Raphson algorithm for the time integration of non-linear problems, Commun. Numer. Methods. Eng. 10 (12) (1994) 983–992, http://dx.doi.org/10.1002/cnm.1640101204. 

- [51] S. Balay, S. Abhyankar, M.F. Adams, J. Brown, P. Brune, K. Buschelman, L. Dalcin, A. Dener, V. Eijkhout, W.D. Gropp, D. Kaushik, M.G. Knepley, D.A. May, L.C. McInnes, R.T. Mills, T. Munson, K. Rupp, P. Sanan, B.F. Smith, S. Zampini, H. Zhang, H. Zhang, PETSc web page, 2018, URL http://www.mcs.anl.gov/petsc. 

- [52] S. Balay, S. Abhyankar, M.F. Adams, J. Brown, P. Brune, K. Buschelman, L. Dalcin, A. Dener, V. Eijkhout, W.D. Gropp, D. Kaushik, M.G. Knepley, D.A. May, L.C. McInnes, R.T. Mills, T. Munson, K. Rupp, P. Sanan, B.F. Smith, S. Zampini, H. Zhang, H. Zhang, PETSc Users Manual, Tech. Rep. ANL-95/11 - Revision 3.10, Argonne National Laboratory, 2018, URL http://www.mcs.anl.gov/petsc. 

- [53] S. Balay, W.D. Gropp, L.C. McInnes, B.F. Smith, Efficient management of parallelism in object oriented numerical software libraries, in: E. Arge, A.M. Bruaset, H.P. Langtangen (Eds.), Modern Software Tools in Scientific Computing, Birkhäuser Press, 1997, pp. 163–202. 

- [54] P.R. Amestoy, I.S. Duff, J. Koster, J.-Y. L’Excellent, A fully asynchronous multifrontal solver using distributed dynamic scheduling, SIAM J. Matrix Anal. Appl. 23 (1) (2001) 15–41. 

- [55] P.R. Amestoy, A. Guermouche, J.-Y. L’Excellent, S. Pralet, Hybrid scheduling for the parallel solution of linear systems, Parallel Comput. 32 (2) (2006) 136–156. 

- [56] K. Maute, P. Coffin, Level set topology optimization of cooling and heating devices using a simplified convection model, Struct. Multidiscip. Optim. 53 (5) (2016) 985–1003, http://dx.doi.org/10.1007/s00158-015-1343-8. 

- [57] B.S. Lazarov, O. Sigmund, Filters in topology optimization based on Helmholtz-type differential equations, Internat. J. Numer. Methods Engrg. 86 (6) (2011) 765–781, http://dx.doi.org/10.1002/nme.3072. 

- [58] M. Frigo, S.G. Johnson, FFTW: An adaptive software architecture for the FFT, in: Proc. 1998 IEEE Intl. Conf. Acoustics Speech and Signal Processing, Vol. 3, IEEE, 1998, pp. 1381–1384. 

26 

_Finite Elements in Analysis & Design 234 (2024) 104123_ 

_C.B. Dilgen and N. Aage_ 

- [59] N. Aage, B.S. Lazarov, Parallel framework for topology optimization using the method of moving asymptotes, Struct. Multidiscip. Optim. 47 (4) (2013) 493–505, http://dx.doi.org/10.1007/s00158-012-0869-2. 

- [60] J. Dahl, J.S. Jensen, O. Sigmund, Topology optimization for transient wave propagation problems in one dimension, Struct. Multidiscip. Optim. 36 (6) (2008) 585–595, http://dx.doi.org/10.1007/s00158-007-0192-5. 

- [61] P. Zhou, Y. Peng, J. Du, Topology optimization of bi-material structures with frequency-domain objectives using time-domain simulation and sensitivity analysis, Struct. Multidiscip. Optim. 63 (2) (2021) 575–593, http://dx.doi.org/10.1007/s00158-020-02814-x, URL http://link.springer.com/10.1007/ s00158-020-02814-x. 

- [62] A.M. Puthanpurayil, O. Lavan, A.J. Carr, R.P. Dhakal, Elemental damping formulation: an alternative modelling of inherent damping in nonlinear dynamic analysis, Bull. Earthq. Eng. 14 (8) (2016) 2405–2434, http://dx.doi.org/10.1007/s10518-016-9904-9. 

- [63] N. Aage, V. Egede Johansen, Topology optimization of microwave waveguide filters, Internat. J. Numer. Methods Engrg. 112 (3) (2017) 283–300, http://dx.doi.org/10.1002/nme.5551, URL http://doi.wiley.com/10.1002/nme.5551. 

- [64] COMSOL multiphysics reference manual, version 5.5, 2020, www.comsol.com. 

- [65] M. Zhou, B.S. Lazarov, F. Wang, O. Sigmund, Minimum length scale in topology optimization by geometric constraints, Comput. Methods Appl. Mech. Engrg. 293 (2015) 266–282, http://dx.doi.org/10.1016/j.cma.2015.05.003, http://linkinghub.elsevier.com/retrieve/pii/S0045782515001693. 

- [66] F. Wang, B.S. Lazarov, O. Sigmund, On projection methods, convergence and robust formulations in topology optimization, Struct. Multidiscip. Optim. 43 (6) (2011) 767–784, http://dx.doi.org/10.1007/s00158-010-0602-y. 

27 

