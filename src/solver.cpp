#include <mfem.hpp>
#include <cmath>
#include <iostream>
#include <vector>
#include <string> 
#include "solver.hpp"
#include "coeffs.hpp"


using namespace mfem;

// move this later outside?
// TODO: Make comments doxygen-style with math and all to explain ur stuff

// TODO: Namespace all classes to App please, because here solver is ambigous, so let;s generalize it
App::Solver::Solver( 
            App::LevelSet& lset,
            App::SolverResult& result,
            const App::PhysicsProblem& problem,
            int nx, int ny, int nz,
            std::string solverAlgo
            )
            : 
            duration(0.2),
            dt(duration/1000),
            nx(nx),
            ny(ny),
            nz(nz),
            lset(lset),
            result(result),
            problem(problem),
            mesh(nullptr),
            fec(nullptr),
            pressure_fes(nullptr),
            displacement_fes(nullptr),
            level_set_fes(nullptr),
            fe_order(0),
            level_set_order(0),
            cut_integration_order(0),
            sx(1.0),
            sy(1.0),
            sz(1.0)
            {

                setMesh(nx, ny, nz);
                (void)solverAlgo;
            }

App::Solver::~Solver() = default;

//todo: do something about the mixed camelCase and snake_case. Choose one.
bool App::Solver::setMesh(int nx, int ny, int nz, mfem::real_t sx, mfem::real_t sy, mfem::real_t sz) {

    if (nx <= 0 || ny < 0 || nz < 0 || sx <= 0.0 || sy <= 0.0 || sz <= 0.0) {
        return false;
    }

    if (ny == 0 && nz == 0) {
        std::cout << "1D not yet lol." << std::endl;
        return false;
    }

    if (nz > 0 && ny == 0) {
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

    this->nx = nx;
    this->ny = ny;
    this->nz = nz;
    this->sx = sx;
    this->sy = sy;
    this->sz = sz;
    fe_order = 1;
    level_set_order = 1;
    cut_integration_order = 4;

    return true;
}

bool App::Solver::assembleSolutionSpace(){

    if (!mesh || fe_order <= 0) {
        return false;
    }

    const int dim = mesh->Dimension();

    fec = std::make_unique<H1_FECollection>(fe_order, dim);
    pressure_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    displacement_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get(), dim, Ordering::byVDIM);

    // TODO: How can we construct your mesh from the app levelset?
    level_set_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());


    //TODO: later make all below these comments persistent class members
    Array<int> pressure_boundary_dofs;
    Array<int> displacment_boundary_dofs;
    Array<int> level_set_boundary_dofs;

    pressure_fes->GetBoundaryTrueDofs(pressure_boundary_dofs);
    displacement_fes->GetBoundaryTrueDofs(displacment_boundary_dofs);
    level_set_fes->GetBoundaryTrueDofs(level_set_boundary_dofs);

    // TODO: ALl needs to be unique_pointered and set as class variables
    GridFunction phi_h(level_set_fes.get());
    GridFunction pressure(pressure_fes.get());
    GridFunction displacement(displacement_fes.get());


    // TODO: Here is where we adapt the LevelSet lset into the phi_h grid function
    phi_h.ProjectCoefficient();

    // because algoimintegrationrules takes the level set as a coefficient
    GridFunctionCoefficient phi_coeff(&phi_h);

    // All these doubles should be re-evaluted in later steps as mfem ConstantCoeffecients
    // Well, not CONSTANT coeffecient. We want to have a base coeff, then for the 
    // Fictitious domain, we want just a spatially varying coeff that is either real or fict
    // Depending on where it is in the cut

    // Material settings, these should be gotten from the data type structs
    double epsilon_f; // fictitious contrast

    // Structural domain stuff
    double rho_s; // Solid Density kg/m^3
    double E; // Young modulus Pa
    double nu; // Poisson ratio, which is the weird-looking V you see
    double mu = E / (2*(1 + nu));    // Shear modulus
    double lame_ps = (E * nu) / (1 - std::pow(nu, 2));
    double lame_3D = (E * nu) / ((1 + nu)*(1 - 2*nu));

    // Acoustic domain stuff
    double rho_a; // Fluid Density kg/m^3
    double c_a; // sound speed, m/s
    // FIXME: Use std::pow here
    double K_a = rho_a * std::pow(c_a, 2); // acoustic bulk modulus
    
    // Only holds if the zeta is equal at both frequencies omega 1 and omega 2
    // We'll make these configurable from the UI
    double alpha_d = (2 * zeta * omega_1 * omega_2)/(omega_1 + omega_2); // Rayleigh mass factor, 1/s
    double beta_d  = (2 * zeta) * (omega_1 + omega_2); // Rayleigh stiffness factor, s


    // Rayleigh Params

    // Newmark constants 

    // These are selected to keep the algo UNCONDITIONALLY STABLE
    double beta_nm = 0.25;
    double gamma_nm = 0.5;

    double a1 = 1.0 - gamma_nm/beta_nm;
    double a2 = (1.0 - gamma_nm/(2*beta_nm)) * dt;
    double a3 = gamma_nm/(beta_nm * dt);
    double a4 = 1.0 / (beta_nm * dt);
    double a5 = 1/(2*beta_nm) - 1.0;
    double a6 = 1 / (beta_nm * dt * dt);

    
    // Weak form matrix blocks
    // All these should be unique_pointers initially null in the solver since their 
    // Construction is stateful
    // TODO: Reimplement these bad boys in the header file as unique pointers
    


    // We call finalize for all just before the run, better not finalize it when "setting" the thing and tweaking the params
    

    // You know, maybe we only need to have the final sparse matrix as class-wide, not these
    // These bad boys can be ressambleed each time
    // FInal input of this function is that the final discrete system 
    // Is assembled and ready to go
    BilinearForm muu_form(displacement_fes.get());
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


    mfem::Vector g;
    mfem::BlockVector h;


    // we still need this bad boy to more accurately sample
    // stuff even in a fictitious domain
    // See, though we're doing physics on the whole thing, we're more able 
    // to discern the geometry through cutting, rather than just sampling based on pos/neg
    // Here, phi_degree is the polynomial degree of which we project the level set coefficient to a gridfunction
    mfem::AlgoimIntegrationRules solid_rules(cut_integration_order, phi_coeff, level_set_order);

    return true;
}


bool App::Solver::solve(){
    return false;
}

bool App::Solver::bindToGlvis(std::string host, int port){
    return false;
}

void App::Solver::setSimDuration(float val){
}

float App::Solver::getSimDuration(){
    float val = 0.0;
    return val;
}

void App::Solver::setProblem(std::string prob){
    (void)prob;
}

std::string App::Solver::getProblem(){
    std::string prob = "hi";
    
    return prob;
}

bool App::Solver::setup(){

    //todo: think about getting rid of this
    return true;
}
