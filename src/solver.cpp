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
            App::LevelSet& lset,
            App::SolverResult& result,
            const App::LogFunction& log
            )
            : 
            settings(settings),
            lset(lset),
            result(result),
            log(log),
            mesh(nullptr),
            fec(nullptr),
            pressure_fes(nullptr),
            displacement_fes(nullptr),
            level_set_fes(nullptr),
            fe_order(0),
            level_set_order(0),
            cut_integration_order(0)
            {
            }

App::Solver::~Solver() = default;

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

    pressure_fes.reset();
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

bool App::Solver::assembleSolutionSpace(){

    // TODO: Fix the inconsistent use of floats, doubles, and mfem real_ts in the codebase.
    if (!mesh || fe_order <= 0 || settings.dt <= 0.0) {
        log(App::LogLevel::Error, "Cannot assemble the solution space: mesh, finite-element order, or time step is invalid.");
        return false;
    }

    const int dim = mesh->Dimension();

    fec = std::make_unique<H1_FECollection>(fe_order, dim);
    pressure_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    displacement_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get(), dim, Ordering::byVDIM);

    // TODO: How can we construct your mesh from the app levelset?
    level_set_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());

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


    //TODO: later make all below these comments persistent class members
    Array<int> pressure_boundary_dofs;
    Array<int> displacment_boundary_dofs;
    Array<int> level_set_boundary_dofs;

    pressure_fes->GetBoundaryTrueDofs(pressure_boundary_dofs);
    displacement_fes->GetBoundaryTrueDofs(displacment_boundary_dofs);
    level_set_fes->GetBoundaryTrueDofs(level_set_boundary_dofs);


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

    // TODO: Run a filtered solver for the PDE filter


    // TODO: ALl needs to be unique_pointered and set as class variables
    GridFunction phi_h(level_set_fes.get());
    GridFunction pressure(pressure_fes.get());
    GridFunction displacement(displacement_fes.get());


    // TODO: Here is where we adapt the LevelSet lset into the phi_h grid function
    // phi_h.ProjectCoefficient(/* LevelSet coefficient */);

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

    // Newmark constants 
    // These are selected to keep the algo UNCONDITIONALLY STABLE
    double beta_nm = 0.25;
    double gamma_nm = 0.5;

    double a1 = 1.0 - gamma_nm/beta_nm;
    double a2 = (1.0 - gamma_nm/(2*beta_nm)) * settings.dt;
    double a3 = gamma_nm/(beta_nm * settings.dt);
    double a4 = 1.0 / (beta_nm * settings.dt);
    double a5 = 1/(2*beta_nm) - 1.0;
    double a6 = 1 / (beta_nm * settings.dt * settings.dt);

    
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
    LevelSetScaledCoefficient solid_rho(phi_h, rho_s, epsilon_f, true);
    LevelSetScaledCoefficient solid_lambda(phi_h, lambda, epsilon_f, true);
    LevelSetScaledCoefficient solid_mu(phi_h, mu, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_rho(phi_h, rho_s * alpha_d, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_lambda(phi_h, lambda * beta_d, epsilon_f, true);
    LevelSetScaledCoefficient damped_solid_mu(phi_h, mu * beta_d, epsilon_f, true);
    LevelSetScaledCoefficient acoustic_inv_rho(phi_h, 1/rho_a, epsilon_f, false);
    LevelSetScaledCoefficient acoustic_inv_bulk(phi_h, 1/K_a, epsilon_f, false);
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
    BilinearForm Mpp_form(pressure_fes.get());
    BilinearForm Kpp_form(pressure_fes.get());
    BilinearForm Cpp_form(pressure_fes.get());

    MixedBilinearForm Kup_form(
        pressure_fes.get(), 
        displacement_fes.get()
    );
    MixedBilinearForm Mpu_form(
        displacement_fes.get(), 
        pressure_fes.get()
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

    Kuu_form.AddDomainIntegrator(
        new ElasticityIntegrator(
            solid_lambda,
            solid_mu
        ) // For the elasticity integrator, how does thios work
    );
    Kuu_form.Assemble();
    Kuu_form.Finalize();

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

    Kpp_form.AddDomainIntegrator(
        new DiffusionIntegrator(
            acoustic_inv_rho
        )
    );
    Kpp_form.Assemble();
    Kpp_form.Finalize();

    Mpp_form.AddDomainIntegrator(
        new MassIntegrator(
            acoustic_inv_bulk
        )
    );
    Mpp_form.Assemble();
    Mpp_form.Finalize();
    
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

    // Kup: scalar pressure trial -> vector displacement test
    Kup_form.AddDomainIntegrator(
        new App::ImplicitSurfaceNormalIntegrator(
            phi_h,
            cut_integration_order,
            level_set_order,
            -1.0,
            false
        ),
        design_domain_marker
    );
    Kup_form.Assemble();
    Kup_form.Finalize();

    // Mpu: vector displacement trial -> scalar pressure test
    Mpu_form.AddDomainIntegrator(
        new App::ImplicitSurfaceNormalIntegrator(
            phi_h,
            cut_integration_order,
            level_set_order,
            1.0,
            true
        ),
        design_domain_marker
    );
    Mpu_form.Assemble();
    Mpu_form.Finalize();

    std::unique_ptr<SparseMatrix> Kup_transpose(
        Transpose(Kup_form.SpMat())
    );
    std::unique_ptr<SparseMatrix> coupling_residual(
        Add(1.0, Mpu_form.SpMat(), 1.0, *Kup_transpose)
    );
    const real_t coupling_scale = std::max(
        real_t{1.0},
        std::max(Mpu_form.SpMat().MaxNorm(), Kup_transpose->MaxNorm())
    );
    if (coupling_residual->MaxNorm() > 1.0e-10 * coupling_scale) {
        log(App::LogLevel::Error, "The implicit coupling matrices do not satisfy Mpu = -Kup^T.");
        return false;
    }

    SparseMatrix M;
    M.ad



    mfem::Vector g;
    mfem::BlockVector h;


    log(App::LogLevel::Message, "Assembled the vibroacoustic solution space.");
    return true;
}


bool App::Solver::solve(){
    log(App::LogLevel::Warning, "The forward solve is not implemented yet.");
    return false;
}

bool App::Solver::bindToGlvis(){
    const std::string& host = settings.glvisHost;
    const int port = settings.glvisPort;
    (void)host;
    (void)port;
    log(App::LogLevel::Warning, "GLVis binding is not implemented yet.");
    return false;
}

bool App::Solver::setup(){

    //todo: think about getting rid of this
    return true;
}
