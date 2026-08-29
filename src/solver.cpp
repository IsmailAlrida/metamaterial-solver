#include <algorithm>
#include <cmath>
#include <iostream>
#include "integrators.hpp"
#include "solver.hpp"


using namespace mfem;

namespace {

enum class DomainAttribute {
    inlet = 1,
    design = 2,
    outlet = 3
};

enum class CartesianBoundary2D {
    bottom = 1,
    right = 2,
    top = 3,
    left = 4
};

enum class CartesianBoundary3D {
    bottom = 1,
    front = 2,
    right = 3,
    back = 4,
    left = 5,
    top = 6
};

}

// move this later outside?
// TODO: Make comments doxygen-style with math and all to explain ur stuff

App::Solver::Solver( 
            const App::SolverSettings& settings,
            const App::OptimizerSettings& optimizer_settings,
            App::LevelSet& lset,
            App::SolverResult& result,
            const App::LogFunction& log
            )
            : 
            settings(settings),
            optimizer_settings(optimizer_settings),
            lset(lset),
            result(result),
            log(log),
            mesh(nullptr),
            fec(nullptr),
            level_set_fes(nullptr),
            scalar_fes(nullptr),
            displacement_fes(nullptr),
            fe_order(0),
            level_set_order(0),
            cut_integration_order(0)
            {
            }

App::Solver::~Solver()
{
    lset.detach();
}

bool App::Solver::run()
{
    status.store(SolverStatus::Working);

    try {
        if (!setMesh() || !assembleSolutionSpace()) {
            status.store(SolverStatus::Error);
            return false;
        }

        if (!solve()) {
            status.store(SolverStatus::Diverged);
            return false;
        }

        status.store(SolverStatus::Converged);
        return true;
    }
    catch (...) {
        status.store(SolverStatus::Error);
        throw;
    }
}

App::SolverStatus App::Solver::get_status() const
{
    return status.load();
}

// TODO: Do something about the mixed camelCase and snake_case. Choose one.
// Given for this iteration we will run the following sequentially
/* setMesh --> assembleSolutionSpace --> solve*/
// i doubt the bottleneck is in setting the mesh as much as it is in the actual forward solves
bool App::Solver::setMesh() {

    const int nx = settings.nx;
    const int ny = settings.ny;
    const int nz = settings.nz;
    const mfem::real_t sx = settings.inletLength
        + settings.designLength
        + settings.outletLength;
    const mfem::real_t sy = settings.sy;
    const mfem::real_t sz = settings.sz;

    if (nx <= 0 || ny < 0 || nz < 0
        || settings.inletLength <= 0.0
        || settings.designLength <= 0.0
        || settings.outletLength <= 0.0
        || sy <= 0.0 || sz <= 0.0) {
        log(App::LogLevel::Error, "Mesh element counts and extents must be positive.");
        return false;
    }

    if (ny == 0 && nz == 0) {
        log(App::LogLevel::Warning, "1D meshes are not supported yet.");
        return false;
    }

    if (nz > 0 && ny == 0) {
        log(App::LogLevel::Error, "A 3D mesh requires a positive y element count.");
        return false;
    }

    lset.detach();
    level_set_fes.reset();
    scalar_fes.reset();
    displacement_fes.reset();
    fec.reset();

    if (nz > 0) {
        mesh = std::make_unique<Mesh>(Mesh::MakeCartesian3D(nx, ny, nz, Element::HEXAHEDRON, sx, sy, sz));
    }
    else
    {
        mesh = std::make_unique<Mesh>(Mesh::MakeCartesian2D(nx, ny, Element::QUADRILATERAL, true, sx, sy));
    }

    fe_order = 1;
    level_set_order = 1;
    cut_integration_order = 4;

    log(App::LogLevel::Message, nz > 0 ? "Created 3D Cartesian mesh." : "Created 2D Cartesian mesh.");
    return true;
}

