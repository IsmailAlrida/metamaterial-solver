#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "adjoint.hpp"
#include "cut_sensitivity.hpp"
#include "glvis_adapter.hpp"
#include "integrators.hpp"
#include "newmark.hpp"
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

#if METAMATERIAL_USE_CUDA
using PhysicsBlockSmoother = DSmoother;
#else
using PhysicsBlockSmoother = GSSmoother;
#endif

} // namespace

// move this later outside?
// TODO: Make comments doxygen-style with math and all to explain ur stuff
App::Solver::Solver(App::SolverSettings& settings,
                    App::OptimizerSettings& optimizer_settings,
                    App::LevelSet& lset,
                    App::SolverResult& result,
                    const App::LogFunction& log,
                    bool manage_parallel_workers)
    : settings(settings),
      optimizer_settings(optimizer_settings),
      lset(lset),
      result(result),
      log(log),
      fe_order(1),
      level_set_order(1),
      cut_integration_order(4),
      manage_parallel_workers(manage_parallel_workers)
{
}

App::Solver::~Solver()
{
    if (manage_parallel_workers) {
        shutdownParallelWorkers();
    }
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

const App::SolverPerformance& App::Solver::performance() const
{
    return performance_data;
}

bool App::Solver::bindToGlvis()
{
    if (glvis_stream && glvis_stream->good()) {
        return true;
    }
    glvis_stream.reset();
    if (glvis_connection_failures >= 3) {
        return false;
    }

    char host[] = "127.0.0.1";
    auto stream = std::make_unique<socketstream>(host, GlvisAdapter::Port);
    if (!stream->good()) {
        ++glvis_connection_failures;
        log(LogLevel::Warning,
            glvis_connection_failures == 3
                ? "Could not connect to the local GLVis panel three times; disabling streaming."
                : "Could not connect to the local GLVis panel; continuing headless.");
        return false;
    }

    stream->precision(8);
    glvis_stream = std::move(stream);
    glvis_connection_failures = 0;
    log(LogLevel::Message, "Connected the solver to the local GLVis panel.");
    return true;
}

void App::Solver::streamToGlvis()
{
    if (!glvis_stream && !bindToGlvis()) {
        return;
    }
    *glvis_stream << "solution\n" << *mesh << *lset.phi << std::flush;
    if (glvis_stream->good()) {
        log(LogLevel::Message,
            "Streamed the filtered design geometry to GLVis.");
        return;
    }

    glvis_stream.reset();
    ++glvis_connection_failures;
    log(LogLevel::Warning,
        glvis_connection_failures >= 3
            ? "The GLVis connection failed three times; disabling streaming."
            : "The GLVis connection was lost; a later iteration will reconnect.");
}

// TODO: Do something about the mixed camelCase and snake_case. Choose one.
bool App::Solver::setMesh()
{
    const auto started_at = std::chrono::steady_clock::now();
    performance_data = {};
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
    Muu_block.reset();
    Cuu_block.reset();
    Kuu_block.reset();
    Mpp_block.reset();
    Cpp_block.reset();
    Kpp_block.reset();
    effective_displacement_block.reset();
    effective_pressure_block.reset();
    initial_displacement_block.reset();
    initial_pressure_block.reset();
    effective_matrix.reset();
    effective_matrix_transpose.reset();
    initial_matrix.reset();
    initial_matrix_transpose.reset();
    design_to_cell.reset();
    cell_to_level_set.reset();
    filter_matrix.reset();
    inlet_load.SetSize(0);
    outlet_functional.SetSize(0);
    system_inlet_load.SetSize(0);
    system_outlet_functional.SetSize(0);
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
    performance_data.meshSetupSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started_at).count();
    return true;
}

