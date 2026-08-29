#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "fftw3.h"
#include "glvis_adapter.hpp"
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

} // namespace

// move this later outside?
// TODO: Make comments doxygen-style with math and all to explain ur stuff
App::Solver::Solver(const App::SolverSettings& settings,
                    const App::OptimizerSettings& optimizer_settings,
                    App::LevelSet& lset,
                    App::SolverResult& result,
                    const App::LogFunction& log)
    : settings(settings),
      optimizer_settings(optimizer_settings),
      lset(lset),
      result(result),
      log(log),
      fe_order(1),
      level_set_order(1),
      cut_integration_order(4)
{
}

App::Solver::~Solver()
{
    lset.detach();
}

App::SolverStatus App::Solver::get_status() const
{
    return status.load();
}

// TODO: Do something about the mixed camelCase and snake_case. Choose one.
bool App::Solver::setMesh()
{
    const int nx = settings.nx;
    const int ny = settings.ny;
    const int nz = settings.nz;
    const real_t sx = settings.inletLength
        + settings.designLength + settings.outletLength;
    if (nx <= 0 || ny <= 0 || nz < 0
        || settings.inletLength <= 0.0
        || settings.designLength <= 0.0
        || settings.outletLength <= 0.0
        || settings.sy <= 0.0 || settings.sz <= 0.0) {
        log(LogLevel::Error, "Mesh element counts and extents must be positive.");
        return false;
    }

    // TODO: Complete and validate the independent 3D optimization track.
    if (nz > 0) {
        mesh = std::make_unique<Mesh>(Mesh::MakeCartesian3D(
            nx, ny, nz, Element::HEXAHEDRON, sx, settings.sy, settings.sz));
    }
    else {
        mesh = std::make_unique<Mesh>(Mesh::MakeCartesian2D(
            nx, ny, Element::QUADRILATERAL, true, sx, settings.sy));
    }

    lset.detach();
    level_set_fes.reset();
    scalar_fes.reset();
    displacement_fes.reset();
    fec.reset();
    M.reset();
    C.reset();
    K.reset();
    design_initialized = false;
    log(LogLevel::Message,
        nz > 0 ? "Created 3D Cartesian mesh." : "Created 2D Cartesian mesh.");
    return true;
}

