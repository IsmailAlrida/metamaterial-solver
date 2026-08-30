#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "integrators.hpp"
#include "adjoint.hpp"
#include "cut_sensitivity.hpp"
#include "newmark.hpp"

using namespace mfem;

#if METAMATERIAL_USE_MPI

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

enum class ParallelCommand : int {
    set_mesh,
    assemble,
    solve,
    differentiate,
    shutdown
};

void broadcast_command(
    MPI_Comm comm,
    int rank,
    int command_depth,
    ParallelCommand command)
{
    if (rank == 0 && command_depth == 0) {
        int value = static_cast<int>(command);
        MPI_Bcast(&value, 1, MPI_INT, 0, comm);
    }
}

struct DepthGuard {
    int& depth;

    ~DepthGuard()
    {
        --depth;
    }
};

template <typename T>
void broadcast_value(MPI_Comm comm, T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    MPI_Bcast(&value, sizeof(T), MPI_BYTE, 0, comm);
}

void broadcast_string(MPI_Comm comm, int rank, std::string& value)
{
    int size = rank == 0 ? static_cast<int>(value.size()) : 0;
    MPI_Bcast(&size, 1, MPI_INT, 0, comm);
    if (rank != 0) {
        value.resize(size);
    }
    if (size > 0) {
        MPI_Bcast(value.data(), size, MPI_CHAR, 0, comm);
    }
}

bool synchronize_settings(
    MPI_Comm comm,
    int rank,
    App::SolverSettings& solver,
    App::OptimizerSettings& optimizer)
{
    broadcast_value(comm, solver.nx);
    broadcast_value(comm, solver.ny);
    broadcast_value(comm, solver.nz);
    broadcast_value(comm, solver.inletLength);
    broadcast_value(comm, solver.designLength);
    broadcast_value(comm, solver.outletLength);
    broadcast_value(comm, solver.sy);
    broadcast_value(comm, solver.sz);
    broadcast_value(comm, solver.duration);
    broadcast_value(comm, solver.dt);
    broadcast_value(comm, solver.newmarkBeta);
    broadcast_value(comm, solver.newmarkGamma);
    broadcast_value(comm, solver.sourceAmplitude);
    broadcast_value(comm, solver.sourceSeed);
    broadcast_value(comm, solver.initialPatternLx);
    broadcast_value(comm, solver.initialPatternLy);
    broadcast_value(comm, solver.initialPatternX);
    broadcast_value(comm, solver.initialPatternY);
    broadcast_value(comm, solver.initialPatternBias);
    broadcast_value(comm, solver.initialPatternThreshold);
    broadcast_string(comm, rank, solver.algo);
    broadcast_value(comm, solver.isotropicGrid);
    broadcast_value(comm, solver.useHannWindow);
    broadcast_value(comm, solver.device);
    broadcast_value(comm, solver.linearSolveMethod);

    int physics_kind = rank == 0
        ? static_cast<int>(solver.physics.index()) : 0;
    broadcast_value(comm, physics_kind);
    if (physics_kind == 0) {
        if (rank != 0) {
            solver.physics = App::VibroacousticSettings{};
        }
        auto& physics = std::get<App::VibroacousticSettings>(solver.physics);
        broadcast_value(comm, physics.rho_s);
        broadcast_value(comm, physics.rho_a);
        broadcast_value(comm, physics.c_a);
        broadcast_value(comm, physics.youngs_modulus);
        broadcast_value(comm, physics.poisson_ratio);
        broadcast_value(comm, physics.zeta);
        broadcast_value(comm, physics.f1);
        broadcast_value(comm, physics.f2);
        broadcast_value(comm, physics.epsilon);
    }
    else if (physics_kind == 1) {
        if (rank != 0) {
            solver.physics = App::ElectromagneticSettings{};
        }
    }
    else {
        return false;
    }

    broadcast_value(comm, optimizer.filterRadius);
    broadcast_value(comm, optimizer.frequencyMin);
    broadcast_value(comm, optimizer.frequencyMax);
    broadcast_value(comm, optimizer.attenuationMinDb);
    broadcast_value(comm, optimizer.attenuationMaxDb);
    broadcast_value(comm, optimizer.frequencySamples);
    broadcast_value(comm, optimizer.maxIterations);
    broadcast_value(comm, optimizer.mmaInitialAsymptote);
    broadcast_value(comm, optimizer.mmaDecreaseAsymptote);
    broadcast_value(comm, optimizer.mmaIncreaseAsymptote);
    broadcast_value(comm, optimizer.mmaConstraintPenalty);
    broadcast_value(comm, optimizer.cutDerivativeRelativeStep);
    broadcast_value(comm, optimizer.displayTargetInDb);

    int band_count = rank == 0
        ? static_cast<int>(optimizer.frequencyBands.size()) : 0;
    broadcast_value(comm, band_count);
    if (band_count < 0 || band_count > 10000) {
        return false;
    }
    if (rank != 0) {
        optimizer.frequencyBands.resize(band_count);
    }
    for (App::FrequencyBand& band : optimizer.frequencyBands) {
        broadcast_value(comm, band.type);
        broadcast_value(comm, band.startHz);
        broadcast_value(comm, band.endHz);
        broadcast_value(comm, band.targetTransmission);
    }
    return true;
}

double global_norm(MPI_Comm comm, const Vector& value)
{
    const double local = value * value;
    double global = 0.0;
    MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, comm);
    return std::sqrt(global);
}

double global_dot(MPI_Comm comm, const Vector& left, const Vector& right)
{
    const double local = left * right;
    double global = 0.0;
    MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, comm);
    return global;
}

bool all_succeeded(MPI_Comm comm, bool local_success)
{
    int success = local_success ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &success, 1, MPI_INT, MPI_MIN, comm);
    return success != 0;
}

void broadcast_vector(MPI_Comm comm, int rank, std::vector<double>& values)
{
    int size = rank == 0 ? static_cast<int>(values.size()) : 0;
    MPI_Bcast(&size, 1, MPI_INT, 0, comm);
    values.resize(size);
    if (size > 0) {
        MPI_Bcast(values.data(), size, MPI_DOUBLE, 0, comm);
    }
}

void broadcast_vector(MPI_Comm comm, int rank, Vector& values)
{
    int size = rank == 0 ? values.Size() : 0;
    MPI_Bcast(&size, 1, MPI_INT, 0, comm);
    values.SetSize(size);
    if (size > 0) {
        MPI_Bcast(values.GetData(), size, MPI_DOUBLE, 0, comm);
    }
}

void broadcast_array(MPI_Comm comm, int rank, Array<int>& values)
{
    int size = rank == 0 ? values.Size() : 0;
    MPI_Bcast(&size, 1, MPI_INT, 0, comm);
    values.SetSize(size);
    if (size > 0) {
        MPI_Bcast(values.GetData(), size, MPI_INT, 0, comm);
    }
}