bool App::Solver::prepareLevelSetAndSource()
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

    GridFunction unsmoothed_level_set(level_set_fes.get());
    unsmoothed_level_set.SetFromTrueDofs(lset.design);
    const auto smoothing_started_at = std::chrono::steady_clock::now();
    if (!smooth_level_set(unsmoothed_level_set, *lset.phi)) {
        return false;
    }
    performance_data.levelSetSmoothingSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - smoothing_started_at).count();

    if (source_pressure.empty()) {
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
    result.timeSteps = time_steps;
    result.dt = settings.dt;
    return true;
}

// TODO: Make a bloch-floquet periodic boundary condition?
bool App::Solver::assembleSolutionSpace()
{
    const auto assembly_started_at = std::chrono::steady_clock::now();
    if (!prepareLevelSetAndSource()) {
        return false;
    }
    const auto* physics = std::get_if<VibroacousticSettings>(&settings.physics);
    const int dim = mesh->Dimension();
    const int time_steps = result.timeSteps;
    M.reset();
    C.reset();
    K.reset();
    Muu_block.reset();
    Cuu_block.reset();
    Kuu_block.reset();
    Mpp_block.reset();
    Kpp_block.reset();
    effective_displacement_block.reset();
    effective_pressure_block.reset();
    initial_displacement_block.reset();
    initial_pressure_block.reset();
    effective_matrix.reset();
    effective_matrix_transpose.reset();
    initial_matrix.reset();
    initial_matrix_transpose.reset();

    Array<int> design_domain_marker(mesh->attributes.Max());
    Array<int> fixed_air_marker(mesh->attributes.Max());
    design_domain_marker = 0;
    fixed_air_marker = 0;
    design_domain_marker[static_cast<int>(DomainAttribute::design) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::inlet) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::outlet) - 1] = 1;

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
    Muu_block.reset(Muu_form.LoseMat());

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
    Kuu_block.reset(Kuu_form.LoseMat());

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
    Mpp_block.reset(Mpp_form.LoseMat());

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
    Kpp_block.reset(Kpp_form.LoseMat());

    MixedBilinearForm Kup_form(scalar_fes.get(), displacement_fes.get());
    auto* Kup_integrator = new ImplicitSurfaceNormalIntegrator(
        *lset.phi, cut_integration_order, level_set_order, -1.0, false);
    Kup_form.AddDomainIntegrator(Kup_integrator, design_domain_marker);
    Kup_form.Assemble();
    Kup_form.Finalize();
    std::unique_ptr<SparseMatrix> Kup(Kup_form.LoseMat());

    MixedBilinearForm Mpu_form(displacement_fes.get(), scalar_fes.get());
    auto* Mpu_integrator = new ImplicitSurfaceNormalIntegrator(
        *lset.phi, cut_integration_order, level_set_order, 1.0, true);
    Mpu_form.AddDomainIntegrator(Mpu_integrator, design_domain_marker);
    Mpu_form.Assemble();
    Mpu_form.Finalize();
    std::unique_ptr<SparseMatrix> Mpu(Mpu_form.LoseMat());
    const int degenerate_normals = std::max(
        Kup_integrator->GetDegenerateNormalCount(),
        Mpu_integrator->GetDegenerateNormalCount());
    if (degenerate_normals != 0) {
        log(LogLevel::Error,
            "The implicit interface contains "
                + std::to_string(degenerate_normals)
                + " quadrature points with an undefined normal.");
        return false;
    }

    const real_t omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const real_t omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const real_t alpha_d = 2.0 * physics->zeta * omega_1 * omega_2
        / (omega_1 + omega_2);
    const real_t beta_d = 2.0 * physics->zeta / (omega_1 + omega_2);
    Cuu_block.reset(Add(
        alpha_d, *Muu_block, beta_d, *Kuu_block));

    const int inlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::left)
        : static_cast<int>(CartesianBoundary2D::left);
    const int outlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::right)
        : static_cast<int>(CartesianBoundary2D::right);
    if (!Cpp_block) {
        Array<int> absorbing_marker(mesh->bdr_attributes.Max());
        absorbing_marker = 0;
        absorbing_marker[inlet_boundary - 1] = 1;
        absorbing_marker[outlet_boundary - 1] = 1;
        ConstantCoefficient inverse_impedance(
            1.0 / (physics->rho_a * physics->c_a));
        BilinearForm Cpp_form(scalar_fes.get());
        Cpp_form.AddBoundaryIntegrator(
            new BoundaryMassIntegrator(inverse_impedance), absorbing_marker);
        Cpp_form.Assemble();
        Cpp_form.Finalize();
        Cpp_block.reset(Cpp_form.LoseMat());
    }
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
    M_blocks.SetBlock(0, 0, Muu_block.get());
    M_blocks.SetBlock(1, 0, Mpu.get());
    M_blocks.SetBlock(1, 1, Mpp_block.get());
    M.reset(M_blocks.CreateMonolithic());
    BlockMatrix C_blocks(offsets);
    C_blocks.SetBlock(0, 0, Cuu_block.get());
    C_blocks.SetBlock(1, 1, Cpp_block.get());
    C.reset(C_blocks.CreateMonolithic());
    BlockMatrix K_blocks(offsets);
    K_blocks.SetBlock(0, 0, Kuu_block.get());
    K_blocks.SetBlock(0, 1, Kup.get());
    K_blocks.SetBlock(1, 1, Kpp_block.get());
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

    }

    system_inlet_load.SetSize(M->Height());
    system_inlet_load = 0.0;
    system_inlet_load.SetVector(inlet_load, pressure_offset);
    system_outlet_functional.SetSize(M->Height());
    system_outlet_functional = 0.0;
    system_outlet_functional.SetVector(outlet_functional, pressure_offset);

    result.stateSize = M->Height();
    result.displacementSize = displacement_size;
    result.pressureSize = pressure_size;
    result.pressureOffset = pressure_offset;
    result.timeSteps = time_steps;
    result.dt = settings.dt;
    performance_data.assemblySeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - assembly_started_at).count();
    log(LogLevel::Message,
        "Assembled fictitious-domain vibroacoustic M, C, and K matrices in "
            + std::to_string(performance_data.assemblySeconds) + " s.");
    return true;
}