// TODO: Make a bloch-floquet periodic boundary condition?
bool App::Solver::assembleSolutionSpace(){

    // TODO: Fix the inconsistent use of floats, doubles, and mfem real_ts in the codebase.
    if (!mesh || fe_order <= 0 || settings.dt <= 0.0) {
        log(App::LogLevel::Error, "Cannot assemble the solution space: mesh, finite-element order, or time step is invalid.");
        return false;
    }

    const int dim = mesh->Dimension();
    M.reset();
    C.reset();
    K.reset();

    fec = std::make_unique<H1_FECollection>(fe_order, dim);
    level_set_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    scalar_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    displacement_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get(), dim, Ordering::byVDIM);
    lset.setSpace(*level_set_fes);

    // Domain regions along x
    int inlet_element_count = 0;
    int design_element_count = 0;
    int outlet_element_count = 0;
    const double design_start = settings.inletLength;
    const double design_end = settings.inletLength + settings.designLength;
    Vector element_center(dim);

    for (int element = 0; element < mesh->GetNE(); element++) {
        const Geometry::Type geometry = mesh->GetElementBaseGeometry(element);
        const IntegrationPoint& center = Geometries.GetCenter(geometry);
        ElementTransformation* transformation = mesh->GetElementTransformation(element);
        transformation->Transform(center, element_center);

        if (element_center[0] < design_start) {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::inlet));
            inlet_element_count++;
        }
        else if (element_center[0] < design_end) {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::design));
            design_element_count++;
        }
        else {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::outlet));
            outlet_element_count++;
        }
    }
    mesh->SetAttributes();

    const int classified_element_count = inlet_element_count
        + design_element_count
        + outlet_element_count;
    if (classified_element_count != mesh->GetNE()
        || inlet_element_count == 0
        || design_element_count == 0
        || outlet_element_count == 0) {
        log(App::LogLevel::Error,
            "The inlet, design, and outlet lengths must each contain at least one mesh element center.");
        return false;
    }

    Array<int> inlet_domain_marker(mesh->attributes.Max());
    Array<int> design_domain_marker(mesh->attributes.Max());
    Array<int> outlet_domain_marker(mesh->attributes.Max());
    Array<int> air_domain_marker(mesh->attributes.Max());
    inlet_domain_marker = 0;
    design_domain_marker = 0;
    outlet_domain_marker = 0;
    air_domain_marker = 0;
    inlet_domain_marker[static_cast<int>(DomainAttribute::inlet) - 1] = 1;
    design_domain_marker[static_cast<int>(DomainAttribute::design) - 1] = 1;
    outlet_domain_marker[static_cast<int>(DomainAttribute::outlet) - 1] = 1;
    air_domain_marker[static_cast<int>(DomainAttribute::inlet) - 1] = 1;
    air_domain_marker[static_cast<int>(DomainAttribute::outlet) - 1] = 1;

    log(App::LogLevel::Message,
        "Classified inlet, design, and outlet mesh regions along x.");

    // The level set has its own FE space. Only DOFs belonging exclusively to
    // design elements are editable; shared inlet/outlet interface DOFs remain air.
    PWConstCoefficient design_indicator(mesh->attributes.Max());
    design_indicator(static_cast<int>(DomainAttribute::design)) = 1.0;
    GridFunction design_support(level_set_fes.get());
    design_support.ProjectDiscCoefficient(
        design_indicator,
        GridFunction::ARITHMETIC
    );

    Array<int> active_level_set_dofs;
    for (int dof = 0; dof < design_support.Size(); dof++) {
        if (design_support[dof] == 1.0) {
            active_level_set_dofs.Append(dof);
        }
    }
    if (active_level_set_dofs.Size() == 0
        || active_level_set_dofs.Size() == level_set_fes->GetVSize()) {
        log(App::LogLevel::Error,
            "The level-set space must contain both active design and fixed-air DOFs.");
        return false;
    }
    lset.setActiveDesignDofs(active_level_set_dofs);


    //TODO: later make all below these comments persistent class members
    Array<int> pressure_boundary_dofs;
    Array<int> displacment_boundary_dofs;

    scalar_fes->GetBoundaryTrueDofs(pressure_boundary_dofs);
    displacement_fes->GetBoundaryTrueDofs(displacment_boundary_dofs);


    const real_t he = mesh->GetElementSize(0, 1);

    Vector mapped_design(lset.design);
    mapped_design -= 0.5;
    mapped_design *= he;

    GridFunction mapped_design_h(level_set_fes.get());
    mapped_design_h.SetFromTrueDofs(mapped_design);

    // Sample code for how we can get center DOFs
    Vector center_design_values(mesh->GetNE());
    for (int element = 0; element < mesh->GetNE(); element++){
        
        // Get the base shape of the cell
        const Geometry::Type geometry = mesh->GetElementBaseGeometry(element);
        // Get the point in the center of the shape
        const IntegrationPoint& center = Geometries.GetCenter(geometry);
        // Let the GridFunction of the shifted design variable inter
        center_design_values[element] = mapped_design_h.GetValue(element, center);

    };

    // Paper Eq. (28): -r^2 Laplacian(phi_c) + phi_c = mapped_design_c.
    // The unknowns live at cell centers. Boundary faces contribute no flux,
    // which imposes the homogeneous Neumann condition used in the paper.
    // I know its funky to have the optimizer's filter radius here, but just roll with it
    const real_t filter_radius = optimizer_settings.filterRadius;
    if (filter_radius < 0.0) {
        log(App::LogLevel::Error, "The PDE filter radius cannot be negative.");
        return false;
    }

    const int cell_count = mesh->GetNE();
    SparseMatrix filter_matrix(cell_count);
    Vector filter_rhs(cell_count);
    Vector filtered_center_values(cell_count);
    Vector cell_volumes(cell_count);

    for (int element = 0; element < cell_count; element++) {
        cell_volumes[element] = mesh->GetElementVolume(element);
        filter_matrix.Add(element, element, cell_volumes[element]);
        filter_rhs[element] = cell_volumes[element] * center_design_values[element];
    }

    Vector first_center(dim);
    Vector second_center(dim);
    for (int face = 0; face < mesh->GetNumFaces(); face++) {
        int first_element = -1;
        int second_element = -1;
        mesh->GetFaceElements(face, &first_element, &second_element);
        if (second_element < 0
            || !design_domain_marker[mesh->GetAttribute(first_element) - 1]
            || !design_domain_marker[mesh->GetAttribute(second_element) - 1]) {
            continue;
        }

        mesh->GetElementCenter(first_element, first_center);
        mesh->GetElementCenter(second_element, second_center);
        second_center -= first_center;
        const real_t center_distance_squared = second_center * second_center;
        if (center_distance_squared <= 0.0) {
            log(App::LogLevel::Error, "The PDE filter found coincident cell centers.");
            return false;
        }

        const real_t face_coefficient = filter_radius * filter_radius
            * 0.5 * (cell_volumes[first_element] + cell_volumes[second_element])
            / center_distance_squared;
        filter_matrix.Add(first_element, first_element, face_coefficient);
        filter_matrix.Add(first_element, second_element, -face_coefficient);
        filter_matrix.Add(second_element, first_element, -face_coefficient);
        filter_matrix.Add(second_element, second_element, face_coefficient);
    }
    filter_matrix.Finalize();

    GSSmoother filter_preconditioner(filter_matrix);
    CGSolver filter_solver;
    filter_solver.SetPreconditioner(filter_preconditioner);
    filter_solver.SetOperator(filter_matrix);
    // TODO: Delegate magic numbers to real app settings LATER
    filter_solver.SetRelTol(1.0e-12);
    filter_solver.SetAbsTol(1.0e-14);
    filter_solver.SetMaxIter(500);
    filter_solver.SetPrintLevel(-1);
    filtered_center_values = 0.0;
    filter_solver.Mult(filter_rhs, filtered_center_values);
    if (!filter_solver.GetConverged()) {
        log(App::LogLevel::Error, "The cell-centered PDE filter did not converge.");
        return false;
    }

    // Interpolate the filtered cell-center values back to the nodal level set.
    L2_FECollection cell_fec(0, dim);
    FiniteElementSpace cell_fes(mesh.get(), &cell_fec);
    GridFunction filtered_cells(&cell_fes);
    filtered_cells = filtered_center_values;
    GridFunctionCoefficient filtered_cell_coefficient(&filtered_cells);
    lset.phi->ProjectDiscCoefficient(
        filtered_cell_coefficient,
        GridFunction::ARITHMETIC
    );
    lset.phi->SetSubVectorComplement(
        lset.activeDesignDofs,
        -0.5 * he
    );

    log(App::LogLevel::Message, "Applied the cell-centered finite-volume PDE filter.");


    // TODO: ALl needs to be unique_pointered and set as class variables
    GridFunction pressure(scalar_fes.get());
    GridFunction displacement(displacement_fes.get());

    // All these doubles should be re-evaluted in later steps as mfem ConstantCoeffecients
    // Well, not CONSTANT coeffecient. We want to have a base coeff, then for the 
    // Fictitious domain, we want just a spatially varying coeff that is either real or fict
    // Depending on where it is in the cut

    const auto* physics = std::get_if<App::VibroacousticSettings>(&settings.physics);
    if (physics == nullptr) {
        log(App::LogLevel::Error, "The selected physics model is not supported by the vibroacoustic solver.");
        return false;
    }

    // Material settings
    const double epsilon_f = physics->epsilon; // fictitious contrast

    // Structural domain stuff
    const double rho_s = physics->rho_s; // Solid Density kg/m^3
    const double E = physics->youngs_modulus; // Young modulus Pa
    const double nu = physics->poisson_ratio; // Poisson ratio, which is the weird-looking V you see
    const double mu = E / (2*(1 + nu));    // Shear modulus
    const double lame_ps = (E * nu) / (1 - std::pow(nu, 2));
    const double lame_3D = (E * nu) / ((1 + nu)*(1 - 2*nu)); // FIXME: If-gate the lame coefficient


    // Acoustic domain stuff
    const double rho_a = physics->rho_a; // Fluid Density kg/m^3
    const double c_a = physics->c_a; // sound speed, m/s
    const double K_a = rho_a * std::pow(c_a, 2); // acoustic bulk modulus

    // Funny how C++ 17 doesnt have PI. Like how do you not have PI for 20 generations of C++?
    // Rayleigh Parameters
    const double omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const double omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const double alpha_d = (2 * physics->zeta * omega_1 * omega_2)/(omega_1 + omega_2); // Rayleigh mass factor, 1/s
    const double beta_d  = (2 * physics->zeta) / (omega_1 + omega_2); // Rayleigh stiffness factor, s

    // TODO: Multiplex 2D and 3D case
    double lambda;
    if (settings.nz > 0)
    {
        lambda = lame_3D;
    }
    else if (settings.nz == 0)
    {
        lambda = lame_ps;
    }
    else
    {
        // TODO: Get a better sense of humor
        log(App::LogLevel::Warning, "Negative nz again?");
        _sleep(200);
        throw;
    }

    // Spatially varying material coefficients
    LevelSetScaledCoefficient solid_rho(*lset.phi, rho_s, epsilon_f, true);
    LevelSetScaledCoefficient solid_lambda(*lset.phi, lambda, epsilon_f, true);
    LevelSetScaledCoefficient solid_mu(*lset.phi, mu, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_rho(*lset.phi, rho_s * alpha_d, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_lambda(*lset.phi, lambda * beta_d, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_mu(*lset.phi, mu * beta_d, epsilon_f, true);
    LevelSetScaledCoefficient acoustic_inv_rho(*lset.phi, 1/rho_a, epsilon_f, false);
    LevelSetScaledCoefficient acoustic_inv_bulk(*lset.phi, 1/K_a, epsilon_f, false);
    ConstantCoefficient acoustic_inv_impedance(1/(rho_a * c_a));

    // Weak form matrix blocks
    // All these should be unique_pointers initially null in the solver since their 
    // Construction is stateful
    // TODO: Reimplement these bad boys in the header file as unique pointers
    


    // We call finalize for all just before the run, better not finalize it when "setting" the thing and tweaking the params
    

    // You know, maybe we only need to have the final sparse matrix as class-wide, not these
    // These bad boys can be ressambleed each time
    // FInal input of this function is that the final discrete system 
    // Is assembled and ready to go
    BilinearForm Muu_form(displacement_fes.get());
    BilinearForm Kuu_form(displacement_fes.get());
    BilinearForm Cuu_form(displacement_fes.get());
    BilinearForm Mpp_form(scalar_fes.get());
    BilinearForm Kpp_form(scalar_fes.get());
    BilinearForm Cpp_form(scalar_fes.get());

    MixedBilinearForm Kup_form(
        scalar_fes.get(), 
        displacement_fes.get()
    );
    MixedBilinearForm Mpu_form(
        displacement_fes.get(), 
        scalar_fes.get()
    );

    // Note: with the way im calling state transitions, i would normally defer this assembly
    // And finalization to a later function, but since we do all this in one go, well.... doesnt really matter
    // Adding the integrators
    Muu_form.AddDomainIntegrator(
        new VectorMassIntegrator(
            solid_rho
        )
    );
    Muu_form.Assemble();
    Muu_form.Finalize();
    std::unique_ptr<SparseMatrix> Muu(Muu_form.LoseMat());

    Kuu_form.AddDomainIntegrator(
        new ElasticityIntegrator(
            solid_lambda,
            solid_mu
        ) // For the elasticity integrator, how does thios work
    );
    Kuu_form.Assemble();
    Kuu_form.Finalize();
    std::unique_ptr<SparseMatrix> Kuu(Kuu_form.LoseMat());


    Cuu_form.AddDomainIntegrator(
        new VectorMassIntegrator(
            damped_solid_rho
        )
    );
    Cuu_form.AddDomainIntegrator(
        new ElasticityIntegrator(
            damped_solid_lambda,
            damped_solid_mu
        )
    );
    Cuu_form.Assemble();
    Cuu_form.Finalize();
    std::unique_ptr<SparseMatrix> Cuu(Cuu_form.LoseMat());

    Kpp_form.AddDomainIntegrator(
        new DiffusionIntegrator(
            acoustic_inv_rho
        )
    );
    Kpp_form.Assemble();
    Kpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Kpp(Kpp_form.LoseMat());

    Mpp_form.AddDomainIntegrator(
        new MassIntegrator(
            acoustic_inv_bulk
        )
    );
    Mpp_form.Assemble();
    Mpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpp(Mpp_form.LoseMat());
    
    // TODO: How do we make the absorbing boundary into a periodic one with ?
    // oh yeah mesh brd_attributes go 1,2,3,4
    mfem::Array<int> absorbing_marker(
        mesh->bdr_attributes.Max()
    );
    absorbing_marker = 0;

    const int inlet_boundary_attribute = dim == 3
        ? static_cast<int>(CartesianBoundary3D::left)
        : static_cast<int>(CartesianBoundary2D::left);
    const int outlet_boundary_attribute = dim == 3
        ? static_cast<int>(CartesianBoundary3D::right)
        : static_cast<int>(CartesianBoundary2D::right);
    absorbing_marker[inlet_boundary_attribute - 1] = 1;
    absorbing_marker[outlet_boundary_attribute - 1] = 1;

    Cpp_form.AddBoundaryIntegrator(
        new BoundaryMassIntegrator(
            acoustic_inv_impedance
        ),
        absorbing_marker
    );
    Cpp_form.Assemble();
    Cpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Cpp(Cpp_form.LoseMat());

    // Kup: scalar pressure trial -> vector displacement test
    Kup_form.AddDomainIntegrator(
        new App::ImplicitSurfaceNormalIntegrator(
            *lset.phi,
            cut_integration_order,
            level_set_order,
            -1.0,
            false
        ),
        design_domain_marker
    );
    Kup_form.Assemble();
    Kup_form.Finalize();
    std::unique_ptr<SparseMatrix> Kup(Kup_form.LoseMat());

    // Mpu: vector displacement trial -> scalar pressure test
    Mpu_form.AddDomainIntegrator(
        new App::ImplicitSurfaceNormalIntegrator(
            *lset.phi,
            cut_integration_order,
            level_set_order,
            1.0,
            true
        ),
        design_domain_marker
    );
    Mpu_form.Assemble();
    Mpu_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpu(Mpu_form.LoseMat());

    std::unique_ptr<SparseMatrix> Kup_transpose(
        Transpose(*Kup)
    );
    std::unique_ptr<SparseMatrix> coupling_residual(
        Add(1.0, *Mpu, 1.0, *Kup_transpose)
    );
    const real_t coupling_scale = std::max(
        real_t{1.0},
        std::max(Mpu->MaxNorm(), Kup_transpose->MaxNorm())
    );
    if (coupling_residual->MaxNorm() > 1.0e-10 * coupling_scale) {
        log(App::LogLevel::Error, "The implicit coupling matrices do not satisfy Mpu = -Kup^T.");
        return false;
    }

    mfem::Array<int> offsets(3);
    offsets[0] = 0;
    offsets[1] = displacement_fes->GetVSize();
    offsets[2] = scalar_fes->GetVSize() + offsets[1];

    BlockMatrix M_blocks(offsets);
    M_blocks.SetBlock(0, 0, Muu.get());
    M_blocks.SetBlock(1, 0, Mpu.get());
    M_blocks.SetBlock(1, 1, Mpp.get());
    M.reset(M_blocks.CreateMonolithic());

    BlockMatrix C_blocks(offsets);
    C_blocks.SetBlock(0, 0, Cuu.get());
    C_blocks.SetBlock(1, 1, Cpp.get());
    C.reset(C_blocks.CreateMonolithic());

    BlockMatrix K_blocks(offsets);
    K_blocks.SetBlock(0, 0, Kuu.get());
    K_blocks.SetBlock(0, 1, Kup.get());
    K_blocks.SetBlock(1, 1, Kpp.get());
    K.reset(K_blocks.CreateMonolithic());

    if (M->CheckFinite() != 0 || C->CheckFinite() != 0 || K->CheckFinite() != 0) {
        log(App::LogLevel::Error, "The assembled global matrices contain non-finite values.");
        M.reset();
        C.reset();
        K.reset();
        return false;
    }

    log(App::LogLevel::Message, "Assembled the vibroacoustic solution space.");
    return true;
}


// TODO: we still have to sample the FFT into the FFT section of the solver result.
// Also TODO: we ought o
bool App::Solver::solve(){

    //TODO: We probably need to make a SolverResults array (sizeof the number of maxiterations) intiailly empty so at the app layer we can append results
    // Actually no that would suck, then we would have to deal with ballooning memory
    result.success = 0;
    result.U.clear();
    result.R.clear();

    if (!M || !C || !K) {
        log(App::LogLevel::Error, "Cannot solve before assembling M, C, and K.");
        return false;
    }
    if (settings.dt <= 0.0 || settings.duration <= 0.0) {
        log(App::LogLevel::Error, "The Newmark time step and duration must be positive.");
        return false;
    }
    if (M->Height() != M->Width()
        || C->Height() != M->Height() || C->Width() != M->Width()
        || K->Height() != M->Height() || K->Width() != M->Width()) {
        log(App::LogLevel::Error, "M, C, and K must have matching square dimensions.");
        return false;
    }

    // Newmark constants
    // These are selected to keep the algo UNCONDITIONALLY STABLE
    const double beta_nm = 0.25;
    const double gamma_nm = 0.5;

    const double a1 = 1.0 - gamma_nm/beta_nm;
    const double a2 = (1.0 - gamma_nm/(2*beta_nm)) * settings.dt;
    const double a3 = gamma_nm/(beta_nm * settings.dt);
    const double a4 = 1.0 / (beta_nm * settings.dt);
    const double a5 = 1/(2*beta_nm) - 1.0;
    const double a6 = 1 / (beta_nm * settings.dt * settings.dt);

    std::unique_ptr<SparseMatrix> mass_and_damping(
        Add(a6, *M, a3, *C)
    );
    std::unique_ptr<SparseMatrix> effective_stiffness(
        Add(1.0, *K, 1.0, *mass_and_damping)
    );

    if (effective_stiffness->CheckFinite() != 0) {
        log(App::LogLevel::Error, "The Newmark effective stiffness matrix contains non-finite values.");
        return false;
    }

    GSSmoother effective_preconditioner(*effective_stiffness);
    GMRESSolver linear_solver;
    linear_solver.SetPreconditioner(effective_preconditioner);
    linear_solver.SetOperator(*effective_stiffness);
    linear_solver.iterative_mode = true;
    linear_solver.SetRelTol(1.0e-10);
    linear_solver.SetAbsTol(1.0e-12);
    linear_solver.SetMaxIter(1000);
    // TODO: Capture prints to the imgui terminal (would be nice)
    linear_solver.SetPrintLevel(-1);

    const int state_size = M->Height();
    const int time_steps = static_cast<int>(std::ceil(settings.duration / settings.dt));

    Vector v(state_size);
    Vector v_dot(state_size);
    Vector v_ddot(state_size);
    v = 0.0;
    v_dot = 0.0;
    v_ddot = 0.0;

    result.U.reserve(time_steps + 1);
    result.R.reserve(time_steps + 1);

    Vector U_n(3 * state_size);
    Vector R_n(3 * state_size);
    U_n = 0.0;
    R_n = 0.0;
    result.U.push_back(U_n);
    result.R.push_back(R_n);

    Vector h(state_size);
    Vector h_hat(state_size);
    Vector x_M(state_size);
    Vector x_C(state_size);
    Vector y_M(state_size);
    Vector y_C(state_size);
    Vector v_new(state_size);
    Vector v_dot_new(state_size);
    Vector v_ddot_new(state_size);
    Vector delta_v(state_size);
    Vector r1(state_size);
    Vector r2(state_size);
    Vector r3(state_size);

    log(App::LogLevel::Warning,
        "The incoming-wave load is not assembled yet; this solve uses h^n = 0.");

    for (int n = 1; n <= time_steps; n++) {
        // TODO: Assemble the incoming-wave boundary load g(t) into the pressure block.
        h = 0.0;

        x_M = 0.0;
        x_M.Add(a4, v_dot);
        x_M.Add(a5, v_ddot);
        x_M.Add(a6, v);
        M->Mult(x_M, y_M);

        x_C = 0.0;
        x_C.Add(-a1, v_dot);
        x_C.Add(-a2, v_ddot);
        x_C.Add(a3, v);
        C->Mult(x_C, y_C);

        h_hat = h;
        h_hat += y_M;
        h_hat += y_C;

        v_new = v;
        linear_solver.Mult(h_hat, v_new);
        if (!linear_solver.GetConverged()) {
            log(App::LogLevel::Error,
                "The Newmark linear solve failed at time step " + std::to_string(n) + ".");
            result.U.clear();
            result.R.clear();
            return false;
        }

        delta_v = v_new;
        delta_v -= v;

        v_dot_new = 0.0;
        v_dot_new.Add(a1, v_dot);
        v_dot_new.Add(a2, v_ddot);
        v_dot_new.Add(a3, delta_v);

        v_ddot_new = 0.0;
        v_ddot_new.Add(-a4, v_dot);
        v_ddot_new.Add(-a5, v_ddot);
        v_ddot_new.Add(a6, delta_v);

        effective_stiffness->Mult(v_new, r1);
        r1 -= h_hat;

        r2 = v_dot_new;
        r2.Add(-a1, v_dot);
        r2.Add(-a2, v_ddot);
        r2.Add(-a3, delta_v);

        r3 = v_ddot_new;
        r3.Add(a4, v_dot);
        r3.Add(a5, v_ddot);
        r3.Add(-a6, delta_v);

        U_n.SetVector(v_new, 0);
        U_n.SetVector(v_dot_new, state_size);
        U_n.SetVector(v_ddot_new, 2 * state_size);
        R_n.SetVector(r1, 0);
        R_n.SetVector(r2, state_size);
        R_n.SetVector(r3, 2 * state_size);
        result.U.push_back(U_n);
        result.R.push_back(R_n);

        v = v_new;
        v_dot = v_dot_new;
        v_ddot = v_ddot_new;
    }

    result.success = 1;
    log(App::LogLevel::Message,
        "Completed " + std::to_string(time_steps) + " Newmark time steps.");
    return true;
}

bool App::Solver::bindToGlvis(){
    log(App::LogLevel::Warning, "GLVis binding is not implemented yet.");
    return false;
}

bool App::Solver::setup(){

    //todo: think about getting rid of this
    return true;
}