void broadcast_complex_vector(
    MPI_Comm comm,
    int rank,
    std::vector<std::complex<double>>& values)
{
    std::vector<double> packed;
    if (rank == 0) {
        packed.resize(2 * values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            packed[2 * i] = values[i].real();
            packed[2 * i + 1] = values[i].imag();
        }
    }
    broadcast_vector(comm, rank, packed);
    if (rank != 0) {
        values.resize(packed.size() / 2);
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = {packed[2 * i], packed[2 * i + 1]};
        }
    }
}

std::unique_ptr<HypreParMatrix> block_matrix(
    const HypreParMatrix* a00,
    const HypreParMatrix* a01,
    const HypreParMatrix* a10,
    const HypreParMatrix* a11)
{
    Array2D<const HypreParMatrix*> blocks(2, 2);
    blocks(0, 0) = a00;
    blocks(0, 1) = a01;
    blocks(1, 0) = a10;
    blocks(1, 1) = a11;
    return std::unique_ptr<HypreParMatrix>(
        HypreParMatrixFromBlocks(blocks));
}

} // namespace

struct App::Solver::ParallelState {
    MPI_Comm comm = MPI_COMM_WORLD;
    int rank = 0;
    int ranks = 1;
    std::vector<int> partition;
    std::unique_ptr<ParMesh> mesh;
    std::unique_ptr<H1_FECollection> collection;
    std::unique_ptr<ParFiniteElementSpace> scalar_fes;
    std::unique_ptr<ParFiniteElementSpace> displacement_fes;
    std::unique_ptr<ParGridFunction> phi;

    std::unique_ptr<HypreParMatrix> M;
    std::unique_ptr<HypreParMatrix> C;
    std::unique_ptr<HypreParMatrix> K;
    std::unique_ptr<HypreParMatrix> Muu;
    std::unique_ptr<HypreParMatrix> Cuu;
    std::unique_ptr<HypreParMatrix> Kuu;
    std::unique_ptr<HypreParMatrix> Mpp;
    std::unique_ptr<HypreParMatrix> Cpp;
    std::unique_ptr<HypreParMatrix> Kpp;
    std::unique_ptr<HypreParMatrix> initial_matrix;
    std::unique_ptr<HypreParMatrix> effective_matrix;
    std::unique_ptr<HypreParMatrix> initial_displacement;
    std::unique_ptr<HypreParMatrix> initial_pressure;
    std::unique_ptr<HypreParMatrix> effective_displacement;
    std::unique_ptr<HypreParMatrix> effective_pressure;
    std::unique_ptr<MUMPSSolver> initial_mumps;
    std::unique_ptr<MUMPSSolver> effective_mumps;
    std::unique_ptr<HypreBoomerAMG> initial_u_amg;
    std::unique_ptr<HypreBoomerAMG> initial_p_amg;
    std::unique_ptr<HypreBoomerAMG> effective_u_amg;
    std::unique_ptr<HypreBoomerAMG> effective_p_amg;

    Vector inlet_load;
    Vector outlet_functional;
    Vector system_inlet_load;
    Vector system_outlet_functional;
    Array<int> displacement_essential_tdofs;
    bool boundary_data_ready = false;
    bool reference_ready = false;
    int command_depth = 0;
};

#endif

void App::Solver::parallelWorkerLoop()
{
#if !METAMATERIAL_USE_MPI
    log(LogLevel::Error,
        "The MPI worker loop requires a parallel-cpu build.");
#else
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank == 0) {
        return;
    }

    while (true) {
        int command_value = 0;
        MPI_Bcast(&command_value, 1, MPI_INT, 0, MPI_COMM_WORLD);
        const auto command = static_cast<ParallelCommand>(command_value);
        if (command == ParallelCommand::shutdown) {
            parallel_workers_shutdown = true;
            return;
        }
        if (command == ParallelCommand::set_mesh) {
            setMesh(true);
        }
        else if (command == ParallelCommand::assemble) {
            assembleSolutionSpace(true);
        }
        else if (command == ParallelCommand::solve) {
            solve(true);
        }
        else if (command == ParallelCommand::differentiate) {
            std::vector<std::complex<double>> pass;
            std::vector<std::complex<double>> stop;
            Vector pass_gradient;
            Vector stop_gradient;
            differentiateFrequencyResponses(
                pass, stop, pass_gradient, stop_gradient, true);
        }
        else {
            std::cerr << "Unknown parallel solver command on MPI rank "
                      << rank << ".\n";
            return;
        }
    }
#endif
}

void App::Solver::shutdownParallelWorkers()
{
#if METAMATERIAL_USE_MPI
    if (!manage_parallel_workers || parallel_workers_shutdown) {
        return;
    }
    int initialized = 0;
    int finalized = 0;
    MPI_Initialized(&initialized);
    MPI_Finalized(&finalized);
    if (initialized == 0 || finalized != 0) {
        parallel_workers_shutdown = true;
        return;
    }
    int rank = 0;
    int ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
    if (rank == 0 && ranks > 1) {
        int command = static_cast<int>(ParallelCommand::shutdown);
        MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }
    parallel_workers_shutdown = true;
#endif
}

bool App::Solver::setMesh(bool parallel)
{
    if (!parallel) {
        return setMesh();
    }

#if !METAMATERIAL_USE_MPI
    log(LogLevel::Error,
        "The parallel solver requires a parallel-cpu build.");
    return false;
#else
    int rank = 0;
    int ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
    broadcast_command(
        MPI_COMM_WORLD, rank, 0, ParallelCommand::set_mesh);
    if (!synchronize_settings(
            MPI_COMM_WORLD, rank, settings, optimizer_settings)) {
        if (rank == 0) {
            log(LogLevel::Error, "Could not broadcast the solver settings.");
        }
        return false;
    }

    parallel_state = std::make_shared<ParallelState>();
    parallel_state->rank = rank;
    parallel_state->ranks = ranks;
    performance_data.mpiRanks = ranks;
    parallel_workers_shutdown = false;
    const bool ready = setMesh();
    int all_ready = ready ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &all_ready, 1, MPI_INT, MPI_MIN,
                  parallel_state->comm);
    if (all_ready && parallel_state->rank == 0) {
        log(LogLevel::Message,
            "Prepared the global mesh for "
                + std::to_string(parallel_state->ranks) + " MPI ranks.");
    }
    return all_ready != 0;
#endif
}