bool App::Solver::solve()
{
    status.store(SolverStatus::Working);
    result.success = 0;
    frequency_response = {};
    if (settings.linearSolveMethod == LinearSolveMethod::mumps) {
        log(LogLevel::Error,
            "MUMPS requires the parallel-cpu backend.");
        status.store(SolverStatus::Error);
        return false;
    }
    if (!reference_ready) {
        performance_data.referenceTransientSeconds = 0.0;
    }
    performance_data.designedTransientSeconds = 0.0;
    performance_data.fourierSeconds = 0.0;
    performance_data.forwardFgmresIterations = 0;
    performance_data.forwardFgmresSolves = 0;
    performance_data.maximumForwardFgmresIterations = 0;

    if (!mesh) {
        log(LogLevel::Error,
            "Call setMesh() before solve().");
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
    bool designed_assembly_ready = M && C && K
        && Muu_block && Cuu_block && Kuu_block
        && Mpp_block && Cpp_block && Kpp_block;
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
            streamToGlvis();
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

        // Paper Eqs. (16) and (19), needed to form the effective matrix.
        const double a_3 = gamma / (beta * settings.dt);
        const double a_6 =
            1.0 / (beta * settings.dt * settings.dt);

        std::unique_ptr<SparseMatrix> M_and_C(Add(a_6, *M, a_3, *C));
        std::unique_ptr<SparseMatrix> K_hat(Add(1.0, *K, 1.0, *M_and_C));
        K_hat->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);

        std::unique_ptr<SparseMatrix> Muu_and_Cuu(
            Add(a_6, *Muu_block, a_3, *Cuu_block));
        effective_displacement_block.reset(
            Add(1.0, *Kuu_block, 1.0, *Muu_and_Cuu));
        std::unique_ptr<SparseMatrix> Mpp_and_Cpp(
            Add(a_6, *Mpp_block, a_3, *Cpp_block));
        effective_pressure_block.reset(
            Add(1.0, *Kpp_block, 1.0, *Mpp_and_Cpp));
        effective_displacement_block->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);

        Array<int> block_offsets(3);
        block_offsets[0] = 0;
        block_offsets[1] = pressure_offset;
        block_offsets[2] = K_hat->Height();
        PhysicsBlockSmoother displacement_preconditioner(
            *effective_displacement_block);
        PhysicsBlockSmoother pressure_preconditioner(
            *effective_pressure_block);
        BlockDiagonalPreconditioner K_hat_preconditioner(block_offsets);
        K_hat_preconditioner.SetDiagonalBlock(
            0, &displacement_preconditioner);
        K_hat_preconditioner.SetDiagonalBlock(
            1, &pressure_preconditioner);
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
        const int time_steps = result.timeSteps;
        log(LogLevel::Message,
            std::string("Solving the ")
                + (reference_analysis ? "empty-duct reference" : "designed duct")
                + " transient (" + std::to_string(state_size) + " unknowns, "
                + std::to_string(time_steps) + " time steps).");
        const auto transient_started_at = std::chrono::steady_clock::now();

        // Paper Eq. (21): M v_ddot^0 = h^0.
        auto M_system = std::make_unique<SparseMatrix>(*M);
        M_system->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);
        initial_displacement_block =
            std::make_unique<SparseMatrix>(*Muu_block);
        initial_pressure_block =
            std::make_unique<SparseMatrix>(*Mpp_block);
        initial_displacement_block->EliminateBC(
            displacement_essential_tdofs, Operator::DIAG_ONE);
        PhysicsBlockSmoother initial_displacement_preconditioner(
            *initial_displacement_block);
        PhysicsBlockSmoother initial_pressure_preconditioner(
            *initial_pressure_block);
        BlockDiagonalPreconditioner M_preconditioner(block_offsets);
        M_preconditioner.SetDiagonalBlock(
            0, &initial_displacement_preconditioner);
        M_preconditioner.SetDiagonalBlock(
            1, &initial_pressure_preconditioner);
        FGMRESSolver M_solver;
        M_solver.SetPreconditioner(M_preconditioner);
        M_solver.SetOperator(*M_system);
        M_solver.SetKDim(50);
        M_solver.SetRelTol(1.0e-10);
        M_solver.SetAbsTol(1.0e-12);
        M_solver.SetMaxIter(1500);
        M_solver.SetPrintLevel(-1);
        std::vector<double>& measured_outlet = reference_analysis
            ? reference_outlet_pressure
            : outlet_pressure;
        const auto solve_initial = [&M_solver](
                                       const Vector& rhs, Vector& solution) {
            M_solver.Mult(rhs, solution);
            return detail::LinearSolveResult{
                M_solver.GetConverged(),
                M_solver.GetNumIterations(),
                M_solver.GetFinalRelNorm()};
        };
        const auto solve_effective = [&K_hat_solver](
                                         const Vector& rhs, Vector& solution) {
            K_hat_solver.Mult(rhs, solution);
            return detail::LinearSolveResult{
                K_hat_solver.GetConverged(),
                K_hat_solver.GetNumIterations(),
                K_hat_solver.GetFinalRelNorm()};
        };
        if (!detail::runNewmark(
                settings,
                *physics,
                *M,
                *C,
                *K,
                *M_system,
                *K_hat,
                displacement_essential_tdofs,
                system_inlet_load,
                system_outlet_functional,
                source_pressure_derivative,
                solve_initial,
                solve_effective,
                [](const Vector& value) { return value.Norml2(); },
                [](const Vector& left, const Vector& right) {
                    return left * right;
                },
                !reference_analysis,
                result.U,
                result.residualNorms,
                measured_outlet,
                performance_data,
                log,
                reference_analysis ? "empty-duct reference" : "designed duct")) {
            return fail(SolverStatus::Diverged);
        }

        const double transient_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - transient_started_at).count();
        if (reference_analysis) {
            performance_data.referenceTransientSeconds = transient_seconds;
        }
        else {
            performance_data.designedTransientSeconds = transient_seconds;
            effective_matrix = std::move(K_hat);
            initial_matrix = std::move(M_system);
        }

        if (reference_analysis) {
            reference_ready = true;
            log(LogLevel::Message,
                "Computed the deterministic empty-duct reference response.");
        }
    }

    lset.design = designed_geometry;
    lset.enforceDesignConstraints();
    const auto fourier_started_at = std::chrono::steady_clock::now();
    if (!postprocessFourierResponse()) {
        return fail(SolverStatus::Error);
    }
    performance_data.fourierSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - fourier_started_at).count();
    log(LogLevel::Message,
        "Forward timings: reference/design/FFT = "
            + std::to_string(performance_data.referenceTransientSeconds)
            + " / "
            + std::to_string(performance_data.designedTransientSeconds)
            + " / " + std::to_string(performance_data.fourierSeconds)
            + " s; "
            + std::to_string(performance_data.forwardFgmresSolves)
            + " FGMRES solves, "
            + std::to_string(performance_data.forwardFgmresIterations)
            + " total iterations.");

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