// TODO: Make a bloch-floquet periodic boundary condition?
bool App::Solver::assembleSolutionSpace()
{
    if (!mesh || settings.duration <= 0.0 || settings.dt <= 0.0) {
        log(LogLevel::Error,
            "The duration and time step must be positive before assembly.");
        return false;
    }
    const double step_ratio = settings.duration / settings.dt;
    if (!std::isfinite(step_ratio) || step_ratio < 2.0
        || step_ratio > std::numeric_limits<int>::max()) {
        log(LogLevel::Error,
            "The duration contains an unsupported number of time steps.");
        return false;
    }
    const long long rounded_steps = std::llround(step_ratio);
    const double step_tolerance =
        1.0e-10 * std::max(1.0, std::abs(step_ratio));
    if (std::abs(step_ratio - static_cast<double>(rounded_steps))
            > step_tolerance) {
        log(LogLevel::Error,
            "The duration must contain an integer number of at least two time steps.");
        return false;
    }
    const int time_steps = static_cast<int>(rounded_steps);
    const auto* physics = std::get_if<VibroacousticSettings>(&settings.physics);
    if (physics == nullptr || physics->epsilon <= 0.0
        || physics->rho_s <= 0.0 || physics->youngs_modulus <= 0.0
        || physics->rho_a <= 0.0 || physics->c_a <= 0.0
        || physics->poisson_ratio < 0.0 || physics->poisson_ratio >= 0.5
        || physics->zeta < 0.0 || physics->f1 <= 0.0 || physics->f2 <= 0.0
        || settings.sourceAmplitude < 0.0
        || settings.initialPatternLx <= 0.0
        || settings.initialPatternLy <= 0.0) {
        log(LogLevel::Error, "The vibroacoustic material settings are invalid.");
        return false;
    }

    const int dim = mesh->Dimension();
    M.reset();
    C.reset();
    K.reset();
    lset.detach();
    level_set_fes.reset();
    scalar_fes.reset();
    displacement_fes.reset();
    fec.reset();
    fec = std::make_unique<H1_FECollection>(fe_order, dim);
    level_set_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    scalar_fes = std::make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    displacement_fes = std::make_unique<FiniteElementSpace>(
        mesh.get(), fec.get(), dim, Ordering::byVDIM);
    lset.setSpace(*level_set_fes);

    // Domain regions along x
    const double design_start = settings.inletLength;
    const double design_end = settings.inletLength + settings.designLength;
    const int cell_count = mesh->GetNE();
    int inlet_count = 0;
    int design_count = 0;
    int outlet_count = 0;
    DenseMatrix element_centers(cell_count, dim);
    Vector cell_volumes(cell_count);
    Vector center(dim);
    for (int element = 0; element < cell_count; ++element) {
        mesh->GetElementCenter(element, center);
        element_centers.SetRow(element, center);
        cell_volumes[element] = mesh->GetElementVolume(element);
        if (center[0] < design_start) {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::inlet));
            ++inlet_count;
        }
        else if (center[0] < design_end) {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::design));
            ++design_count;
        }
        else {
            mesh->SetAttribute(element, static_cast<int>(DomainAttribute::outlet));
            ++outlet_count;
        }
    }
    mesh->SetAttributes();
    if (inlet_count + design_count + outlet_count != cell_count
        || inlet_count == 0 || design_count == 0 || outlet_count == 0) {
        log(LogLevel::Error,
            "Each duct region must contain at least one mesh element center.");
        return false;
    }

    Array<int> design_domain_marker(mesh->attributes.Max());
    design_domain_marker = 0;
    design_domain_marker[static_cast<int>(DomainAttribute::design) - 1] = 1;

    // Only DOFs exclusively supported by design elements are editable.
    Array<int> design_incidence(level_set_fes->GetVSize());
    Array<int> fixed_incidence(level_set_fes->GetVSize());
    design_incidence = 0;
    fixed_incidence = 0;
    const int level_set_size = level_set_fes->GetTrueVSize();
    SparseMatrix design_to_cell(cell_count, level_set_size);
    SparseMatrix cell_to_level_set(level_set_size, cell_count);
    Array<int> element_dofs;
    Vector shape;
    for (int element = 0; element < cell_count; ++element) {
        level_set_fes->GetElementDofs(element, element_dofs);
        const FiniteElement* finite_element = level_set_fes->GetFE(element);
        const IntegrationPoint& reference_center =
            Geometries.GetCenter(mesh->GetElementBaseGeometry(element));
        shape.SetSize(finite_element->GetDof());
        finite_element->CalcShape(reference_center, shape);
        Array<int>& incidence = mesh->GetAttribute(element)
                == static_cast<int>(DomainAttribute::design)
            ? design_incidence
            : fixed_incidence;
        for (int i = 0; i < element_dofs.Size(); ++i) {
            const int dof = element_dofs[i];
            ++incidence[dof];
            design_to_cell.Add(element, dof, shape[i]);
            cell_to_level_set.Add(dof, element, 1.0);
        }
    }
    design_to_cell.Finalize();
    cell_to_level_set.Finalize();
    const int* cell_to_level_set_rows = cell_to_level_set.GetI();
    real_t* cell_to_level_set_values = cell_to_level_set.GetData();
    for (int dof = 0; dof < level_set_size; ++dof) {
        const int incidence = design_incidence[dof] + fixed_incidence[dof];
        for (int entry = cell_to_level_set_rows[dof];
             entry < cell_to_level_set_rows[dof + 1];
             ++entry) {
            cell_to_level_set_values[entry] /= incidence;
        }
    }
    Array<int> active_dofs;
    for (int dof = 0; dof < design_incidence.Size(); ++dof) {
        if (design_incidence[dof] > 0 && fixed_incidence[dof] == 0) {
            active_dofs.Append(dof);
        }
    }
    if (active_dofs.Size() == 0
        || active_dofs.Size() == level_set_fes->GetTrueVSize()) {
        log(LogLevel::Error,
            "The level-set space must contain active design and fixed-air DOFs.");
        return false;
    }
    lset.setActiveDesignDofs(active_dofs);
    if (!design_initialized) {
        FunctionCoefficient paper_initial_guess(
            [this, design_start](const Vector& position) {
                const double x = position[0] - design_start;
                const double y = position.Size() > 1 ? position[1] : 0.0;
                const double value = std::cos(
                    settings.initialPatternX * std::acos(-1.0) * x
                    / settings.initialPatternLx)
                    * std::cos(
                        settings.initialPatternY * std::acos(-1.0) * y
                        / settings.initialPatternLy)
                    + settings.initialPatternBias;
                return value >= settings.initialPatternThreshold ? 0.0 : 1.0;
            });
        GridFunction initial(level_set_fes.get());
        initial.ProjectCoefficient(paper_initial_guess);
        initial.GetTrueDofs(lset.design);
        lset.enforceDesignConstraints();
        design_initialized = true;
        log(LogLevel::Message,
            "Initialized the paper cosine level-set design.");
    }

    GridFunction unsmoothed_level_set(level_set_fes.get());
    unsmoothed_level_set.SetFromTrueDofs(lset.design);
    if (!smooth_level_set(
            unsmoothed_level_set,
            *lset.phi,
            design_to_cell,
            cell_to_level_set,
            element_centers,
            cell_volumes)) {
        return false;
    }


    const int displacement_size = displacement_fes->GetVSize();
    const int pressure_size = scalar_fes->GetVSize();

    const real_t lambda = dim == 3
        ? physics->youngs_modulus * physics->poisson_ratio
            / ((1.0 + physics->poisson_ratio)
               * (1.0 - 2.0 * physics->poisson_ratio))
        : physics->youngs_modulus * physics->poisson_ratio
            / (1.0 - physics->poisson_ratio * physics->poisson_ratio);
    const real_t mu = physics->youngs_modulus
        / (2.0 * (1.0 + physics->poisson_ratio));
    LevelSetScaledCoefficient solid_density(
        *lset.phi, physics->rho_s, physics->epsilon, true);
    LevelSetScaledCoefficient solid_lambda(
        *lset.phi, lambda, physics->epsilon, true);
    LevelSetScaledCoefficient solid_mu(
        *lset.phi, mu, physics->epsilon, true);
    LevelSetScaledCoefficient acoustic_mass(
        *lset.phi,
        1.0 / (physics->rho_a * physics->c_a * physics->c_a),
        physics->epsilon,
        false);
    LevelSetScaledCoefficient acoustic_stiffness(
        *lset.phi, 1.0 / physics->rho_a, physics->epsilon, false);

    // The fictitious-domain coefficients select solid or air at each
    // quadrature point. MFEM owns the element loops and sparse assembly.
    BilinearForm Muu_form(displacement_fes.get());
    Muu_form.AddDomainIntegrator(new VectorMassIntegrator(solid_density));
    Muu_form.Assemble();
    Muu_form.Finalize();
    std::unique_ptr<SparseMatrix> Muu(Muu_form.LoseMat());

    BilinearForm Kuu_form(displacement_fes.get());
    Kuu_form.AddDomainIntegrator(
        new ElasticityIntegrator(solid_lambda, solid_mu));
    Kuu_form.Assemble();
    Kuu_form.Finalize();
    std::unique_ptr<SparseMatrix> Kuu(Kuu_form.LoseMat());

    BilinearForm Mpp_form(scalar_fes.get());
    Mpp_form.AddDomainIntegrator(new MassIntegrator(acoustic_mass));
    Mpp_form.Assemble();
    Mpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpp(Mpp_form.LoseMat());

    BilinearForm Kpp_form(scalar_fes.get());
    Kpp_form.AddDomainIntegrator(new DiffusionIntegrator(acoustic_stiffness));
    Kpp_form.Assemble();
    Kpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Kpp(Kpp_form.LoseMat());

    MixedBilinearForm Kup_form(scalar_fes.get(), displacement_fes.get());
    Kup_form.AddDomainIntegrator(new ImplicitSurfaceNormalIntegrator(
        *lset.phi, cut_integration_order, level_set_order, -1.0, false),
        design_domain_marker);
    Kup_form.Assemble();
    Kup_form.Finalize();
    std::unique_ptr<SparseMatrix> Kup(Kup_form.LoseMat());

    MixedBilinearForm Mpu_form(displacement_fes.get(), scalar_fes.get());
    Mpu_form.AddDomainIntegrator(new ImplicitSurfaceNormalIntegrator(
        *lset.phi, cut_integration_order, level_set_order, 1.0, true),
        design_domain_marker);
    Mpu_form.Assemble();
    Mpu_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpu(Mpu_form.LoseMat());

    const real_t omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const real_t omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const real_t alpha_d = 2.0 * physics->zeta * omega_1 * omega_2
        / (omega_1 + omega_2);
    const real_t beta_d = 2.0 * physics->zeta / (omega_1 + omega_2);
    std::unique_ptr<SparseMatrix> Cuu(
        Add(alpha_d, *Muu, beta_d, *Kuu));

    Array<int> absorbing_marker(mesh->bdr_attributes.Max());
    absorbing_marker = 0;
    const int inlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::left)
        : static_cast<int>(CartesianBoundary2D::left);
    const int outlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::right)
        : static_cast<int>(CartesianBoundary2D::right);
    absorbing_marker[inlet_boundary - 1] = 1;
    absorbing_marker[outlet_boundary - 1] = 1;
    ConstantCoefficient inverse_impedance(
        1.0 / (physics->rho_a * physics->c_a));
    BilinearForm Cpp_form(scalar_fes.get());
    Cpp_form.AddBoundaryIntegrator(
        new BoundaryMassIntegrator(inverse_impedance), absorbing_marker);
    Cpp_form.Assemble();
    Cpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Cpp(Cpp_form.LoseMat());
    std::unique_ptr<SparseMatrix> Kup_transpose(Transpose(*Kup));
    std::unique_ptr<SparseMatrix> coupling_residual(
        Add(1.0, *Mpu, 1.0, *Kup_transpose));
    const real_t coupling_scale = std::max(
        real_t{1.0},
        std::max(Mpu->MaxNorm(), Kup_transpose->MaxNorm()));
    if (coupling_residual->MaxNorm() > 1.0e-10 * coupling_scale) {
        log(LogLevel::Error,
            "The implicit coupling matrices do not satisfy Mpu = -Kup^T.");
        return false;
    }

    Array<int> offsets(3);
    offsets[0] = 0;
    offsets[1] = displacement_size;
    offsets[2] = displacement_size + pressure_size;
    pressure_offset = displacement_size;
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
        log(LogLevel::Error, "The assembled global matrices contain non-finite values.");
        return false;
    }

    Array<int> clamped_marker(mesh->bdr_attributes.Max());
    clamped_marker = 0;
    if (dim == 3) {
        clamped_marker[static_cast<int>(CartesianBoundary3D::bottom) - 1] = 1;
        clamped_marker[static_cast<int>(CartesianBoundary3D::top) - 1] = 1;
    }
    else {
        clamped_marker[static_cast<int>(CartesianBoundary2D::bottom) - 1] = 1;
        clamped_marker[static_cast<int>(CartesianBoundary2D::top) - 1] = 1;
    }
    displacement_fes->GetEssentialTrueDofs(
        clamped_marker, displacement_essential_tdofs);

    Array<int> inlet_marker(mesh->bdr_attributes.Max());
    Array<int> outlet_marker(mesh->bdr_attributes.Max());
    inlet_marker = 0;
    outlet_marker = 0;
    inlet_marker[inlet_boundary - 1] = 1;
    outlet_marker[outlet_boundary - 1] = 1;
    ConstantCoefficient one(1.0);
    LinearForm inlet_form(scalar_fes.get());
    inlet_form.AddBoundaryIntegrator(
        new BoundaryLFIntegrator(one), inlet_marker);
    inlet_form.Assemble();
    inlet_load = inlet_form;
    LinearForm outlet_form(scalar_fes.get());
    outlet_form.AddBoundaryIntegrator(
        new BoundaryLFIntegrator(one), outlet_marker);
    outlet_form.Assemble();
    outlet_functional = outlet_form;

    const double expected_boundary_measure = dim == 3
        ? settings.sy * settings.sz
        : settings.sy;
    const double measure_tolerance = 1.0e-10
        * std::max(1.0, expected_boundary_measure);
    if (std::abs(inlet_load.Sum() - expected_boundary_measure) > measure_tolerance
        || std::abs(outlet_functional.Sum() - expected_boundary_measure)
            > measure_tolerance) {
        log(LogLevel::Error,
            "The inlet/outlet integration vectors do not match the duct cross-section.");
        return false;
    }

    std::mt19937 generator(settings.sourceSeed);
    std::uniform_real_distribution<double> distribution(
        -settings.sourceAmplitude, settings.sourceAmplitude);
    source_pressure.resize(time_steps + 1);
    source_pressure_derivative.resize(time_steps + 1);
    for (double& value : source_pressure) {
        value = distribution(generator);
    }
    source_pressure_derivative[0] =
        (source_pressure[1] - source_pressure[0]) / settings.dt;
    for (int step = 1; step < time_steps; ++step) {
        source_pressure_derivative[step] =
            (source_pressure[step + 1] - source_pressure[step - 1])
            / (2.0 * settings.dt);
    }
    source_pressure_derivative[time_steps] =
        (source_pressure[time_steps] - source_pressure[time_steps - 1])
        / settings.dt;

    result.stateSize = M->Height();
    result.displacementSize = displacement_size;
    result.pressureSize = pressure_size;
    result.pressureOffset = pressure_offset;
    result.timeSteps = time_steps;
    result.dt = settings.dt;
    log(LogLevel::Message,
        "Assembled fictitious-domain vibroacoustic M, C, and K matrices.");
    return true;
}

