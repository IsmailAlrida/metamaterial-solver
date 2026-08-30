#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
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

const App::FrequencyResponse& App::Solver::frequencyResponse() const
{
    return frequency_response;
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
    effective_matrix_transpose.reset();
    initial_matrix_transpose.reset();
    design_to_cell.reset();
    cell_to_level_set.reset();
    filter_matrix.reset();
    inlet_load.SetSize(0);
    outlet_functional.SetSize(0);
    element_centers.SetSize(0, 0);
    cell_volumes.SetSize(0);
    displacement_essential_tdofs.SetSize(0);
    source_pressure.clear();
    source_pressure_derivative.clear();
    level_set_scale = 0.0;
    design_initialized = false;
    reference_ready = false;
    reference_outlet_pressure.clear();
    fft_window.clear();
    frequency_response = {};
    result.success = 0;
    std::atomic_store(
        &result.inletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.outletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.referenceOutletPressure, std::shared_ptr<const SignalTD>{});
    std::atomic_store(
        &result.materialImpulseResponse, std::shared_ptr<const SignalFFT>{});
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
        || physics->epsilon > 1.0
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
    effective_matrix_transpose.reset();
    initial_matrix_transpose.reset();
    if (!level_set_fes) {
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
        element_centers.SetSize(cell_count, dim);
        cell_volumes.SetSize(cell_count);
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

        // Only DOFs exclusively supported by design elements are editable.
        Array<int> design_incidence(level_set_fes->GetVSize());
        Array<int> fixed_incidence(level_set_fes->GetVSize());
        design_incidence = 0;
        fixed_incidence = 0;
        const int level_set_size = level_set_fes->GetTrueVSize();
        design_to_cell = std::make_unique<SparseMatrix>(
            cell_count, level_set_size);
        cell_to_level_set = std::make_unique<SparseMatrix>(
            level_set_size, cell_count);
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
                design_to_cell->Add(element, dof, shape[i]);
                cell_to_level_set->Add(dof, element, 1.0);
            }
        }
        design_to_cell->Finalize();
        cell_to_level_set->Finalize();
        const int* cell_to_level_set_rows = cell_to_level_set->GetI();
        real_t* cell_to_level_set_values = cell_to_level_set->GetData();
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
    }

    Array<int> design_domain_marker(mesh->attributes.Max());
    Array<int> fixed_air_marker(mesh->attributes.Max());
    design_domain_marker = 0;
    fixed_air_marker = 0;
    design_domain_marker[static_cast<int>(DomainAttribute::design) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::inlet) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::outlet) - 1] = 1;

    GridFunction unsmoothed_level_set(level_set_fes.get());
    unsmoothed_level_set.SetFromTrueDofs(lset.design);
    if (!smooth_level_set(unsmoothed_level_set, *lset.phi)) {
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
    ConstantCoefficient solid_density(physics->rho_s);
    ConstantCoefficient solid_lambda(lambda);
    ConstantCoefficient solid_mu(mu);
    ConstantCoefficient fictitious_solid_density(
        physics->epsilon * physics->rho_s);
    ConstantCoefficient fictitious_solid_lambda(physics->epsilon * lambda);
    ConstantCoefficient fictitious_solid_mu(physics->epsilon * mu);
    ConstantCoefficient acoustic_mass(
        1.0 / (physics->rho_a * physics->c_a * physics->c_a));
    ConstantCoefficient acoustic_stiffness(1.0 / physics->rho_a);

    // Algoim supplies the cut-volume rules while the ordinary MFEM
    // integrators and forms own the element loops and sparse assembly.
    BilinearForm Muu_form(displacement_fes.get());
    Muu_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<VectorMassIntegrator>(solid_density),
        *lset.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        true), design_domain_marker);
    Muu_form.AddDomainIntegrator(
        new VectorMassIntegrator(fictitious_solid_density), fixed_air_marker);
    Muu_form.Assemble();
    Muu_form.Finalize();
    std::unique_ptr<SparseMatrix> Muu(Muu_form.LoseMat());

    BilinearForm Kuu_form(displacement_fes.get());
    Kuu_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<ElasticityIntegrator>(solid_lambda, solid_mu),
        *lset.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        true), design_domain_marker);
    Kuu_form.AddDomainIntegrator(new ElasticityIntegrator(
        fictitious_solid_lambda, fictitious_solid_mu), fixed_air_marker);
    Kuu_form.Assemble();
    Kuu_form.Finalize();
    std::unique_ptr<SparseMatrix> Kuu(Kuu_form.LoseMat());

    BilinearForm Mpp_form(scalar_fes.get());
    Mpp_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<MassIntegrator>(acoustic_mass),
        *lset.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        false), design_domain_marker);
    Mpp_form.AddDomainIntegrator(
        new MassIntegrator(acoustic_mass), fixed_air_marker);
    Mpp_form.Assemble();
    Mpp_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpp(Mpp_form.LoseMat());

    BilinearForm Kpp_form(scalar_fes.get());
    Kpp_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<DiffusionIntegrator>(acoustic_stiffness),
        *lset.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        false), design_domain_marker);
    Kpp_form.AddDomainIntegrator(
        new DiffusionIntegrator(acoustic_stiffness), fixed_air_marker);
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

    if (inlet_load.Size() == 0) {
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
        if (std::abs(inlet_load.Sum() - expected_boundary_measure)
                > measure_tolerance
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
    }

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
    frequency_response = {};

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
        status.store(failure);
        return false;
    };

    const int first_analysis = reference_ready ? 1 : 0;
    bool designed_assembly_ready = true;
    for (int analysis = first_analysis; analysis < 2; ++analysis) {
        const bool reference_analysis = analysis == 0;
        if (reference_analysis) {
            reference_ready = false;
            lset.design = 0.0;
        }
        else {
            lset.design = designed_geometry;
        }
        lset.enforceDesignConstraints();
        if ((reference_analysis || !designed_assembly_ready)
            && !assembleSolutionSpace()) {
            return fail(SolverStatus::Error);
        }
        designed_assembly_ready = !reference_analysis;

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
        K_hat->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);

        // ponytail: GSSmoother is the low-memory baseline; replace it with a
        // block preconditioner only if the paper-default residual gate fails.
        GSSmoother K_hat_preconditioner(*K_hat);
        FGMRESSolver K_hat_solver;
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
        log(LogLevel::Message,
            std::string("Solving the ")
                + (reference_analysis ? "empty-duct reference" : "designed duct")
                + " transient (" + std::to_string(state_size) + " unknowns, "
                + std::to_string(time_steps) + " time steps).");

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
        M_system.EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);
        GSSmoother M_preconditioner(M_system);
        FGMRESSolver M_solver;
        M_solver.SetPreconditioner(M_preconditioner);
        M_solver.SetOperator(M_system);
        M_solver.SetKDim(50);
        M_solver.SetRelTol(1.0e-10);
        M_solver.SetAbsTol(1.0e-12);
        M_solver.SetMaxIter(1500);
        M_solver.SetPrintLevel(-1);
        M_solver.Mult(h, v_ddot);
        if (!M_solver.GetConverged()) {
            std::ostringstream message;
            message << "The initial-acceleration solve did not converge: "
                    << M_solver.GetNumIterations() << " FGMRES iterations, "
                    << "relative residual " << std::scientific
                    << M_solver.GetFinalRelNorm() << ".";
            log(LogLevel::Error, message.str());
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
            std::ostringstream message;
            message << "The initial-acceleration residual gate failed: physical "
                    << "relative residual " << std::scientific
                    << initial_residual << ", FGMRES relative residual "
                    << M_solver.GetFinalRelNorm() << ", "
                    << M_solver.GetNumIterations() << " FGMRES iterations "
                    << "(limit 1.000000e-09).";
            log(LogLevel::Error, message.str());
            return fail(SolverStatus::Diverged);
        }

        if (!reference_analysis) {
            result.U.resize(time_steps + 1);
            result.residualNorms.resize(time_steps + 1);
            for (Vector& state : result.U) {
                state.SetSize(3 * state_size);
            }
            Vector& U_0 = result.U[0];
            U_0.SetVector(v, 0);
            U_0.SetVector(v_dot, state_size);
            U_0.SetVector(v_ddot, 2 * state_size);
            result.residualNorms[0] = {initial_residual, 0.0, 0.0};
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
        Vector linear_residual(state_size);
        double maximum_equilibrium_residual = initial_residual;
        double maximum_velocity_residual = 0.0;
        double maximum_acceleration_residual = 0.0;
        double maximum_linear_residual = initial_residual;
        double maximum_solver_residual = M_solver.GetFinalRelNorm();
        int maximum_fgmres_iterations = M_solver.GetNumIterations();

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
            // The essential boundary rows impose v = 0, so their RHS is 0.
            h_hat.SetSubVector(displacement_essential_tdofs, 0.0);

            v_n = v;
            K_hat_solver.Mult(h_hat, v_n);
            if (!K_hat_solver.GetConverged()) {
                std::ostringstream message;
                message << "The Newmark linear solve failed at time step " << n
                        << ": " << K_hat_solver.GetNumIterations()
                        << " FGMRES iterations, relative residual "
                        << std::scientific << K_hat_solver.GetFinalRelNorm()
                        << ".";
                log(LogLevel::Error, message.str());
                return fail(SolverStatus::Diverged);
            }
            v_n.SetSubVector(displacement_essential_tdofs, 0.0);

            K_hat->Mult(v_n, linear_residual);
            linear_residual -= h_hat;
            const double linear_residual_norm = linear_residual.Norml2()
                / std::max(1.0, h_hat.Norml2());

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
            if (!std::isfinite(linear_residual_norm)
                || !std::isfinite(R_1_norm)
                || !std::isfinite(R_2_norm)
                || !std::isfinite(R_3_norm)
                || linear_residual_norm > 1.0e-9
                || R_1_norm > 1.0e-9
                || R_2_norm > 1.0e-9
                || R_3_norm > 1.0e-9) {
                std::ostringstream message;
                message << "The Newmark residual gate failed at time step " << n
                        << ": linear=" << std::scientific
                        << linear_residual_norm << ", equilibrium=" << R_1_norm
                        << ", velocity=" << R_2_norm
                        << ", acceleration=" << R_3_norm
                        << ", FGMRES="
                        << K_hat_solver.GetFinalRelNorm() << ", iterations="
                        << K_hat_solver.GetNumIterations()
                        << " (limit 1.000000e-09).";
                log(LogLevel::Error, message.str());
                return fail(SolverStatus::Diverged);
            }

            maximum_linear_residual = std::max(
                maximum_linear_residual, linear_residual_norm);
            maximum_equilibrium_residual = std::max(
                maximum_equilibrium_residual, R_1_norm);
            maximum_velocity_residual = std::max(
                maximum_velocity_residual, R_2_norm);
            maximum_acceleration_residual = std::max(
                maximum_acceleration_residual, R_3_norm);
            maximum_solver_residual = std::max(
                maximum_solver_residual,
                K_hat_solver.GetFinalRelNorm());
            maximum_fgmres_iterations = std::max(
                maximum_fgmres_iterations,
                K_hat_solver.GetNumIterations());

            if (!reference_analysis) {
                Vector& U_n = result.U[n];
                U_n.SetVector(v_n, 0);
                U_n.SetVector(v_dot_n, state_size);
                U_n.SetVector(v_ddot_n, 2 * state_size);
                result.residualNorms[n] = {
                    R_1_norm, R_2_norm, R_3_norm};
            }

            v = v_n;
            v_dot = v_dot_n;
            v_ddot = v_ddot_n;
        }

        std::ostringstream summary;
        summary << "Completed the "
                << (reference_analysis ? "empty-duct reference" : "designed duct")
                << " transient: maximum linear/equilibrium/velocity/acceleration "
                << "residuals = " << std::scientific
                << maximum_linear_residual << " / "
                << maximum_equilibrium_residual << " / "
                << maximum_velocity_residual << " / "
                << maximum_acceleration_residual
                << ", maximum FGMRES residual = "
                << maximum_solver_residual
                << ", maximum FGMRES iterations = "
                << maximum_fgmres_iterations << ".";
        log(LogLevel::Message, summary.str());

        if (reference_analysis) {
            reference_ready = true;
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
    GridFunction& smoothed_level_set)
{
    const int cell_count = mesh->GetNE();
    const int dim = mesh->Dimension();
    const int level_set_size = level_set_fes->GetTrueVSize();
    if (!design_to_cell || !cell_to_level_set
        || level_set.FESpace() != level_set_fes.get()
        || smoothed_level_set.FESpace() != level_set_fes.get()
        || design_to_cell->Height() != cell_count
        || design_to_cell->Width() != level_set_size
        || cell_to_level_set->Height() != level_set_size
        || cell_to_level_set->Width() != cell_count
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
    level_set_scale = std::min({hx, hy, hz});

    // Paper Eq. (28): -r^2 Laplacian(phi_c) + phi_c = mapped_design_c.
    const real_t filter_radius = optimizer_settings.filterRadius;
    if (filter_radius < 0.0) {
        log(LogLevel::Error, "The PDE filter radius cannot be negative.");
        return false;
    }
    if (!filter_matrix) {
        filter_matrix = std::make_unique<SparseMatrix>(cell_count);
        for (int element = 0; element < cell_count; ++element) {
            filter_matrix->Add(element, element, cell_volumes[element]);
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
            filter_matrix->Add(first, first, coefficient);
            filter_matrix->Add(first, second, -coefficient);
            filter_matrix->Add(second, first, -coefficient);
            filter_matrix->Add(second, second, coefficient);
        }
        filter_matrix->Finalize();
    }

    Vector mapped;
    level_set.GetTrueDofs(mapped);
    mapped -= 0.5;
    mapped *= level_set_scale;
    Vector center_values(cell_count);
    Vector filter_rhs(cell_count);
    Vector filtered_centers(cell_count);
    design_to_cell->Mult(mapped, center_values);
    for (int cell = 0; cell < cell_count; ++cell) {
        filter_rhs[cell] = cell_volumes[cell] * center_values[cell];
    }

    GSSmoother preconditioner(*filter_matrix);
    CGSolver solver;
    solver.SetPreconditioner(preconditioner);
    solver.SetOperator(*filter_matrix);
    solver.SetRelTol(1.0e-10);
    solver.SetAbsTol(1.0e-12);
    solver.SetMaxIter(1500);
    solver.SetPrintLevel(-1);
    filtered_centers = 0.0;
    solver.Mult(filter_rhs, filtered_centers);
    if (!solver.GetConverged()) {
        std::ostringstream message;
        message << "The cell-centered PDE filter did not converge: "
                << solver.GetNumIterations() << " CG iterations, relative residual "
                << std::scientific << solver.GetFinalRelNorm() << ".";
        log(LogLevel::Error, message.str());
        return false;
    }

    Vector physical(level_set_size);
    cell_to_level_set->Mult(filtered_centers, physical);
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

    fft_window.resize(sample_count);
    for (int sample = 0; sample < sample_count; ++sample) {
        fft_window[sample] = settings.useHannWindow
            ? 0.5 * (1.0 - std::cos(
                2.0 * std::acos(-1.0) * sample / (sample_count - 1)))
            : 1.0;
    }

    auto transform = [this, sample_count](
                         const std::vector<double>& signal,
                         std::vector<std::complex<double>>& spectrum) {
        std::vector<double> windowed(sample_count);
        for (int sample = 0; sample < sample_count; ++sample) {
            windowed[sample] = signal[sample] * fft_window[sample];
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
        std::accumulate(fft_window.begin(), fft_window.end(), 0.0);
    if (window_sum <= 0.0) {
        log(LogLevel::Error, "The FFT window has zero total weight.");
        return false;
    }

    const bool has_nyquist_bin = sample_count % 2 == 0;
    const float invalid_value = std::numeric_limits<float>::quiet_NaN();
    frequency_response.frequency.resize(response->size);
    frequency_response.outlet = response_spectrum;
    frequency_response.reference = reference_spectrum;
    frequency_response.valid.resize(response->size);
    for (int bin = 0; bin < response->size; ++bin) {
        const double reference_amplitude = std::abs(reference_spectrum[bin]);
        const double amplitude = std::abs(response_spectrum[bin]);
        const bool valid = reference_amplitude > reference_floor;
        const double one_sided_scale = bin == 0
                || (has_nyquist_bin && bin == response->size - 1)
            ? 1.0 / window_sum
            : 2.0 / window_sum;

        const double frequency = bin / (sample_count * settings.dt);
        frequency_response.frequency[bin] = frequency;
        frequency_response.valid[bin] = valid ? 1 : 0;
        response->frequency[bin] = static_cast<float>(frequency);
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

bool App::Solver::differentiateFrequencyResponse(
    const std::vector<std::complex<double>>& spectrum_derivative,
    Vector& design_gradient)
{
    const int time_steps = result.timeSteps;
    const int state_size = result.stateSize;
    const int displacement_size = result.displacementSize;
    const int pressure_size = result.pressureSize;
    if (!mesh || mesh->Dimension() != 2 || !M || !C || !K
        || !design_to_cell || !cell_to_level_set || !filter_matrix
        || time_steps <= 0 || state_size <= 0
        || result.U.size() != static_cast<std::size_t>(time_steps + 1)
        || fft_window.size() != static_cast<std::size_t>(time_steps)
        || spectrum_derivative.size() != frequency_response.outlet.size()
        || spectrum_derivative.size()
            != static_cast<std::size_t>(time_steps / 2 + 1)) {
        log(LogLevel::Error,
            "The forward history is incomplete for the discrete adjoint.");
        return false;
    }
    for (const Vector& state : result.U) {
        if (state.Size() != 3 * state_size) {
            log(LogLevel::Error,
                "A stored Newmark state has an invalid block layout.");
            return false;
        }
    }

    // The complex input stores dPhi/dRe + i*dPhi/dIm. FFTW's unnormalized
    // C2R transform applies the transpose of the unnormalized forward R2C
    // transform after the interior bins are halved.
    fftw_complex* fft_derivative = fftw_alloc_complex(
        spectrum_derivative.size());
    if (fft_derivative == nullptr) {
        log(LogLevel::Error,
            "FFTW could not allocate the adjoint frequency buffer.");
        return false;
    }
    const bool has_nyquist_bin = time_steps % 2 == 0;
    for (int bin = 0; bin < static_cast<int>(spectrum_derivative.size()); ++bin) {
        const bool single_sided = bin == 0
            || (has_nyquist_bin
                && bin == static_cast<int>(spectrum_derivative.size()) - 1);
        const double scale = single_sided ? 1.0 : 0.5;
        fft_derivative[bin][0] = scale * spectrum_derivative[bin].real();
        fft_derivative[bin][1] = single_sided
            ? 0.0
            : scale * spectrum_derivative[bin].imag();
    }
    std::vector<double> outlet_derivative(time_steps);
    fftw_plan inverse_plan = fftw_plan_dft_c2r_1d(
        time_steps,
        fft_derivative,
        outlet_derivative.data(),
        FFTW_ESTIMATE);
    if (inverse_plan == nullptr) {
        fftw_free(fft_derivative);
        log(LogLevel::Error,
            "FFTW could not create the adjoint transform.");
        return false;
    }
    fftw_execute(inverse_plan);
    fftw_destroy_plan(inverse_plan);
    fftw_free(fft_derivative);
    for (int sample = 0; sample < time_steps; ++sample) {
        outlet_derivative[sample] *= fft_window[sample];
    }

    const double beta = settings.newmarkBeta;
    const double gamma = settings.newmarkGamma;
    const double a_1 = 1.0 - gamma / beta;
    const double a_2 =
        (1.0 - gamma / (2.0 * beta)) * settings.dt;
    const double a_3 = gamma / (beta * settings.dt);
    const double a_4 = 1.0 / (beta * settings.dt);
    const double a_5 = 1.0 / (2.0 * beta) - 1.0;
    const double a_6 = 1.0 / (beta * settings.dt * settings.dt);

    if (!effective_matrix_transpose) {
        std::unique_ptr<SparseMatrix> M_and_C(Add(a_6, *M, a_3, *C));
        std::unique_ptr<SparseMatrix> K_hat(Add(1.0, *K, 1.0, *M_and_C));
        K_hat->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);
        effective_matrix_transpose.reset(Transpose(*K_hat));
    }
    GSSmoother K_hat_preconditioner(*effective_matrix_transpose);
    FGMRESSolver K_hat_solver;
    K_hat_solver.SetPreconditioner(K_hat_preconditioner);
    K_hat_solver.SetOperator(*effective_matrix_transpose);
    K_hat_solver.iterative_mode = true;
    K_hat_solver.SetKDim(50);
    K_hat_solver.SetRelTol(1.0e-10);
    K_hat_solver.SetAbsTol(1.0e-12);
    K_hat_solver.SetMaxIter(1500);
    K_hat_solver.SetPrintLevel(-1);

    adjoint_history.resize(time_steps + 1);
    for (Vector& state : adjoint_history) {
        state.SetSize(state_size);
    }
    auto& adjoint = adjoint_history;
    Vector bar_v(state_size);
    Vector bar_v_dot(state_size);
    Vector bar_v_ddot(state_size);
    bar_v = 0.0;
    bar_v_dot = 0.0;
    bar_v_ddot = 0.0;
    Vector M_transpose_adjoint(state_size);
    Vector C_transpose_adjoint(state_size);
    Vector adjoint_residual(state_size);

    for (int n = time_steps; n >= 1; --n) {
        Vector pressure_bar(
            bar_v.GetData() + pressure_offset, pressure_size);
        pressure_bar.Add(outlet_derivative[n - 1], outlet_functional);

        Vector previous_bar_v(state_size);
        Vector previous_bar_v_dot(state_size);
        Vector previous_bar_v_ddot(state_size);
        previous_bar_v = 0.0;
        previous_bar_v.Add(-a_3, bar_v_dot);
        previous_bar_v.Add(-a_6, bar_v_ddot);
        previous_bar_v_dot = 0.0;
        previous_bar_v_dot.Add(a_1, bar_v_dot);
        previous_bar_v_dot.Add(-a_4, bar_v_ddot);
        previous_bar_v_ddot = 0.0;
        previous_bar_v_ddot.Add(a_2, bar_v_dot);
        previous_bar_v_ddot.Add(-a_5, bar_v_ddot);

        bar_v.Add(a_3, bar_v_dot);
        bar_v.Add(a_6, bar_v_ddot);
        bar_v.SetSubVector(displacement_essential_tdofs, 0.0);

        adjoint[n].SetSize(state_size);
        if (n == time_steps) {
            adjoint[n] = 0.0;
        }
        else {
            adjoint[n] = adjoint[n + 1];
        }
        K_hat_solver.Mult(bar_v, adjoint[n]);
        effective_matrix_transpose->Mult(adjoint[n], adjoint_residual);
        adjoint_residual -= bar_v;
        const double relative_adjoint_residual = adjoint_residual.Norml2()
            / std::max(1.0, bar_v.Norml2());
        if (!K_hat_solver.GetConverged()
            || !std::isfinite(relative_adjoint_residual)
            || relative_adjoint_residual > 1.0e-9) {
            std::ostringstream message;
            message << "The Newmark adjoint residual gate failed at time step "
                    << n << ": physical=" << std::scientific
                    << relative_adjoint_residual << ", FGMRES="
                    << K_hat_solver.GetFinalRelNorm() << ", iterations="
                    << K_hat_solver.GetNumIterations() << ", converged="
                    << (K_hat_solver.GetConverged() ? "yes" : "no")
                    << " (limit 1.000000e-09).";
            log(LogLevel::Error, message.str());
            return false;
        }
        adjoint[n].SetSubVector(displacement_essential_tdofs, 0.0);

        M->MultTranspose(adjoint[n], M_transpose_adjoint);
        C->MultTranspose(adjoint[n], C_transpose_adjoint);
        previous_bar_v.Add(a_6, M_transpose_adjoint);
        previous_bar_v.Add(a_3, C_transpose_adjoint);
        previous_bar_v_dot.Add(a_4, M_transpose_adjoint);
        previous_bar_v_dot.Add(-a_1, C_transpose_adjoint);
        previous_bar_v_ddot.Add(a_5, M_transpose_adjoint);
        previous_bar_v_ddot.Add(-a_2, C_transpose_adjoint);

        bar_v = previous_bar_v;
        bar_v_dot = previous_bar_v_dot;
        bar_v_ddot = previous_bar_v_ddot;
    }

    if (!initial_matrix_transpose) {
        SparseMatrix initial_matrix(*M);
        initial_matrix.EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);
        initial_matrix_transpose.reset(Transpose(initial_matrix));
    }
    GSSmoother initial_preconditioner(*initial_matrix_transpose);
    FGMRESSolver initial_solver;
    initial_solver.SetPreconditioner(initial_preconditioner);
    initial_solver.SetOperator(*initial_matrix_transpose);
    initial_solver.SetKDim(50);
    initial_solver.SetRelTol(1.0e-10);
    initial_solver.SetAbsTol(1.0e-12);
    initial_solver.SetMaxIter(1500);
    initial_solver.SetPrintLevel(-1);
    Vector initial_adjoint(state_size);
    initial_adjoint = 0.0;
    bar_v_ddot.SetSubVector(displacement_essential_tdofs, 0.0);
    initial_solver.Mult(bar_v_ddot, initial_adjoint);
    initial_matrix_transpose->Mult(initial_adjoint, adjoint_residual);
    adjoint_residual -= bar_v_ddot;
    const double initial_adjoint_residual = adjoint_residual.Norml2()
        / std::max(1.0, bar_v_ddot.Norml2());
    if (!initial_solver.GetConverged()
        || !std::isfinite(initial_adjoint_residual)
        || initial_adjoint_residual > 1.0e-9) {
        std::ostringstream message;
        message << "The initial-acceleration adjoint residual gate failed: "
                << "physical=" << std::scientific << initial_adjoint_residual
                << ", FGMRES=" << initial_solver.GetFinalRelNorm()
                << ", iterations=" << initial_solver.GetNumIterations()
                << ", converged="
                << (initial_solver.GetConverged() ? "yes" : "no")
                << " (limit 1.000000e-09).";
        log(LogLevel::Error, message.str());
        return false;
    }
    initial_adjoint.SetSubVector(displacement_essential_tdofs, 0.0);

    const auto* physics =
        std::get_if<VibroacousticSettings>(&settings.physics);
    if (physics == nullptr || level_set_scale <= 0.0
        || optimizer_settings.cutDerivativeRelativeStep <= 0.0) {
        log(LogLevel::Error,
            "The cut derivative settings are invalid.");
        return false;
    }
    const double lambda = physics->youngs_modulus * physics->poisson_ratio
        / (1.0 - physics->poisson_ratio * physics->poisson_ratio);
    const double mu = physics->youngs_modulus
        / (2.0 * (1.0 + physics->poisson_ratio));
    ConstantCoefficient solid_density(physics->rho_s);
    ConstantCoefficient solid_lambda(lambda);
    ConstantCoefficient solid_mu(mu);
    ConstantCoefficient acoustic_mass(
        1.0 / (physics->rho_a * physics->c_a * physics->c_a));
    ConstantCoefficient acoustic_stiffness(1.0 / physics->rho_a);
    ImplicitDomainIntegrator Muu_integrator(
        std::make_unique<VectorMassIntegrator>(solid_density),
        *lset.phi, cut_integration_order, level_set_order,
        physics->epsilon, true);
    ImplicitDomainIntegrator Kuu_integrator(
        std::make_unique<ElasticityIntegrator>(solid_lambda, solid_mu),
        *lset.phi, cut_integration_order, level_set_order,
        physics->epsilon, true);
    ImplicitDomainIntegrator Mpp_integrator(
        std::make_unique<MassIntegrator>(acoustic_mass),
        *lset.phi, cut_integration_order, level_set_order,
        physics->epsilon, false);
    ImplicitDomainIntegrator Kpp_integrator(
        std::make_unique<DiffusionIntegrator>(acoustic_stiffness),
        *lset.phi, cut_integration_order, level_set_order,
        physics->epsilon, false);
    ImplicitSurfaceNormalIntegrator Kup_integrator(
        *lset.phi, cut_integration_order, level_set_order, -1.0, false);
    ImplicitSurfaceNormalIntegrator Mpu_integrator(
        *lset.phi, cut_integration_order, level_set_order, 1.0, true);

    const double omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const double omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const double alpha_d = 2.0 * physics->zeta * omega_1 * omega_2
        / (omega_1 + omega_2);
    const double beta_d = 2.0 * physics->zeta / (omega_1 + omega_2);
    const double perturbation =
        optimizer_settings.cutDerivativeRelativeStep * level_set_scale;
    const double inverse_perturbation = 0.5 / perturbation;

    Array<int> active(level_set_fes->GetTrueVSize());
    active = 0;
    for (int i = 0; i < lset.activeDesignDofs.Size(); ++i) {
        active[lset.activeDesignDofs[i]] = 1;
    }
    Vector physical_gradient(level_set_fes->GetTrueVSize());
    physical_gradient = 0.0;
    Array<int> phi_dofs;
    Array<int> displacement_dofs;
    Array<int> pressure_dofs;
    Vector matrix_product;
    auto contract = [&matrix_product](
                        const Vector& left,
                        const DenseMatrix& matrix,
                        const Vector& right) {
        matrix_product.SetSize(matrix.Height());
        matrix.Mult(right, matrix_product);
        return left * matrix_product;
    };
    auto difference = [inverse_perturbation](
                          DenseMatrix& plus,
                          const DenseMatrix& minus) {
        plus -= minus;
        plus *= inverse_perturbation;
    };

    for (int element = 0; element < mesh->GetNE(); ++element) {
        if (mesh->GetAttribute(element)
            != static_cast<int>(DomainAttribute::design)) {
            continue;
        }
        level_set_fes->GetElementDofs(element, phi_dofs);
        double minimum_phi = std::numeric_limits<double>::max();
        double maximum_phi = std::numeric_limits<double>::lowest();
        for (int dof = 0; dof < phi_dofs.Size(); ++dof) {
            minimum_phi = std::min(minimum_phi, (*lset.phi)[phi_dofs[dof]]);
            maximum_phi = std::max(maximum_phi, (*lset.phi)[phi_dofs[dof]]);
        }
        if (minimum_phi > perturbation || maximum_phi < -perturbation) {
            continue;
        }
        displacement_fes->GetElementVDofs(element, displacement_dofs);
        scalar_fes->GetElementDofs(element, pressure_dofs);
        ElementTransformation* transformation =
            mesh->GetElementTransformation(element);
        const FiniteElement& scalar_element = *scalar_fes->GetFE(element);
        const FiniteElement& displacement_element =
            *displacement_fes->GetFE(element);

        Array<int> pressure_state_dofs(pressure_dofs.Size());
        for (int i = 0; i < pressure_dofs.Size(); ++i) {
            pressure_state_dofs[i] = pressure_offset + pressure_dofs[i];
        }

        for (int local_dof = 0; local_dof < phi_dofs.Size(); ++local_dof) {
            const int phi_dof = phi_dofs[local_dof];
            if (active[phi_dof] == 0) {
                continue;
            }
            const double original_phi = (*lset.phi)[phi_dof];

            DenseMatrix Muu_plus, Kuu_plus, Mpp_plus, Kpp_plus;
            DenseMatrix Kup_plus, Mpu_plus;
            (*lset.phi)[phi_dof] = original_phi + perturbation;
            Muu_integrator.AssembleElementMatrix(
                displacement_element, *transformation, Muu_plus);
            Kuu_integrator.AssembleElementMatrix(
                displacement_element, *transformation, Kuu_plus);
            Mpp_integrator.AssembleElementMatrix(
                scalar_element, *transformation, Mpp_plus);
            Kpp_integrator.AssembleElementMatrix(
                scalar_element, *transformation, Kpp_plus);
            Kup_integrator.AssembleElementMatrix2(
                scalar_element, displacement_element,
                *transformation, Kup_plus);
            Mpu_integrator.AssembleElementMatrix2(
                displacement_element, scalar_element,
                *transformation, Mpu_plus);

            DenseMatrix Muu_minus, Kuu_minus, Mpp_minus, Kpp_minus;
            DenseMatrix Kup_minus, Mpu_minus;
            (*lset.phi)[phi_dof] = original_phi - perturbation;
            Muu_integrator.AssembleElementMatrix(
                displacement_element, *transformation, Muu_minus);
            Kuu_integrator.AssembleElementMatrix(
                displacement_element, *transformation, Kuu_minus);
            Mpp_integrator.AssembleElementMatrix(
                scalar_element, *transformation, Mpp_minus);
            Kpp_integrator.AssembleElementMatrix(
                scalar_element, *transformation, Kpp_minus);
            Kup_integrator.AssembleElementMatrix2(
                scalar_element, displacement_element,
                *transformation, Kup_minus);
            Mpu_integrator.AssembleElementMatrix2(
                displacement_element, scalar_element,
                *transformation, Mpu_minus);
            (*lset.phi)[phi_dof] = original_phi;

            difference(Muu_plus, Muu_minus);
            difference(Kuu_plus, Kuu_minus);
            difference(Mpp_plus, Mpp_minus);
            difference(Kpp_plus, Kpp_minus);
            difference(Kup_plus, Kup_minus);
            difference(Mpu_plus, Mpu_minus);

            Vector initial_u, initial_p, acceleration_u, acceleration_p;
            initial_adjoint.GetSubVector(displacement_dofs, initial_u);
            initial_adjoint.GetSubVector(pressure_state_dofs, initial_p);
            Vector initial_state(
                result.U[0].GetData() + 2 * state_size, state_size);
            initial_state.GetSubVector(displacement_dofs, acceleration_u);
            initial_state.GetSubVector(pressure_state_dofs, acceleration_p);
            double sensitivity = -contract(
                initial_u, Muu_plus, acceleration_u);
            sensitivity -= contract(initial_p, Mpu_plus, acceleration_u);
            sensitivity -= contract(initial_p, Mpp_plus, acceleration_p);

            Vector lambda_u, lambda_p;
            Vector v_u, v_p, previous_v_u, previous_v_p;
            Vector previous_dot_u, previous_dot_p;
            Vector previous_ddot_u, previous_ddot_p;
            Vector mass_u, mass_p, damping_u;
            for (int n = 1; n <= time_steps; ++n) {
                adjoint[n].GetSubVector(displacement_dofs, lambda_u);
                adjoint[n].GetSubVector(pressure_state_dofs, lambda_p);

                Vector state(
                    result.U[n].GetData(), state_size);
                Vector previous_state(
                    result.U[n - 1].GetData(), state_size);
                Vector previous_dot(
                    result.U[n - 1].GetData() + state_size, state_size);
                Vector previous_ddot(
                    result.U[n - 1].GetData() + 2 * state_size, state_size);
                state.GetSubVector(displacement_dofs, v_u);
                state.GetSubVector(pressure_state_dofs, v_p);
                previous_state.GetSubVector(displacement_dofs, previous_v_u);
                previous_state.GetSubVector(pressure_state_dofs, previous_v_p);
                previous_dot.GetSubVector(displacement_dofs, previous_dot_u);
                previous_dot.GetSubVector(pressure_state_dofs, previous_dot_p);
                previous_ddot.GetSubVector(
                    displacement_dofs, previous_ddot_u);
                previous_ddot.GetSubVector(
                    pressure_state_dofs, previous_ddot_p);

                mass_u.SetSize(previous_v_u.Size());
                mass_u = 0.0;
                mass_u.Add(a_4, previous_dot_u);
                mass_u.Add(a_5, previous_ddot_u);
                mass_u.Add(a_6, previous_v_u);
                mass_u.Add(-a_6, v_u);
                mass_p.SetSize(previous_v_p.Size());
                mass_p = 0.0;
                mass_p.Add(a_4, previous_dot_p);
                mass_p.Add(a_5, previous_ddot_p);
                mass_p.Add(a_6, previous_v_p);
                mass_p.Add(-a_6, v_p);
                damping_u.SetSize(previous_v_u.Size());
                damping_u = 0.0;
                damping_u.Add(-a_1, previous_dot_u);
                damping_u.Add(-a_2, previous_ddot_u);
                damping_u.Add(a_3, previous_v_u);
                damping_u.Add(-a_3, v_u);

                sensitivity += contract(lambda_u, Muu_plus, mass_u);
                sensitivity += contract(lambda_p, Mpu_plus, mass_u);
                sensitivity += contract(lambda_p, Mpp_plus, mass_p);
                sensitivity += alpha_d
                    * contract(lambda_u, Muu_plus, damping_u);
                sensitivity += beta_d
                    * contract(lambda_u, Kuu_plus, damping_u);
                sensitivity -= contract(lambda_u, Kuu_plus, v_u);
                sensitivity -= contract(lambda_u, Kup_plus, v_p);
                sensitivity -= contract(lambda_p, Kpp_plus, v_p);
            }
            physical_gradient[phi_dof] += sensitivity;
        }
    }

    // Reverse the paper's node -> cell -> finite-volume filter -> node map.
    Vector filtered_gradient(cell_to_level_set->Width());
    cell_to_level_set->MultTranspose(
        physical_gradient, filtered_gradient);
    GSSmoother filter_preconditioner(*filter_matrix);
    CGSolver filter_solver;
    filter_solver.SetPreconditioner(filter_preconditioner);
    filter_solver.SetOperator(*filter_matrix);
    filter_solver.SetRelTol(1.0e-10);
    filter_solver.SetAbsTol(1.0e-12);
    filter_solver.SetMaxIter(1500);
    filter_solver.SetPrintLevel(-1);
    Vector filter_adjoint(filtered_gradient.Size());
    filter_adjoint = 0.0;
    filter_solver.Mult(filtered_gradient, filter_adjoint);
    if (!filter_solver.GetConverged()) {
        std::ostringstream message;
        message << "The adjoint PDE filter solve did not converge: "
                << filter_solver.GetNumIterations()
                << " CG iterations, relative residual " << std::scientific
                << filter_solver.GetFinalRelNorm() << ".";
        log(LogLevel::Error, message.str());
        return false;
    }
    Vector center_gradient(filter_adjoint.Size());
    for (int cell = 0; cell < center_gradient.Size(); ++cell) {
        center_gradient[cell] = cell_volumes[cell] * filter_adjoint[cell];
    }
    design_gradient.SetSize(design_to_cell->Width());
    design_to_cell->MultTranspose(center_gradient, design_gradient);
    design_gradient *= level_set_scale;
    design_gradient.SetSubVectorComplement(lset.activeDesignDofs, 0.0);
    if (design_gradient.CheckFinite() != 0) {
        log(LogLevel::Error,
            "The discrete-adjoint design gradient is non-finite.");
        return false;
    }
    return true;
}