bool App::Solver::assembleSolutionSpace(bool parallel)
{
    if (!parallel) {
        return assembleSolutionSpace();
    }

#if !METAMATERIAL_USE_MPI
    log(LogLevel::Error,
        "The parallel solver requires a parallel-cpu build.");
    return false;
#else
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    broadcast_command(
        MPI_COMM_WORLD,
        rank,
        parallel_state ? parallel_state->command_depth : 0,
        ParallelCommand::assemble);
    const bool assembly_ready = parallel_state && mesh;
    if (!all_succeeded(MPI_COMM_WORLD, assembly_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Call setMesh(true) before parallel assembly.");
        }
        return false;
    }
    ParallelState& state = *parallel_state;
    ++state.command_depth;
    DepthGuard depth_guard{state.command_depth};
    const auto started_at = std::chrono::steady_clock::now();

    // Rank zero owns the replicated design and paper cell-centred filter.
    // The distributed ranks receive only the filtered level set and source.
    int prepared = 1;
    if (state.rank == 0 && !prepareLevelSetAndSource()) {
        prepared = 0;
    }
    MPI_Bcast(&prepared, 1, MPI_INT, 0, state.comm);
    if (!prepared) {
        return false;
    }

    const int dim = mesh->Dimension();
    const double design_start = settings.inletLength;
    const double design_end = settings.inletLength + settings.designLength;
    for (int element = 0; element < mesh->GetNE(); ++element) {
        Vector center(dim);
        mesh->GetElementCenter(element, center);
        mesh->SetAttribute(
            element,
            center[0] < design_start
                ? static_cast<int>(DomainAttribute::inlet)
                : center[0] < design_end
                    ? static_cast<int>(DomainAttribute::design)
                    : static_cast<int>(DomainAttribute::outlet));
    }
    mesh->SetAttributes();

    Vector global_phi;
    if (state.rank == 0) {
        lset.phi->GetTrueDofs(global_phi);
    }
    broadcast_vector(state.comm, state.rank, lset.design);
    broadcast_vector(state.comm, state.rank, global_phi);
    broadcast_array(state.comm, state.rank, lset.activeDesignDofs);
    MPI_Bcast(&level_set_scale, 1, MPI_DOUBLE, 0, state.comm);
    broadcast_vector(state.comm, state.rank, source_pressure);
    broadcast_vector(state.comm, state.rank, source_pressure_derivative);

    int time_steps = state.rank == 0 ? result.timeSteps : 0;
    MPI_Bcast(&time_steps, 1, MPI_INT, 0, state.comm);
    if (!state.mesh) {
        state.partition.resize(mesh->GetNE());
        for (int element = 0; element < mesh->GetNE(); ++element) {
            state.partition[element] = std::min(
                state.ranks - 1,
                element * state.ranks / mesh->GetNE());
        }
        state.mesh = std::make_unique<ParMesh>(
            state.comm, *mesh, state.partition.data());
        state.collection = std::make_unique<H1_FECollection>(fe_order, dim);
        state.scalar_fes = std::make_unique<ParFiniteElementSpace>(
            state.mesh.get(), state.collection.get());
        state.displacement_fes = std::make_unique<ParFiniteElementSpace>(
            state.mesh.get(), state.collection.get(), dim, Ordering::byVDIM);
    }

    H1_FECollection global_collection(fe_order, dim);
    FiniteElementSpace global_space(mesh.get(), &global_collection);
    GridFunction global_level_set(&global_space);
    global_level_set.SetFromTrueDofs(global_phi);
    state.phi = std::make_unique<ParGridFunction>(
        state.mesh.get(), &global_level_set, state.partition.data());

    const auto* physics = std::get_if<VibroacousticSettings>(&settings.physics);
    if (physics == nullptr) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "Parallel assembly currently supports vibroacoustics only.");
        }
        return false;
    }

    Array<int> design_marker(state.mesh->attributes.Max());
    Array<int> fixed_air_marker(state.mesh->attributes.Max());
    design_marker = 0;
    fixed_air_marker = 0;
    design_marker[static_cast<int>(DomainAttribute::design) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::inlet) - 1] = 1;
    fixed_air_marker[static_cast<int>(DomainAttribute::outlet) - 1] = 1;

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

    // ponytail: fixed-air terms reassemble to avoid retaining four extra
    // sparse matrices; cache them only if assembly time outweighs peak RAM.
    ParBilinearForm Muu_form(state.displacement_fes.get());
    auto* solid_domain_integrator = new ImplicitDomainIntegrator(
        std::make_unique<VectorMassIntegrator>(solid_density),
        *state.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        true);
    Muu_form.AddDomainIntegrator(solid_domain_integrator, design_marker);
    Muu_form.AddDomainIntegrator(
        new VectorMassIntegrator(fictitious_solid_density), fixed_air_marker);
    Muu_form.Assemble();
    Muu_form.Finalize();
    state.Muu.reset(Muu_form.ParallelAssemble());

    double solid_measure = solid_domain_integrator->GetCutMeasure();
    MPI_Allreduce(MPI_IN_PLACE, &solid_measure, 1, MPI_DOUBLE, MPI_SUM,
                  state.comm);
    bool infill_valid = true;
    if (state.rank == 0) {
        infill_valid = std::isfinite(solid_measure)
            && design_region_measure > 0.0
            && solid_measure >= -1.0e-12
            && solid_measure <= design_region_measure * (1.0 + 1.0e-10);
        if (infill_valid) {
            result.solidInfillFraction.store(
                std::clamp(
                    solid_measure / design_region_measure, 0.0, 1.0),
                std::memory_order_release);
        }
        else {
            log(LogLevel::Error,
                "The parallel solid infill measure is outside the design region.");
        }
    }
    if (!all_succeeded(state.comm, infill_valid)) {
        return false;
    }

    ParBilinearForm Kuu_form(state.displacement_fes.get());
    Kuu_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<ElasticityIntegrator>(solid_lambda, solid_mu),
        *state.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        true), design_marker);
    Kuu_form.AddDomainIntegrator(new ElasticityIntegrator(
        fictitious_solid_lambda, fictitious_solid_mu), fixed_air_marker);
    Kuu_form.Assemble();
    Kuu_form.Finalize();
    state.Kuu.reset(Kuu_form.ParallelAssemble());

    ParBilinearForm Mpp_form(state.scalar_fes.get());
    Mpp_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<MassIntegrator>(acoustic_mass),
        *state.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        false), design_marker);
    Mpp_form.AddDomainIntegrator(
        new MassIntegrator(acoustic_mass), fixed_air_marker);
    Mpp_form.Assemble();
    Mpp_form.Finalize();
    state.Mpp.reset(Mpp_form.ParallelAssemble());

    ParBilinearForm Kpp_form(state.scalar_fes.get());
    Kpp_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<DiffusionIntegrator>(acoustic_stiffness),
        *state.phi,
        cut_integration_order,
        level_set_order,
        physics->epsilon,
        false), design_marker);
    Kpp_form.AddDomainIntegrator(
        new DiffusionIntegrator(acoustic_stiffness), fixed_air_marker);
    Kpp_form.Assemble();
    Kpp_form.Finalize();
    state.Kpp.reset(Kpp_form.ParallelAssemble());

    ParMixedBilinearForm Kup_form(
        state.scalar_fes.get(), state.displacement_fes.get());
    auto* Kup_integrator = new ImplicitSurfaceNormalIntegrator(
        *state.phi, cut_integration_order, level_set_order, -1.0, false);
    Kup_form.AddDomainIntegrator(Kup_integrator, design_marker);
    Kup_form.Assemble();
    Kup_form.Finalize();
    std::unique_ptr<HypreParMatrix> Kup(Kup_form.ParallelAssemble());

    ParMixedBilinearForm Mpu_form(
        state.displacement_fes.get(), state.scalar_fes.get());
    auto* Mpu_integrator = new ImplicitSurfaceNormalIntegrator(
        *state.phi, cut_integration_order, level_set_order, 1.0, true);
    Mpu_form.AddDomainIntegrator(Mpu_integrator, design_marker);
    Mpu_form.Assemble();
    Mpu_form.Finalize();
    std::unique_ptr<HypreParMatrix> Mpu(Mpu_form.ParallelAssemble());
    int local_degenerate_normals = std::max(
        Kup_integrator->GetDegenerateNormalCount(),
        Mpu_integrator->GetDegenerateNormalCount());
    int degenerate_normals = 0;
    MPI_Allreduce(&local_degenerate_normals, &degenerate_normals,
                  1, MPI_INT, MPI_SUM, state.comm);
    if (degenerate_normals != 0) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The distributed implicit interface contains "
                    + std::to_string(degenerate_normals)
                    + " quadrature points with an undefined normal.");
        }
        return false;
    }

    const real_t omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const real_t omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const real_t alpha_d = 2.0 * physics->zeta * omega_1 * omega_2
        / (omega_1 + omega_2);
    const real_t beta_d = 2.0 * physics->zeta / (omega_1 + omega_2);
    state.Cuu.reset(Add(alpha_d, *state.Muu, beta_d, *state.Kuu));

    const int inlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::left)
        : static_cast<int>(CartesianBoundary2D::left);
    const int outlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::right)
        : static_cast<int>(CartesianBoundary2D::right);
    bool cpp_is_finite = true;
    if (!state.Cpp) {
        Array<int> absorbing_marker(state.mesh->bdr_attributes.Max());
        absorbing_marker = 0;
        absorbing_marker[inlet_boundary - 1] = 1;
        absorbing_marker[outlet_boundary - 1] = 1;
        ConstantCoefficient inverse_impedance(
            1.0 / (physics->rho_a * physics->c_a));
        ParBilinearForm Cpp_form(state.scalar_fes.get());
        Cpp_form.AddBoundaryIntegrator(
            new BoundaryMassIntegrator(inverse_impedance), absorbing_marker);
        Cpp_form.Assemble();
        Cpp_form.Finalize();
        cpp_is_finite = Cpp_form.SpMat().CheckFinite() == 0;
        state.Cpp.reset(Cpp_form.ParallelAssemble());
    }

    std::unique_ptr<SparseMatrix> Kup_transpose(
        Transpose(Kup_form.SpMat()));
    std::unique_ptr<SparseMatrix> coupling_residual(
        Add(1.0, Mpu_form.SpMat(), 1.0, *Kup_transpose));
    const bool coupling_ready = coupling_residual->MaxNorm()
        <= 1.0e-10 * std::max({
            real_t{1.0}, Mpu_form.SpMat().MaxNorm(),
            Kup_transpose->MaxNorm()});
    if (!all_succeeded(state.comm, coupling_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The parallel coupling matrices do not satisfy Mpu = -Kup^T.");
        }
        return false;
    }

    state.M = block_matrix(state.Muu.get(), nullptr, Mpu.get(), state.Mpp.get());
    state.C = block_matrix(state.Cuu.get(), nullptr, nullptr, state.Cpp.get());
    state.K = block_matrix(state.Kuu.get(), Kup.get(), nullptr, state.Kpp.get());
    const bool matrices_are_finite = Muu_form.SpMat().CheckFinite() == 0
        && Kuu_form.SpMat().CheckFinite() == 0
        && Mpp_form.SpMat().CheckFinite() == 0
        && Kpp_form.SpMat().CheckFinite() == 0
        && Kup_form.SpMat().CheckFinite() == 0
        && Mpu_form.SpMat().CheckFinite() == 0
        && cpp_is_finite;
    if (!all_succeeded(state.comm, matrices_are_finite)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The assembled finite element matrices contain non-finite values.");
        }
        return false;
    }

    const int displacement_size = state.displacement_fes->GetTrueVSize();
    const int pressure_size = state.scalar_fes->GetTrueVSize();
    if (!state.boundary_data_ready) {
        Array<int> clamped_marker(state.mesh->bdr_attributes.Max());
        clamped_marker = 0;
        if (dim == 3) {
            clamped_marker[static_cast<int>(CartesianBoundary3D::bottom) - 1] = 1;
            clamped_marker[static_cast<int>(CartesianBoundary3D::top) - 1] = 1;
        }
        else {
            clamped_marker[static_cast<int>(CartesianBoundary2D::bottom) - 1] = 1;
            clamped_marker[static_cast<int>(CartesianBoundary2D::top) - 1] = 1;
        }
        state.displacement_fes->GetEssentialTrueDofs(
            clamped_marker, state.displacement_essential_tdofs);

        Array<int> inlet_marker(state.mesh->bdr_attributes.Max());
        Array<int> outlet_marker(state.mesh->bdr_attributes.Max());
        inlet_marker = 0;
        outlet_marker = 0;
        inlet_marker[inlet_boundary - 1] = 1;
        outlet_marker[outlet_boundary - 1] = 1;
        ConstantCoefficient one(1.0);
        ParLinearForm inlet_form(state.scalar_fes.get());
        inlet_form.AddBoundaryIntegrator(
            new BoundaryLFIntegrator(one), inlet_marker);
        inlet_form.Assemble();
        inlet_form.ParallelAssemble(state.inlet_load);
        ParLinearForm outlet_form(state.scalar_fes.get());
        outlet_form.AddBoundaryIntegrator(
            new BoundaryLFIntegrator(one), outlet_marker);
        outlet_form.Assemble();
        outlet_form.ParallelAssemble(state.outlet_functional);

        state.system_inlet_load.SetSize(displacement_size + pressure_size);
        state.system_inlet_load = 0.0;
        state.system_inlet_load.SetVector(state.inlet_load, displacement_size);
        state.system_outlet_functional.SetSize(displacement_size + pressure_size);
        state.system_outlet_functional = 0.0;
        state.system_outlet_functional.SetVector(
            state.outlet_functional, displacement_size);

        Vector ones(state.outlet_functional.Size());
        ones = 1.0;
        const double inlet_measure = global_dot(
            state.comm, state.inlet_load, ones);
        const double outlet_measure = global_dot(
            state.comm, state.outlet_functional, ones);
        const double expected_measure = dim == 3
            ? settings.sy * settings.sz : settings.sy;
        const double measure_tolerance =
            1.0e-10 * std::max(1.0, expected_measure);
        const bool boundary_measures_are_valid =
            std::abs(inlet_measure - expected_measure) <= measure_tolerance
            && std::abs(outlet_measure - expected_measure) <= measure_tolerance;
        if (!all_succeeded(state.comm, boundary_measures_are_valid)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The parallel inlet/outlet functionals have the wrong measure.");
            }
            return false;
        }
        state.boundary_data_ready = true;
    }

    result.stateSize = state.M->Height();
    result.displacementSize = displacement_size;
    result.pressureSize = pressure_size;
    result.pressureOffset = displacement_size;
    result.timeSteps = time_steps;
    result.dt = settings.dt;
    performance_data.assemblySeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started_at).count();
    if (state.rank == 0) {
        log(LogLevel::Message,
            "Assembled distributed vibroacoustic matrices on "
                + std::to_string(state.ranks) + " MPI ranks.");
    }
    return true;