bool App::Solver::solve()
{
    status.store(SolverStatus::Working);
    result.success = 0;
    result.U.clear();
    result.residualNorms.clear();
    std::atomic_store(
        &result.inletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.outletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.referenceOutletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.materialImpulseResponse, std::shared_ptr<const SignalFFT>{});

    if (!mesh || !M || !C || !K) {
        log(LogLevel::Error,
            "Call setMesh() and assembleSolutionSpace() before solve().");
        status.store(SolverStatus::Error);
        return false;
    }

    const Vector designed_geometry(lset.design);
    auto fail = [this, &designed_geometry](SolverStatus failure) {
        lset.design = designed_geometry;
        lset.enforceDesignConstraints();
        result.U.clear();
        result.residualNorms.clear();
        status.store(failure);
        return false;
    };

    for (int analysis = 0; analysis < 2; ++analysis) {
        const bool reference_analysis = analysis == 0;
        if (reference_analysis) {
            lset.design = 0.0;
        }
        else {
            lset.design = designed_geometry;
        }
        lset.enforceDesignConstraints();
        if (!assembleSolutionSpace()) {
            return fail(SolverStatus::Error);
        }

        if (!reference_analysis) {
            char host[] = "127.0.0.1";
            socketstream stream(host, GlvisAdapter::Port);
            if (stream.good()) {
                stream.precision(8);
                stream << "solution\n" << *mesh << *lset.phi << std::flush;
                log(LogLevel::Message,
                    "Streamed the filtered paper initial geometry to GLVis.");
            }
            else {
                log(LogLevel::Warning,
                    "Could not connect to the local GLVis panel; continuing headless.");
            }
        }

        const auto* physics =
            std::get_if<VibroacousticSettings>(&settings.physics);
        const double beta = settings.newmarkBeta;
        const double gamma = settings.newmarkGamma;
        if (physics == nullptr || beta <= 0.0 || gamma < 0.5
            || beta < 0.25 * std::pow(gamma + 0.5, 2.0)) {
            log(LogLevel::Error,
                "The Newmark parameters do not satisfy the stability bound.");
            return fail(SolverStatus::Error);
        }

        // Paper Eqs. (14)-(19).
        const double a_1 = 1.0 - gamma / beta;
        const double a_2 =
            (1.0 - gamma / (2.0 * beta)) * settings.dt;
        const double a_3 = gamma / (beta * settings.dt);
        const double a_4 = 1.0 / (beta * settings.dt);
        const double a_5 = 1.0 / (2.0 * beta) - 1.0;
        const double a_6 =
            1.0 / (beta * settings.dt * settings.dt);

        std::unique_ptr<SparseMatrix> M_and_C(Add(a_6, *M, a_3, *C));
        std::unique_ptr<SparseMatrix> K_hat(Add(1.0, *K, 1.0, *M_and_C));
        for (int i = 0; i < displacement_essential_tdofs.Size(); ++i) {
            K_hat->EliminateRowCol(displacement_essential_tdofs[i]);
        }

        // ponytail: GSSmoother is the low-memory baseline; replace it with a
        // block preconditioner only if the paper-default residual gate fails.
        GSSmoother K_hat_preconditioner(*K_hat);
        GMRESSolver K_hat_solver;
        K_hat_solver.SetPreconditioner(K_hat_preconditioner);
        K_hat_solver.SetOperator(*K_hat);
        K_hat_solver.iterative_mode = true;
        K_hat_solver.SetKDim(50);
        K_hat_solver.SetRelTol(1.0e-10);
        K_hat_solver.SetAbsTol(1.0e-12);
        K_hat_solver.SetMaxIter(1500);
        K_hat_solver.SetPrintLevel(-1);

        const int state_size = M->Height();
        const int pressure_size = scalar_fes->GetVSize();
        const int time_steps = result.timeSteps;
        const double load_scale =
            2.0 / (physics->rho_a * physics->c_a);

        Vector v(state_size);
        Vector v_dot(state_size);
        Vector v_ddot(state_size);
        v = 0.0;
        v_dot = 0.0;
        v_ddot = 0.0;

        Vector h(state_size);
        h = 0.0;
        Vector pressure_h(h.GetData() + pressure_offset, pressure_size);
        pressure_h.Add(
            load_scale * source_pressure_derivative[0], inlet_load);
        // The load is not zero: only the clamped displacement rows are.
        h.SetSubVector(displacement_essential_tdofs, 0.0);

        // Paper Eq. (21): M v_ddot^0 = h^0.
        SparseMatrix M_system(*M);
        for (int i = 0; i < displacement_essential_tdofs.Size(); ++i) {
            M_system.EliminateRowCol(displacement_essential_tdofs[i]);
        }
        GSSmoother M_preconditioner(M_system);
        GMRESSolver M_solver;
        M_solver.SetPreconditioner(M_preconditioner);
        M_solver.SetOperator(M_system);
        M_solver.SetKDim(50);
        M_solver.SetRelTol(1.0e-10);
        M_solver.SetAbsTol(1.0e-12);
        M_solver.SetMaxIter(1500);
        M_solver.SetPrintLevel(-1);
        M_solver.Mult(h, v_ddot);
        if (!M_solver.GetConverged()) {
            log(LogLevel::Error,
                "The initial-acceleration solve did not converge.");
            return fail(SolverStatus::Diverged);
        }
        v_ddot.SetSubVector(displacement_essential_tdofs, 0.0);

        Vector R_0(state_size);
        M_system.Mult(v_ddot, R_0);
        R_0 -= h;
        const double initial_residual = R_0.Norml2()
            / std::max(1.0, h.Norml2());
        if (!std::isfinite(initial_residual)
            || initial_residual > 1.0e-9) {
            log(LogLevel::Error,
                "The initial-acceleration solve exceeded the residual tolerance.");
            return fail(SolverStatus::Diverged);
        }

        if (!reference_analysis) {
            result.U.reserve(time_steps + 1);
            result.residualNorms.reserve(time_steps + 1);
            Vector U_0(3 * state_size);
            U_0.SetVector(v, 0);
            U_0.SetVector(v_dot, state_size);
            U_0.SetVector(v_ddot, 2 * state_size);
            result.U.push_back(U_0);
            result.residualNorms.push_back(
                {initial_residual, 0.0, 0.0});
        }

        std::vector<double>& measured_outlet = reference_analysis
            ? reference_outlet_pressure
            : outlet_pressure;
        measured_outlet.clear();
        measured_outlet.reserve(time_steps);

        Vector h_hat(state_size);
        Vector x_M(state_size);
        Vector x_C(state_size);
        Vector y_M(state_size);
        Vector y_C(state_size);
        Vector v_n(state_size);
        Vector v_dot_n(state_size);
        Vector v_ddot_n(state_size);
        Vector delta_v(state_size);
        Vector K_v(state_size);
        Vector R_1(state_size);
        Vector R_2(state_size);
        Vector R_3(state_size);

        for (int n = 1; n <= time_steps; ++n) {
            h = 0.0;
            pressure_h.SetDataAndSize(
                h.GetData() + pressure_offset, pressure_size);
            pressure_h.Add(
                load_scale * source_pressure_derivative[n], inlet_load);

            x_M = 0.0;
            x_M.Add(a_4, v_dot);
            x_M.Add(a_5, v_ddot);
            x_M.Add(a_6, v);
            M->Mult(x_M, y_M);

            x_C = 0.0;
            x_C.Add(-a_1, v_dot);
            x_C.Add(-a_2, v_ddot);
            x_C.Add(a_3, v);
            C->Mult(x_C, y_C);

            h_hat = h;
            h_hat += y_M;
            h_hat += y_C;
            // EliminateRowCol() imposes v = 0 on these rows, so their RHS is 0.
            h_hat.SetSubVector(displacement_essential_tdofs, 0.0);

            v_n = v;
            K_hat_solver.Mult(h_hat, v_n);
            if (!K_hat_solver.GetConverged()) {
                log(LogLevel::Error,
                    "The Newmark solve failed at time step "
                        + std::to_string(n) + ".");
                return fail(SolverStatus::Diverged);
            }
            v_n.SetSubVector(displacement_essential_tdofs, 0.0);

            delta_v = v_n;
            delta_v -= v;
            v_dot_n = 0.0;
            v_dot_n.Add(a_1, v_dot);
            v_dot_n.Add(a_2, v_ddot);
            v_dot_n.Add(a_3, delta_v);
            v_ddot_n = 0.0;
            v_ddot_n.Add(-a_4, v_dot);
            v_ddot_n.Add(-a_5, v_ddot);
            v_ddot_n.Add(a_6, delta_v);
            v_dot_n.SetSubVector(displacement_essential_tdofs, 0.0);
            v_ddot_n.SetSubVector(displacement_essential_tdofs, 0.0);

            Vector pressure(
                v_n.GetData() + pressure_offset, pressure_size);
            measured_outlet.push_back(outlet_functional * pressure);

            M->Mult(v_ddot_n, y_M);
            C->Mult(v_dot_n, y_C);
            K->Mult(v_n, K_v);
            R_1 = y_M;
            R_1 += y_C;
            R_1 += K_v;
            R_1 -= h;
            R_1.SetSubVector(displacement_essential_tdofs, 0.0);

            R_2 = v_dot_n;
            R_2.Add(-a_1, v_dot);
            R_2.Add(-a_2, v_ddot);
            R_2.Add(-a_3, delta_v);

            R_3 = v_ddot_n;
            R_3.Add(a_4, v_dot);
            R_3.Add(a_5, v_ddot);
            R_3.Add(-a_6, delta_v);

            const double R_1_norm = R_1.Norml2() / std::max({
                1.0,
                h.Norml2(),
                y_M.Norml2() + y_C.Norml2() + K_v.Norml2()
            });
            const double R_2_norm = R_2.Norml2() / std::max({
                1.0,
                v_dot_n.Norml2(),
                std::abs(a_1) * v_dot.Norml2()
                    + std::abs(a_2) * v_ddot.Norml2()
                    + std::abs(a_3) * delta_v.Norml2()
            });
            const double R_3_norm = R_3.Norml2() / std::max({
                1.0,
                v_ddot_n.Norml2(),
                std::abs(a_4) * v_dot.Norml2()
                    + std::abs(a_5) * v_ddot.Norml2()
                    + std::abs(a_6) * delta_v.Norml2()
            });
            if (!std::isfinite(R_1_norm)
                || !std::isfinite(R_2_norm)
                || !std::isfinite(R_3_norm)
                || R_1_norm > 1.0e-9) {
                log(LogLevel::Error,
                    "The Newmark residual check failed at time step "
                        + std::to_string(n) + ".");
                return fail(SolverStatus::Diverged);
            }

            if (!reference_analysis) {
                Vector U_n(3 * state_size);
                U_n.SetVector(v_n, 0);
                U_n.SetVector(v_dot_n, state_size);
                U_n.SetVector(v_ddot_n, 2 * state_size);
                result.U.push_back(U_n);
                result.residualNorms.push_back(
                    {R_1_norm, R_2_norm, R_3_norm});
            }

            v = v_n;
            v_dot = v_dot_n;
            v_ddot = v_ddot_n;
        }

        if (reference_analysis) {
            log(LogLevel::Message,
                "Computed the deterministic empty-duct reference response.");
        }
    }

    lset.design = designed_geometry;
    lset.enforceDesignConstraints();
    if (!postprocessFourierResponse()) {
        return fail(SolverStatus::Error);
    }

    result.success = 1;
    status.store(SolverStatus::Converged);
    log(LogLevel::Message,
        "Completed the Newmark solve and published the outlet transmission FFT.");
    return true;
}