#if METAMATERIAL_USE_CUDA
    DSmoother preconditioner(*filter_matrix);
#else
    GSSmoother preconditioner(*filter_matrix);
#endif
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

    std::vector<std::complex<double>> response_spectrum;
    std::vector<std::complex<double>> reference_spectrum;
    if (!detail::forwardWindowedSignal(
            outlet_pressure, fft_window, response_spectrum)
        || !detail::forwardWindowedSignal(
            reference_outlet_pressure, fft_window, reference_spectrum)) {
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
        std::max(
            std::numeric_limits<double>::min(),
            std::sqrt(std::numeric_limits<double>::epsilon())
                * maximum_reference);
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
                           const std::vector<double>& amplitude) {
        auto signal = std::make_shared<SignalTD>();
        signal->size = sample_count;
        signal->time.resize(sample_count);
        signal->amplitude.resize(sample_count);
        for (int sample = 0; sample < sample_count; ++sample) {
            signal->time[sample] = sample * settings.dt;
            signal->amplitude[sample] = amplitude[sample];
        }
        return std::shared_ptr<const SignalTD>(std::move(signal));
    };

    std::shared_ptr<const SignalFFT> published_response = std::move(response);
    std::atomic_store(
        &result.materialImpulseResponse, std::move(published_response));
    std::atomic_store(
        &result.inletPressure, make_signal(source_pressure));
    std::atomic_store(
        &result.outletPressure, make_signal(outlet_pressure));
    std::atomic_store(
        &result.referenceOutletPressure,
        make_signal(reference_outlet_pressure));
    return true;
}