#endif
}

bool App::Solver::solve(bool parallel)
{
    if (!parallel) {
        return solve();
    }

#if !METAMATERIAL_USE_MPI
    log(LogLevel::Error,
        "The parallel solver requires a parallel-cpu build.");
    return false;
#else
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    broadcast_command(
        MPI_COMM_WORLD,
        rank,
        parallel_state ? parallel_state->command_depth : 0,
        ParallelCommand::solve);
    const bool solver_ready = parallel_state && mesh;
    if (!all_succeeded(MPI_COMM_WORLD, solver_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Call setMesh(true) before the parallel solve.");
        }
        status.store(SolverStatus::Error);
        return false;
    }
    ParallelState& state = *parallel_state;
    ++state.command_depth;
    DepthGuard depth_guard{state.command_depth};
    status.store(SolverStatus::Working);
    result.success = 0;
    frequency_response = {};
    performance_data.forwardFgmresIterations = 0;
    performance_data.forwardFgmresSolves = 0;
    performance_data.maximumForwardFgmresIterations = 0;
    performance_data.mumpsInitialFactorizationSeconds = 0.0;
    performance_data.mumpsEffectiveFactorizationSeconds = 0.0;
    performance_data.mumpsSolveSeconds = 0.0;
    performance_data.maximumForwardResidual = 0.0;

    const Vector designed_geometry(lset.design);
    auto fail = [this, &state, &designed_geometry](SolverStatus failure) {
        lset.design = designed_geometry;
        state.reference_ready = false;
        status.store(failure);
        return false;
    };

    const int first_analysis = state.reference_ready ? 1 : 0;
    bool designed_assembly_ready = state.M && state.C && state.K;
    for (int analysis = first_analysis; analysis < 2; ++analysis) {
        const bool reference_analysis = analysis == 0;
        if (reference_analysis) {
            state.reference_ready = false;
            lset.design = 0.0;
        }
        else {
            lset.design = designed_geometry;
        }
        if ((reference_analysis || !designed_assembly_ready)
            && !assembleSolutionSpace(true)) {
            return fail(SolverStatus::Error);
        }
        designed_assembly_ready = !reference_analysis;

        if (!reference_analysis && state.rank == 0) {
            streamToGlvis();
        }

        const auto* physics =
            std::get_if<VibroacousticSettings>(&settings.physics);
        const double beta = settings.newmarkBeta;
        const double gamma = settings.newmarkGamma;
        if (physics == nullptr || beta <= 0.0 || gamma < 0.5
            || beta < 0.25 * std::pow(gamma + 0.5, 2.0)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The Newmark parameters do not satisfy the stability bound.");
            }
            return fail(SolverStatus::Error);
        }

        const double a_3 = gamma / (beta * settings.dt);
        const double a_6 = 1.0 / (beta * settings.dt * settings.dt);
        std::unique_ptr<HypreParMatrix> M_and_C(
            Add(a_6, *state.M, a_3, *state.C));
        std::unique_ptr<HypreParMatrix> K_hat(
            Add(1.0, *state.K, 1.0, *M_and_C));
        K_hat->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);

        std::unique_ptr<HypreParMatrix> Muu_and_Cuu(
            Add(a_6, *state.Muu, a_3, *state.Cuu));
        std::unique_ptr<HypreParMatrix> effective_displacement(
            Add(1.0, *state.Kuu, 1.0, *Muu_and_Cuu));
        std::unique_ptr<HypreParMatrix> Mpp_and_Cpp(
            Add(a_6, *state.Mpp, a_3, *state.Cpp));
        std::unique_ptr<HypreParMatrix> effective_pressure(
            Add(1.0, *state.Kpp, 1.0, *Mpp_and_Cpp));
        effective_displacement->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);

        std::unique_ptr<HypreParMatrix> M_system(
            new HypreParMatrix(*state.M));
        M_system->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);
        std::unique_ptr<HypreParMatrix> initial_displacement(
            new HypreParMatrix(*state.Muu));
        initial_displacement->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);
        std::unique_ptr<HypreParMatrix> initial_pressure(
            new HypreParMatrix(*state.Mpp));

        Array<int> offsets(3);
        offsets[0] = 0;
        offsets[1] = state.displacement_fes->GetTrueVSize();
        offsets[2] = K_hat->Height();
        detail::LinearSolve solve_initial;
        detail::LinearSolve solve_effective;

        std::unique_ptr<BlockDiagonalPreconditioner> initial_preconditioner;
        std::unique_ptr<BlockDiagonalPreconditioner> effective_preconditioner;
        std::unique_ptr<FGMRESSolver> initial_fgmres;
        std::unique_ptr<FGMRESSolver> effective_fgmres;

        if (settings.linearSolveMethod == LinearSolveMethod::mumps) {
            if (!state.initial_mumps) {
                state.initial_mumps = std::make_unique<MUMPSSolver>(state.comm);
                state.effective_mumps = std::make_unique<MUMPSSolver>(state.comm);
                for (MUMPSSolver* direct : {
                         state.initial_mumps.get(), state.effective_mumps.get()}) {
                    direct->SetPrintLevel(0);
                    direct->SetMatrixSymType(MUMPSSolver::UNSYMMETRIC);
                    direct->SetReorderingStrategy(MUMPSSolver::PORD);
                }
            }
            auto factor_started_at = std::chrono::steady_clock::now();
            state.initial_mumps->SetOperator(*M_system);
            performance_data.mumpsInitialFactorizationSeconds +=
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - factor_started_at).count();
            factor_started_at = std::chrono::steady_clock::now();
            state.effective_mumps->SetOperator(*K_hat);
            performance_data.mumpsEffectiveFactorizationSeconds +=
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - factor_started_at).count();
            solve_initial = [&state, this](
                                const Vector& rhs, Vector& solution) {
                const auto started_at = std::chrono::steady_clock::now();
                state.initial_mumps->Mult(rhs, solution);
                performance_data.mumpsSolveSeconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started_at).count();
                return detail::LinearSolveResult{};
            };
            solve_effective = [&state, this](
                                  const Vector& rhs, Vector& solution) {
                const auto started_at = std::chrono::steady_clock::now();
                state.effective_mumps->Mult(rhs, solution);
                performance_data.mumpsSolveSeconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started_at).count();
                return detail::LinearSolveResult{};
            };
        }
        else {
            state.initial_u_amg = std::make_unique<HypreBoomerAMG>(
                *initial_displacement);
            state.initial_u_amg->SetElasticityOptions(
                state.displacement_fes.get());
            state.initial_p_amg = std::make_unique<HypreBoomerAMG>(
                *initial_pressure);
            state.effective_u_amg = std::make_unique<HypreBoomerAMG>(
                *effective_displacement);
            state.effective_u_amg->SetElasticityOptions(
                state.displacement_fes.get());
            state.effective_p_amg = std::make_unique<HypreBoomerAMG>(
                *effective_pressure);
            initial_preconditioner =
                std::make_unique<BlockDiagonalPreconditioner>(offsets);
            initial_preconditioner->SetDiagonalBlock(
                0, state.initial_u_amg.get());
            initial_preconditioner->SetDiagonalBlock(
                1, state.initial_p_amg.get());
            effective_preconditioner =
                std::make_unique<BlockDiagonalPreconditioner>(offsets);
            effective_preconditioner->SetDiagonalBlock(
                0, state.effective_u_amg.get());
            effective_preconditioner->SetDiagonalBlock(
                1, state.effective_p_amg.get());

            auto configure = [](FGMRESSolver& solver,
                                const Operator& matrix,
                                mfem::Solver& preconditioner) {
                solver.SetPreconditioner(preconditioner);
                solver.SetOperator(matrix);
                solver.iterative_mode = true;
                solver.SetKDim(50);
                solver.SetRelTol(1.0e-10);
                solver.SetAbsTol(1.0e-12);
                solver.SetMaxIter(1500);
                solver.SetPrintLevel(-1);
            };
            initial_fgmres = std::make_unique<FGMRESSolver>(state.comm);
            effective_fgmres = std::make_unique<FGMRESSolver>(state.comm);
            configure(
                *initial_fgmres, *M_system, *initial_preconditioner);
            configure(
                *effective_fgmres, *K_hat, *effective_preconditioner);
            solve_initial = [&initial_fgmres](
                                const Vector& rhs, Vector& solution) {
                initial_fgmres->Mult(rhs, solution);
                return detail::LinearSolveResult{
                    initial_fgmres->GetConverged(),
                    initial_fgmres->GetNumIterations(),
                    initial_fgmres->GetFinalRelNorm()};
            };
            solve_effective = [&effective_fgmres](
                                  const Vector& rhs, Vector& solution) {
                effective_fgmres->Mult(rhs, solution);
                return detail::LinearSolveResult{
                    effective_fgmres->GetConverged(),
                    effective_fgmres->GetNumIterations(),
                    effective_fgmres->GetFinalRelNorm()};
            };
        }

        std::vector<double>& measured_outlet = reference_analysis
            ? reference_outlet_pressure : outlet_pressure;
        const auto transient_started_at = std::chrono::steady_clock::now();
        const LogFunction rank_log = state.rank == 0
            ? log
            : LogFunction([](LogLevel, std::string) {});
        const bool transient_ready = detail::runNewmark(
                settings,
                *physics,
                *state.M,
                *state.C,
                *state.K,
                *M_system,
                *K_hat,
                state.displacement_essential_tdofs,
                state.system_inlet_load,
                state.system_outlet_functional,
                source_pressure_derivative,
                solve_initial,
                solve_effective,
                [&state](const Vector& value) {
                    return global_norm(state.comm, value);
                },
                [&state](const Vector& left, const Vector& right) {
                    return global_dot(state.comm, left, right);
                },
                !reference_analysis,
                result.U,
                result.residualNorms,
                measured_outlet,
                performance_data,
                rank_log,
                reference_analysis ? "empty-duct reference" : "designed duct");
        if (!all_succeeded(state.comm, transient_ready)) {
            state.initial_u_amg.reset();
            state.initial_p_amg.reset();
            state.effective_u_amg.reset();
            state.effective_p_amg.reset();
            return fail(SolverStatus::Diverged);
        }
        const double transient_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - transient_started_at).count();
        if (reference_analysis) {
            state.initial_u_amg.reset();
            state.initial_p_amg.reset();
            state.effective_u_amg.reset();
            state.effective_p_amg.reset();
            performance_data.referenceTransientSeconds = transient_seconds;
            state.reference_ready = true;
        }
        else {
            performance_data.designedTransientSeconds = transient_seconds;
            state.initial_matrix = std::move(M_system);
            state.effective_matrix = std::move(K_hat);
            state.initial_displacement = std::move(initial_displacement);
            state.initial_pressure = std::move(initial_pressure);
            state.effective_displacement = std::move(effective_displacement);
            state.effective_pressure = std::move(effective_pressure);
        }
    }

    lset.design = designed_geometry;
    const auto fourier_started_at = std::chrono::steady_clock::now();
    const bool fourier_ready = postprocessFourierResponse();
    if (!all_succeeded(state.comm, fourier_ready)) {
        return fail(SolverStatus::Error);
    }
    performance_data.fourierSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - fourier_started_at).count();
    result.success = 1;
    status.store(SolverStatus::Converged);
    if (state.rank == 0) {
        log(LogLevel::Message,
            "Completed the distributed Newmark solve and outlet FFT.");
    }
    return true;