bool App::Solver::smooth_level_set(
    const GridFunction& level_set,
    GridFunction& smoothed_level_set,
    const SparseMatrix& design_to_cell,
    const SparseMatrix& cell_to_level_set,
    const DenseMatrix& element_centers,
    const Vector& cell_volumes)
{
    const int cell_count = mesh->GetNE();
    const int dim = mesh->Dimension();
    const int level_set_size = level_set_fes->GetTrueVSize();
    if (level_set.FESpace() != level_set_fes.get()
        || smoothed_level_set.FESpace() != level_set_fes.get()
        || design_to_cell.Height() != cell_count
        || design_to_cell.Width() != level_set_size
        || cell_to_level_set.Height() != level_set_size
        || cell_to_level_set.Width() != cell_count
        || element_centers.Height() != cell_count
        || element_centers.Width() != dim
        || cell_volumes.Size() != cell_count) {
        log(LogLevel::Error,
            "The level-set smoother received incompatible mesh data.");
        return false;
    }

    const double sx = settings.inletLength
        + settings.designLength + settings.outletLength;
    const double hx = sx / settings.nx;
    const double hy = settings.sy / settings.ny;
    const double hz = settings.nz > 0
        ? settings.sz / settings.nz
        : std::numeric_limits<double>::max();
    const double level_set_scale = std::min({hx, hy, hz});

    // Paper Eq. (28): -r^2 Laplacian(phi_c) + phi_c = mapped_design_c.
    const real_t filter_radius = optimizer_settings.filterRadius;
    if (filter_radius < 0.0) {
        log(LogLevel::Error, "The PDE filter radius cannot be negative.");
        return false;
    }
    SparseMatrix filter_matrix(cell_count);
    for (int element = 0; element < cell_count; ++element) {
        filter_matrix.Add(element, element, cell_volumes[element]);
    }
    for (int face = 0; face < mesh->GetNumFaces(); ++face) {
        int first = -1;
        int second = -1;
        mesh->GetFaceElements(face, &first, &second);
        if (second < 0
            || mesh->GetAttribute(first)
                != static_cast<int>(DomainAttribute::design)
            || mesh->GetAttribute(second)
                != static_cast<int>(DomainAttribute::design)) {
            continue;
        }
        real_t distance_squared = 0.0;
        for (int axis = 0; axis < dim; ++axis) {
            const real_t distance = element_centers(second, axis)
                - element_centers(first, axis);
            distance_squared += distance * distance;
        }
        if (distance_squared <= 0.0) {
            log(LogLevel::Error,
                "The PDE filter found coincident cell centers.");
            return false;
        }
        const real_t coefficient = filter_radius * filter_radius
            * 0.5 * (cell_volumes[first] + cell_volumes[second])
            / distance_squared;
        filter_matrix.Add(first, first, coefficient);
        filter_matrix.Add(first, second, -coefficient);
        filter_matrix.Add(second, first, -coefficient);
        filter_matrix.Add(second, second, coefficient);
    }
    filter_matrix.Finalize();

    Vector mapped;
    level_set.GetTrueDofs(mapped);
    mapped -= 0.5;
    mapped *= level_set_scale;
    Vector center_values(cell_count);
    Vector filter_rhs(cell_count);
    Vector filtered_centers(cell_count);
    design_to_cell.Mult(mapped, center_values);
    for (int cell = 0; cell < cell_count; ++cell) {
        filter_rhs[cell] = cell_volumes[cell] * center_values[cell];
    }

    GSSmoother preconditioner(filter_matrix);
    CGSolver solver;
    solver.SetPreconditioner(preconditioner);
    solver.SetOperator(filter_matrix);
    solver.SetRelTol(1.0e-10);
    solver.SetAbsTol(1.0e-12);
    solver.SetMaxIter(1500);
    solver.SetPrintLevel(-1);
    filtered_centers = 0.0;
    solver.Mult(filter_rhs, filtered_centers);
    if (!solver.GetConverged()) {
        log(LogLevel::Error,
            "The cell-centered PDE filter did not converge.");
        return false;
    }

    Vector physical(level_set_size);
    cell_to_level_set.Mult(filtered_centers, physical);
    smoothed_level_set.SetFromTrueDofs(physical);
    smoothed_level_set.SetSubVectorComplement(
        lset.activeDesignDofs, -0.5 * level_set_scale);
    return true;
}