bool App::Solver::differentiateFrequencyResponses(
    const std::vector<std::complex<double>>& pass_spectrum_derivative,
    const std::vector<std::complex<double>>& stop_spectrum_derivative,
    Vector& pass_design_gradient,
    Vector& stop_design_gradient)
{
    const int time_steps = result.timeSteps;
    const int state_size = result.stateSize;
    const std::size_t spectrum_size = frequency_response.outlet.size();
    const bool has_pass = !pass_spectrum_derivative.empty();
    const bool has_stop = !stop_spectrum_derivative.empty();
    auto valid_derivative = [spectrum_size, time_steps](
                                const std::vector<std::complex<double>>& value) {
        return value.empty()
            || (value.size() == spectrum_size
                && value.size()
                    == static_cast<std::size_t>(time_steps / 2 + 1));
    };
    if (!mesh || mesh->Dimension() != 2 || !M || !C || !K
        || !effective_matrix || !initial_matrix
        || !effective_displacement_block || !effective_pressure_block
        || !initial_displacement_block || !initial_pressure_block
        || !design_to_cell || !cell_to_level_set || !filter_matrix
        || time_steps <= 0 || state_size <= 0
        || (!has_pass && !has_stop)
        || result.U.size() != static_cast<std::size_t>(time_steps + 1)
        || fft_window.size() != static_cast<std::size_t>(time_steps)
        || !valid_derivative(pass_spectrum_derivative)
        || !valid_derivative(stop_spectrum_derivative)) {
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

    std::vector<double> pass_outlet_derivative;
    std::vector<double> stop_outlet_derivative;
    if (!detail::inverseOutletDerivative(
            pass_spectrum_derivative,
            time_steps,
            fft_window,
            pass_outlet_derivative)
        || !detail::inverseOutletDerivative(
            stop_spectrum_derivative,
            time_steps,
            fft_window,
            stop_outlet_derivative)) {
        log(LogLevel::Error, "FFTW could not create the adjoint transform.");
        return false;
    }

    if (!effective_matrix_transpose) {
        effective_matrix_transpose.reset(Transpose(*effective_matrix));
    }
    Array<int> block_offsets(3);
    block_offsets[0] = 0;
    block_offsets[1] = pressure_offset;
    block_offsets[2] = state_size;
    PhysicsBlockSmoother displacement_preconditioner(
        *effective_displacement_block);
    PhysicsBlockSmoother pressure_preconditioner(
        *effective_pressure_block);
    BlockDiagonalPreconditioner K_hat_preconditioner(block_offsets);
    K_hat_preconditioner.SetDiagonalBlock(
        0, &displacement_preconditioner);
    K_hat_preconditioner.SetDiagonalBlock(
        1, &pressure_preconditioner);
    FGMRESSolver K_hat_solver;
    K_hat_solver.SetPreconditioner(K_hat_preconditioner);
    K_hat_solver.SetOperator(*effective_matrix_transpose);
    K_hat_solver.iterative_mode = true;
    K_hat_solver.SetKDim(50);
    K_hat_solver.SetRelTol(1.0e-10);
    K_hat_solver.SetAbsTol(1.0e-12);
    K_hat_solver.SetMaxIter(1500);
    K_hat_solver.SetPrintLevel(-1);

    if (!initial_matrix_transpose) {
        initial_matrix_transpose.reset(Transpose(*initial_matrix));
    }
    PhysicsBlockSmoother initial_displacement_preconditioner(
        *initial_displacement_block);
    PhysicsBlockSmoother initial_pressure_preconditioner(
        *initial_pressure_block);
    BlockDiagonalPreconditioner initial_preconditioner(block_offsets);
    initial_preconditioner.SetDiagonalBlock(
        0, &initial_displacement_preconditioner);
    initial_preconditioner.SetDiagonalBlock(
        1, &initial_pressure_preconditioner);
    FGMRESSolver initial_solver;
    initial_solver.SetPreconditioner(initial_preconditioner);
    initial_solver.SetOperator(*initial_matrix_transpose);
    initial_solver.SetKDim(50);
    initial_solver.SetRelTol(1.0e-10);
    initial_solver.SetAbsTol(1.0e-12);
    initial_solver.SetMaxIter(1500);
    initial_solver.SetPrintLevel(-1);

    performance_data.passAdjointSeconds = 0.0;
    performance_data.stopAdjointSeconds = 0.0;
    performance_data.adjointFgmresIterations = 0;
    performance_data.adjointFgmresSolves = 0;
    performance_data.maximumAdjointFgmresIterations = 0;
    detail::LinearSolve solve_effective_transpose =
        [&K_hat_solver](const Vector& rhs, Vector& solution) {
            K_hat_solver.Mult(rhs, solution);
            return detail::LinearSolveResult{
                K_hat_solver.GetConverged(),
                K_hat_solver.GetNumIterations(),
                K_hat_solver.GetFinalRelNorm()};
        };
    detail::LinearSolve solve_initial_transpose =
        [&initial_solver](const Vector& rhs, Vector& solution) {
            initial_solver.Mult(rhs, solution);
            return detail::LinearSolveResult{
                initial_solver.GetConverged(),
                initial_solver.GetNumIterations(),
                initial_solver.GetFinalRelNorm()};
        };
    const detail::GlobalNorm norm = [](const Vector& value) {
        return value.Norml2();
    };

    Vector pass_initial_adjoint;
    Vector stop_initial_adjoint;
    if (has_pass && !detail::runNewmarkAdjoint(
            settings,
            *M,
            *C,
            *effective_matrix_transpose,
            *initial_matrix_transpose,
            displacement_essential_tdofs,
            system_outlet_functional,
            pass_outlet_derivative,
            solve_effective_transpose,
            solve_initial_transpose,
            norm,
            pass_adjoint_history,
            pass_initial_adjoint,
            performance_data,
            performance_data.passAdjointSeconds,
            log,
            "pass-band")) {
        return false;
    }
    if (has_stop && !detail::runNewmarkAdjoint(
            settings,
            *M,
            *C,
            *effective_matrix_transpose,
            *initial_matrix_transpose,
            displacement_essential_tdofs,
            system_outlet_functional,
            stop_outlet_derivative,
            solve_effective_transpose,
            solve_initial_transpose,
            norm,
            stop_adjoint_history,
            stop_initial_adjoint,
            performance_data,
            performance_data.stopAdjointSeconds,
            log,
            "stop-band")) {
        return false;
    }

    Array<int> active(level_set_fes->GetVSize());
    active = 0;
    for (int i = 0; i < lset.activeDesignDofs.Size(); ++i) {
        active[lset.activeDesignDofs[i]] = 1;
    }

    Vector pass_physical_gradient;
    Vector stop_physical_gradient;
    int cut_element_count = 0;
    int differentiated_dof_count = 0;
    const auto differentiation_started_at = std::chrono::steady_clock::now();
    if (!detail::differentiateCutElements(
            settings,
            optimizer_settings,
            *mesh,
            *level_set_fes,
            *displacement_fes,
            *scalar_fes,
            *lset.phi,
            active,
            result.U,
            has_pass ? &pass_adjoint_history : nullptr,
            has_stop ? &stop_adjoint_history : nullptr,
            has_pass ? &pass_initial_adjoint : nullptr,
            has_stop ? &stop_initial_adjoint : nullptr,
            pressure_offset,
            time_steps,
            static_cast<int>(DomainAttribute::design),
            cut_integration_order,
            level_set_order,
            level_set_scale,
            pass_physical_gradient,
            stop_physical_gradient,
            cut_element_count,
            differentiated_dof_count)) {
        log(LogLevel::Error,
            "The cut-element design differentiation failed.");
        return false;
    }
    performance_data.cutElements = cut_element_count;
    performance_data.differentiatedDofs = differentiated_dof_count;
    performance_data.cutDifferentiationSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - differentiation_started_at).count();
    if (!detail::reverseFilterGradients(
            *filter_matrix,
            *design_to_cell,
            *cell_to_level_set,
            cell_volumes,
            lset.activeDesignDofs,
            level_set_scale,
            has_pass,
            has_stop,
            pass_physical_gradient,
            stop_physical_gradient,
            pass_design_gradient,
            stop_design_gradient,
            performance_data,
            log)) {
        return false;
    }
    log(LogLevel::Message,
        "Completed pass/stop adjoints in "
            + std::to_string(performance_data.passAdjointSeconds) + " / "
            + std::to_string(performance_data.stopAdjointSeconds)
            + " s; differentiated "
            + std::to_string(performance_data.differentiatedDofs)
            + " local level-set DOFs across "
            + std::to_string(performance_data.cutElements)
            + " cut elements in "
            + std::to_string(performance_data.cutDifferentiationSeconds)
            + " s.");
    return true;
}