#endif
}

bool App::Solver::differentiateFrequencyResponses(
    const std::vector<std::complex<double>>& pass_spectrum_derivative,
    const std::vector<std::complex<double>>& stop_spectrum_derivative,
    Vector& pass_design_gradient,
    Vector& stop_design_gradient,
    bool parallel)
{
    if (!parallel) {
        return differentiateFrequencyResponses(
            pass_spectrum_derivative,
            stop_spectrum_derivative,
            pass_design_gradient,
            stop_design_gradient);
    }

#if !METAMATERIAL_USE_MPI
    log(LogLevel::Error,
        "The parallel adjoint requires a parallel-cpu build.");
    return false;
#else
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    broadcast_command(
        MPI_COMM_WORLD,
        rank,
        parallel_state ? parallel_state->command_depth : 0,
        ParallelCommand::differentiate);
    const bool adjoint_ready = static_cast<bool>(parallel_state);
    if (!all_succeeded(MPI_COMM_WORLD, adjoint_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Run the parallel forward solver before its adjoint.");
        }
        return false;
    }
    ParallelState& state = *parallel_state;
    ++state.command_depth;
    DepthGuard depth_guard{state.command_depth};

    std::vector<std::complex<double>> pass = pass_spectrum_derivative;
    std::vector<std::complex<double>> stop = stop_spectrum_derivative;
    broadcast_complex_vector(state.comm, state.rank, pass);
    broadcast_complex_vector(state.comm, state.rank, stop);
    const int time_steps = result.timeSteps;
    const int true_state_size = result.stateSize;
    const std::size_t spectrum_size = frequency_response.outlet.size();
    const bool has_pass = !pass.empty();
    const bool has_stop = !stop.empty();
    auto valid_derivative = [spectrum_size, time_steps](
                                const std::vector<std::complex<double>>& value) {
        return value.empty()
            || (value.size() == spectrum_size
                && value.size()
                    == static_cast<std::size_t>(time_steps / 2 + 1));
    };
    const bool forward_ready = state.M && state.C && state.effective_matrix
        && state.initial_matrix && state.initial_displacement
        && state.initial_pressure && state.effective_displacement
        && state.effective_pressure && state.phi
        && (state.rank != 0
            || (design_to_cell && cell_to_level_set && filter_matrix))
        && time_steps > 0 && true_state_size > 0
        && (has_pass || has_stop)
        && result.U.size() == static_cast<std::size_t>(time_steps + 1)
        && fft_window.size() == static_cast<std::size_t>(time_steps)
        && valid_derivative(pass) && valid_derivative(stop);
    if (!all_succeeded(state.comm, forward_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The distributed forward history is incomplete for the adjoint.");
        }
        return false;
    }

    std::vector<double> pass_outlet_derivative;
    std::vector<double> stop_outlet_derivative;
    const bool inverse_ready = detail::inverseOutletDerivative(
            pass, time_steps, fft_window, pass_outlet_derivative)
        && detail::inverseOutletDerivative(
            stop, time_steps, fft_window, stop_outlet_derivative);
    if (!all_succeeded(state.comm, inverse_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "FFTW could not create the distributed adjoint transform.");
        }
        return false;
    }

    std::unique_ptr<HypreParMatrix> effective_transpose(
        state.effective_matrix->Transpose());
    std::unique_ptr<HypreParMatrix> initial_transpose(
        state.initial_matrix->Transpose());
    detail::LinearSolve solve_effective_transpose;
    detail::LinearSolve solve_initial_transpose;
    Array<int> offsets(3);
    offsets[0] = 0;
    offsets[1] = state.displacement_fes->GetTrueVSize();
    offsets[2] = true_state_size;

    std::unique_ptr<BlockDiagonalPreconditioner> initial_preconditioner;
    std::unique_ptr<BlockDiagonalPreconditioner> effective_preconditioner;
    std::unique_ptr<FGMRESSolver> initial_fgmres;
    std::unique_ptr<FGMRESSolver> effective_fgmres;
    if (settings.linearSolveMethod == LinearSolveMethod::mumps) {
        const bool factors_ready = state.initial_mumps
            && state.effective_mumps;
        if (!all_succeeded(state.comm, factors_ready)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The designed MUMPS factors are unavailable for the adjoint.");
            }
            return false;
        }
        solve_initial_transpose = [&state, this](
                                      const Vector& rhs, Vector& solution) {
            const auto started_at = std::chrono::steady_clock::now();
            state.initial_mumps->MultTranspose(rhs, solution);
            performance_data.mumpsSolveSeconds += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
            return detail::LinearSolveResult{};
        };
        solve_effective_transpose = [&state, this](
                                        const Vector& rhs, Vector& solution) {
            const auto started_at = std::chrono::steady_clock::now();
            state.effective_mumps->MultTranspose(rhs, solution);
            performance_data.mumpsSolveSeconds += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
            return detail::LinearSolveResult{};
        };
    }
    else {
        const bool preconditioners_ready = state.initial_u_amg
            && state.initial_p_amg && state.effective_u_amg
            && state.effective_p_amg;
        if (!all_succeeded(state.comm, preconditioners_ready)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The designed AMG preconditioners are unavailable for the adjoint.");
            }
            return false;
        }
        initial_preconditioner =
            std::make_unique<BlockDiagonalPreconditioner>(offsets);
        initial_preconditioner->SetDiagonalBlock(0, state.initial_u_amg.get());
        initial_preconditioner->SetDiagonalBlock(1, state.initial_p_amg.get());
        effective_preconditioner =
            std::make_unique<BlockDiagonalPreconditioner>(offsets);
        effective_preconditioner->SetDiagonalBlock(
            0, state.effective_u_amg.get());
        effective_preconditioner->SetDiagonalBlock(
            1, state.effective_p_amg.get());

        auto configure = [](FGMRESSolver& solver,
                            const Operator& matrix,
                            mfem::Solver& preconditioner) {
            solver.SetPreconditioner(preconditioner);
            solver.SetOperator(matrix);
            solver.iterative_mode = true;
            solver.SetKDim(50);
            solver.SetRelTol(1.0e-10);
            solver.SetAbsTol(1.0e-12);
            solver.SetMaxIter(1500);
            solver.SetPrintLevel(-1);
        };
        initial_fgmres = std::make_unique<FGMRESSolver>(state.comm);
        effective_fgmres = std::make_unique<FGMRESSolver>(state.comm);
        configure(*initial_fgmres, *initial_transpose, *initial_preconditioner);
        configure(*effective_fgmres,
                  *effective_transpose,
                  *effective_preconditioner);
        solve_initial_transpose = [&initial_fgmres](
                                      const Vector& rhs, Vector& solution) {
            initial_fgmres->Mult(rhs, solution);
            return detail::LinearSolveResult{
                initial_fgmres->GetConverged(),
                initial_fgmres->GetNumIterations(),
                initial_fgmres->GetFinalRelNorm()};
        };
        solve_effective_transpose = [&effective_fgmres](
                                        const Vector& rhs, Vector& solution) {
            effective_fgmres->Mult(rhs, solution);
            return detail::LinearSolveResult{
                effective_fgmres->GetConverged(),
                effective_fgmres->GetNumIterations(),
                effective_fgmres->GetFinalRelNorm()};
        };
    }

    performance_data.passAdjointSeconds = 0.0;
    performance_data.stopAdjointSeconds = 0.0;
    performance_data.adjointFgmresIterations = 0;
    performance_data.adjointFgmresSolves = 0;
    performance_data.maximumAdjointFgmresIterations = 0;
    Vector pass_initial_adjoint;
    Vector stop_initial_adjoint;
    const LogFunction rank_log = state.rank == 0
        ? log : LogFunction([](LogLevel, std::string) {});
    const bool pass_adjoint_ready = !has_pass || detail::runNewmarkAdjoint(
            settings,
            *state.M,
            *state.C,
            *effective_transpose,
            *initial_transpose,
            state.displacement_essential_tdofs,
            state.system_outlet_functional,
            pass_outlet_derivative,
            solve_effective_transpose,
            solve_initial_transpose,
            [&state](const Vector& value) {
                return global_norm(state.comm, value);
            },
            pass_adjoint_history,
            pass_initial_adjoint,
            performance_data,
            performance_data.passAdjointSeconds,
            rank_log,
            "pass-band");
    if (!all_succeeded(state.comm, pass_adjoint_ready)) {
        return false;
    }
    const bool stop_adjoint_ready = !has_stop || detail::runNewmarkAdjoint(
            settings,
            *state.M,
            *state.C,
            *effective_transpose,
            *initial_transpose,
            state.displacement_essential_tdofs,
            state.system_outlet_functional,
            stop_outlet_derivative,
            solve_effective_transpose,
            solve_initial_transpose,
            [&state](const Vector& value) {
                return global_norm(state.comm, value);
            },
            stop_adjoint_history,
            stop_initial_adjoint,
            performance_data,
            performance_data.stopAdjointSeconds,
            rank_log,
            "stop-band");
    if (!all_succeeded(state.comm, stop_adjoint_ready)) {
        return false;
    }

    const int displacement_true_size =
        state.displacement_fes->GetTrueVSize();
    const int pressure_true_size = state.scalar_fes->GetTrueVSize();
    const int displacement_local_size = state.displacement_fes->GetVSize();
    const int pressure_local_size = state.scalar_fes->GetVSize();
    const int local_state_size =
        displacement_local_size + pressure_local_size;
    ParGridFunction displacement(state.displacement_fes.get());
    ParGridFunction pressure(state.scalar_fes.get());
    Vector displacement_true(displacement_true_size);
    Vector pressure_true(pressure_true_size);
    auto distribute_state = [&](const Vector& source,
                                int blocks,
                                Vector& distributed) {
        if (source.Size() != blocks * true_state_size) {
            return false;
        }
        distributed.SetSize(blocks * local_state_size);
        for (int block = 0; block < blocks; ++block) {
            for (int i = 0; i < displacement_true_size; ++i) {
                displacement_true[i] = source[block * true_state_size + i];
            }
            for (int i = 0; i < pressure_true_size; ++i) {
                pressure_true[i] = source[
                    block * true_state_size + displacement_true_size + i];
            }
            displacement.Distribute(displacement_true);
            pressure.Distribute(pressure_true);
            distributed.SetVector(displacement, block * local_state_size);
            distributed.SetVector(
                pressure, block * local_state_size + displacement_local_size);
        }
        return true;
    };
    auto distribute_history = [&](const std::vector<Vector>& source,
                                  int blocks,
                                  std::vector<Vector>& distributed) {
        distributed.resize(source.size());
        for (std::size_t n = 0; n < source.size(); ++n) {
            if (!distribute_state(source[n], blocks, distributed[n])) {
                return false;
            }
        }
        return true;
    };
    auto distribute_history_in_place = [&](std::vector<Vector>& history) {
        for (Vector& value : history) {
            Vector distributed;
            if (!distribute_state(value, 1, distributed)) {
                return false;
            }
            value = std::move(distributed);
        }
        return true;
    };
    std::vector<Vector> local_forward_history;
    const bool histories_ready = distribute_history(
            result.U, 3, local_forward_history)
        && (!has_pass || distribute_history_in_place(pass_adjoint_history))
        && (!has_stop || distribute_history_in_place(stop_adjoint_history));
    if (!all_succeeded(state.comm, histories_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "Could not distribute the forward/adjoint histories.");
        }
        return false;
    }
    Vector local_pass_initial;
    Vector local_stop_initial;
    const bool initial_states_ready =
        (!has_pass || distribute_state(
            pass_initial_adjoint, 1, local_pass_initial))
        && (!has_stop || distribute_state(
            stop_initial_adjoint, 1, local_stop_initial));
    if (!all_succeeded(state.comm, initial_states_ready)) {
        return false;
    }

    H1_FECollection global_collection(fe_order, mesh->Dimension());
    FiniteElementSpace global_level_set_fes(mesh.get(), &global_collection);
    Array<int> local_to_global(state.phi->Size());
    local_to_global = -1;
    int local_element = 0;
    bool dof_map_ready = true;
    Array<int> local_dofs;
    Array<int> global_dofs;
    for (int global_element = 0;
         global_element < mesh->GetNE();
         ++global_element) {
        if (state.partition[global_element] != state.rank) {
            continue;
        }
        state.phi->FESpace()->GetElementDofs(local_element, local_dofs);
        global_level_set_fes.GetElementDofs(global_element, global_dofs);
        if (local_dofs.Size() != global_dofs.Size()) {
            dof_map_ready = false;
            break;
        }
        for (int i = 0; i < local_dofs.Size(); ++i) {
            const int local_dof = local_dofs[i] >= 0
                ? local_dofs[i] : -1 - local_dofs[i];
            const int global_dof = global_dofs[i] >= 0
                ? global_dofs[i] : -1 - global_dofs[i];
            local_to_global[local_dof] = global_dof;
        }
        ++local_element;
    }
    if (!all_succeeded(state.comm, dof_map_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "Could not map distributed level-set DOFs to the global design.");
        }
        return false;
    }
    Array<int> active_global(global_level_set_fes.GetVSize());
    active_global = 0;
    for (int i = 0; i < lset.activeDesignDofs.Size(); ++i) {
        active_global[lset.activeDesignDofs[i]] = 1;
    }
    Array<int> active_local(local_to_global.Size());
    active_local = 0;
    for (int local_dof = 0; local_dof < local_to_global.Size(); ++local_dof) {
        if (local_to_global[local_dof] >= 0) {
            active_local[local_dof] = active_global[local_to_global[local_dof]];
        }
    }

    Vector local_pass_gradient;
    Vector local_stop_gradient;
    int local_cut_elements = 0;
    int local_differentiated_dofs = 0;
    const auto differentiation_started_at = std::chrono::steady_clock::now();
    const bool cut_derivative_ready = detail::differentiateCutElements(
            settings,
            optimizer_settings,
            *state.mesh,
            *state.phi->FESpace(),
            *state.displacement_fes,
            *state.scalar_fes,
            *state.phi,
            active_local,
            local_forward_history,
            has_pass ? &pass_adjoint_history : nullptr,
            has_stop ? &stop_adjoint_history : nullptr,
            has_pass ? &local_pass_initial : nullptr,
            has_stop ? &local_stop_initial : nullptr,
            displacement_local_size,
            time_steps,
            static_cast<int>(DomainAttribute::design),
            cut_integration_order,
            level_set_order,
            level_set_scale,
            local_pass_gradient,
            local_stop_gradient,
            local_cut_elements,
            local_differentiated_dofs);
    if (!all_succeeded(state.comm, cut_derivative_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The distributed cut-element differentiation failed.");
        }
        return false;
    }

    Vector pass_contribution(global_level_set_fes.GetVSize());
    Vector stop_contribution(global_level_set_fes.GetVSize());
    pass_contribution = 0.0;
    stop_contribution = 0.0;
    for (int local_dof = 0; local_dof < local_to_global.Size(); ++local_dof) {
        const int global_dof = local_to_global[local_dof];
        if (global_dof >= 0) {
            pass_contribution[global_dof] += local_pass_gradient[local_dof];
            stop_contribution[global_dof] += local_stop_gradient[local_dof];
        }
    }
    Vector pass_physical_gradient;
    Vector stop_physical_gradient;
    if (state.rank == 0) {
        pass_physical_gradient.SetSize(pass_contribution.Size());
        stop_physical_gradient.SetSize(stop_contribution.Size());
    }
    MPI_Reduce(
        pass_contribution.GetData(),
        state.rank == 0 ? pass_physical_gradient.GetData() : nullptr,
        pass_contribution.Size(), MPI_DOUBLE, MPI_SUM, 0, state.comm);
    MPI_Reduce(
        stop_contribution.GetData(),
        state.rank == 0 ? stop_physical_gradient.GetData() : nullptr,
        stop_contribution.Size(), MPI_DOUBLE, MPI_SUM, 0, state.comm);
    MPI_Allreduce(&local_cut_elements, &performance_data.cutElements,
                  1, MPI_INT, MPI_SUM, state.comm);
    MPI_Allreduce(&local_differentiated_dofs,
                  &performance_data.differentiatedDofs,
                  1, MPI_INT, MPI_SUM, state.comm);
    const double local_differentiation_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - differentiation_started_at).count();
    MPI_Allreduce(&local_differentiation_seconds,
                  &performance_data.cutDifferentiationSeconds,
                  1, MPI_DOUBLE, MPI_MAX, state.comm);

    int filter_success = 1;
    if (state.rank == 0) {
        filter_success = detail::reverseFilterGradients(
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
            log);
    }
    MPI_Bcast(&filter_success, 1, MPI_INT, 0, state.comm);
    if (filter_success && state.rank == 0) {
        log(LogLevel::Message,
            "Completed the distributed pass/stop adjoints and reduced the design gradients.");
    }
    return filter_success != 0;
#endif
}