bool App::Solver::postprocessFourierResponse()
{
    const int sample_count = static_cast<int>(outlet_pressure.size());
    if (sample_count == 0
        || reference_outlet_pressure.size() != outlet_pressure.size()) {
        return false;
    }

    std::vector<double> window(sample_count);
    for (int sample = 0; sample < sample_count; ++sample) {
        window[sample] = settings.useHannWindow
            ? 0.5 * (1.0 - std::cos(
                2.0 * std::acos(-1.0) * sample / (sample_count - 1)))
            : 1.0;
    }

    auto transform = [&window, sample_count](
                         const std::vector<double>& signal,
                         std::vector<std::complex<double>>& spectrum) {
        std::vector<double> windowed(sample_count);
        for (int sample = 0; sample < sample_count; ++sample) {
            windowed[sample] = signal[sample] * window[sample];
        }

        fftw_complex* output = fftw_alloc_complex(sample_count / 2 + 1);
        if (output == nullptr) {
            return false;
        }
        fftw_plan plan = fftw_plan_dft_r2c_1d(
            sample_count, windowed.data(), output, FFTW_ESTIMATE);
        if (plan == nullptr) {
            fftw_free(output);
            return false;
        }
        fftw_execute(plan);
        spectrum.resize(sample_count / 2 + 1);
        for (int bin = 0; bin < static_cast<int>(spectrum.size()); ++bin) {
            spectrum[bin] = {output[bin][0], output[bin][1]};
        }
        fftw_destroy_plan(plan);
        fftw_free(output);
        return true;
    };

    std::vector<std::complex<double>> response_spectrum;
    std::vector<std::complex<double>> reference_spectrum;
    if (!transform(outlet_pressure, response_spectrum)
        || !transform(reference_outlet_pressure, reference_spectrum)) {
        log(LogLevel::Error, "FFTW could not produce the frequency response.");
        return false;
    }

    auto response = std::make_shared<SignalFFT>();
    response->size = static_cast<int>(response_spectrum.size());
    response->frequency.resize(response->size);
    response->referenceAmplitude.resize(response->size);
    response->amplitude.resize(response->size);
    response->transmission.resize(response->size);
    response->attenuationDB.resize(response->size);
    response->phase.resize(response->size);
    response->valid.resize(response->size);

    double maximum_reference = 0.0;
    for (const std::complex<double>& value : reference_spectrum) {
        maximum_reference = std::max(maximum_reference, std::abs(value));
    }
    const double reference_floor =
        std::max(1.0e-14, 1.0e-12 * maximum_reference);
    const double window_sum =
        std::accumulate(window.begin(), window.end(), 0.0);
    if (window_sum <= 0.0) {
        log(LogLevel::Error, "The FFT window has zero total weight.");
        return false;
    }

    const bool has_nyquist_bin = sample_count % 2 == 0;
    const float invalid_value = std::numeric_limits<float>::quiet_NaN();
    for (int bin = 0; bin < response->size; ++bin) {
        const double reference_amplitude = std::abs(reference_spectrum[bin]);
        const double amplitude = std::abs(response_spectrum[bin]);
        const bool valid = reference_amplitude > reference_floor;
        const double one_sided_scale = bin == 0
                || (has_nyquist_bin && bin == response->size - 1)
            ? 1.0 / window_sum
            : 2.0 / window_sum;

        response->frequency[bin] = static_cast<float>(
            bin / (sample_count * settings.dt));
        response->referenceAmplitude[bin] = static_cast<float>(
            reference_amplitude * one_sided_scale);
        response->amplitude[bin] = static_cast<float>(
            amplitude * one_sided_scale);
        response->valid[bin] = valid ? 1 : 0;
        if (!valid) {
            response->transmission[bin] = invalid_value;
            response->attenuationDB[bin] = invalid_value;
            response->phase[bin] = invalid_value;
            continue;
        }

        const double transmission = amplitude / reference_amplitude;
        response->transmission[bin] = static_cast<float>(transmission);
        response->attenuationDB[bin] = static_cast<float>(
            20.0 * std::log10(std::max(transmission, 1.0e-12)));
        response->phase[bin] = static_cast<float>(
            std::arg(response_spectrum[bin] / reference_spectrum[bin]));
    }

    auto make_signal = [this, sample_count](
                           const std::vector<double>& amplitude,
                           bool skip_initial_sample) {
        auto signal = std::make_shared<SignalTD>();
        signal->size = sample_count;
        signal->time.resize(sample_count);
        signal->amplitude.resize(sample_count);
        for (int sample = 0; sample < sample_count; ++sample) {
            signal->time[sample] = (sample + 1) * settings.dt;
            signal->amplitude[sample] =
                amplitude[sample + (skip_initial_sample ? 1 : 0)];
        }
        return std::shared_ptr<const SignalTD>(std::move(signal));
    };

    std::shared_ptr<const SignalFFT> published_response = std::move(response);
    std::atomic_store(
        &result.materialImpulseResponse, std::move(published_response));
    std::atomic_store(
        &result.inletPressure, make_signal(source_pressure, true));
    std::atomic_store(
        &result.outletPressure, make_signal(outlet_pressure, false));
    std::atomic_store(
        &result.referenceOutletPressure,
        make_signal(reference_outlet_pressure, false));
    return true;
}
