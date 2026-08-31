#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
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

void write_mumps_diagnostic(int rank, const std::string& message) noexcept
{
    try {
        const std::filesystem::path directory = "logs";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            std::cerr << "Could not create the MUMPS log directory: "
                      << error.message() << '\n';
            return;
        }

        const std::filesystem::path path =
            directory / ("mumps_rank_" + std::to_string(rank) + ".log");
        std::ofstream output(path, std::ios::app);
        if (!output) {
            std::cerr << "Could not open " << path << " for MUMPS logging.\n";
            return;
        }

        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        output << "timestamp_ms=" << timestamp
               << " rank=" << rank << ' ' << message << '\n';
        output.flush();
    }
    catch (...) {
        std::cerr << "Could not write the MUMPS diagnostic log.\n";
    }
}

void factor_mumps(
    MUMPSSolver& solver,
    const HypreParMatrix& matrix,
    int rank,
    int ranks,
    const std::string& name)
{
    std::ostringstream details;
    details << "event=factor_begin matrix=" << name
            << " ranks=" << ranks
            << " global_rows=" << matrix.GetGlobalNumRows()
            << " global_columns=" << matrix.GetGlobalNumCols()
            << " local_rows=" << matrix.Height()
            << " global_nnz=" << matrix.NNZ();
    write_mumps_diagnostic(rank, details.str());

    try {
        solver.SetOperator(matrix);
    }
    catch (const std::exception& error) {
        write_mumps_diagnostic(
            rank,
            "event=factor_error matrix=" + name
                + " exception=" + error.what());
        throw;
    }
    catch (...) {
        write_mumps_diagnostic(
            rank, "event=factor_error matrix=" + name
                + " exception=unknown");
        throw;
    }

    write_mumps_diagnostic(
        rank, "event=factor_complete matrix=" + name);
}

template <typename callback_t>
bool abort_on_distributed_exception(
    int rank,
    const char* stage,
    callback_t&& callback)
{
    try {
        return callback();
    }
    catch (const std::exception& error) {
        write_mumps_diagnostic(
            rank,
            "event=distributed_exception stage=" + std::string(stage)
                + " exception=" + error.what());
    }
    catch (...) {
        write_mumps_diagnostic(
            rank,
            "event=distributed_exception stage=" + std::string(stage)
                + " exception=unknown");
    }
    MPI_Abort(MPI_COMM_WORLD, 1);
    return false;
}

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
    App::SolverSettings& solver)
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
    broadcast_value(comm, solver.filterRadius);
    broadcast_value(comm, solver.cutDerivativeRelativeStep);
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
    std::unique_ptr<HypreParMatrix> reference_M;
    std::unique_ptr<HypreParMatrix> reference_C;
    std::unique_ptr<HypreParMatrix> reference_K;
    std::unique_ptr<HypreParMatrix> reference_effective_matrix;
    std::unique_ptr<HypreParMatrix> Muu;
    std::unique_ptr<HypreParMatrix> Cuu;
    std::unique_ptr<HypreParMatrix> Kuu;
    std::unique_ptr<HypreParMatrix> Mpp;
    std::unique_ptr<HypreParMatrix> Cpp;
    std::unique_ptr<HypreParMatrix> Kpp;
    std::unique_ptr<HypreParMatrix> reference_Muu;
    std::unique_ptr<HypreParMatrix> reference_Cuu;
    std::unique_ptr<HypreParMatrix> reference_Kuu;
    std::unique_ptr<HypreParMatrix> reference_Mpp;
    std::unique_ptr<HypreParMatrix> reference_Kpp;
    std::unique_ptr<HypreParMatrix> initial_matrix;
    std::unique_ptr<HypreParMatrix> effective_matrix;
    std::unique_ptr<HypreParMatrix> initial_displacement;
    std::unique_ptr<HypreParMatrix> initial_pressure;
    std::unique_ptr<HypreParMatrix> effective_displacement;
    std::unique_ptr<HypreParMatrix> effective_pressure;
    std::unique_ptr<HypreParMatrix> reference_effective_displacement;
    std::unique_ptr<HypreParMatrix> reference_effective_pressure;
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
    Array<int> active_design_dofs;
    Array<int> true_to_serial_design_dofs;
    bool boundary_data_ready = false;
    bool reference_ready = false;
};

App::Solver::Solver(App::SolverSettings& settings,
                    App::LevelSet& lset,
                    App::SolverResult& result,
                    const App::LogFunction& log)
    : settings(settings),
      lset(lset),
      result(result),
      log(log),
      fe_order(1),
      level_set_order(1),
      cut_integration_order(4)
{
}

App::Solver::~Solver() = default;

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
            if (parallel_state) {
                parallel_state->initial_mumps.reset();
                parallel_state->effective_mumps.reset();
            }
            parallel_workers_shutdown = true;
            MPI_Barrier(MPI_COMM_WORLD);
            return;
        }
        try {
            if (command == ParallelCommand::set_mesh) {
                setMeshParallelLocal();
            }
            else if (command == ParallelCommand::assemble) {
                assembleSolutionSpaceParallelLocal();
            }
            else if (command == ParallelCommand::solve) {
                solveParallelLocal();
            }
            else if (command == ParallelCommand::differentiate) {
                std::vector<std::complex<double>> pass;
                std::vector<std::complex<double>> stop;
                Vector pass_gradient;
                Vector stop_gradient;
                differentiateFrequencyResponsesParallelLocal(
                    pass, stop, pass_gradient, stop_gradient);
            }
            else {
                std::cerr << "Unknown parallel solver command on MPI rank "
                          << rank << ".\n";
                MPI_Abort(MPI_COMM_WORLD, 1);
                return;
            }
        }
        catch (const std::exception& error) {
            write_mumps_diagnostic(
                rank,
                "event=worker_error command="
                    + std::to_string(command_value)
                    + " exception=" + error.what());
            MPI_Abort(MPI_COMM_WORLD, 1);
            return;
        }
        catch (...) {
            write_mumps_diagnostic(
                rank,
                "event=worker_error command="
                    + std::to_string(command_value)
                    + " exception=unknown");
            MPI_Abort(MPI_COMM_WORLD, 1);
            return;
        }
    }
#endif
}

void App::Solver::shutdownParallelWorkers()
{
#if METAMATERIAL_USE_MPI
    if (parallel_workers_shutdown) {
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
    if (parallel_state) {
        parallel_state->initial_mumps.reset();
        parallel_state->effective_mumps.reset();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    parallel_workers_shutdown = true;
#endif
}

#if METAMATERIAL_USE_MPI
bool App::Solver::setMesh()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank != 0) {
        return false;
    }
    int command = static_cast<int>(ParallelCommand::set_mesh);
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return abort_on_distributed_exception(
        rank, "set_mesh", [this]() { return setMeshParallelLocal(); });
}

bool App::Solver::setMeshParallelLocal()
{
    int rank = 0;
    int ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
    write_mumps_diagnostic(rank, "event=stage_begin stage=set_mesh");
    if (!synchronize_settings(MPI_COMM_WORLD, rank, settings)) {
        if (rank == 0) {
            log(LogLevel::Error, "Could not broadcast the solver settings.");
        }
        return false;
    }
    write_mumps_diagnostic(rank, "event=stage_complete stage=settings_broadcast");

    if (parallel_state) {
        parallel_state->initial_mumps.reset();
        parallel_state->effective_mumps.reset();
    }
    parallel_state = std::make_unique<ParallelState>();
    parallel_state->rank = rank;
    parallel_state->ranks = ranks;
    performance_data.mpiRanks = ranks;
    parallel_workers_shutdown = false;
    const bool ready = buildDesignMesh();
    int all_ready = ready ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &all_ready, 1, MPI_INT, MPI_MIN,
                  parallel_state->comm);
    if (all_ready) {
        const int dim = mesh->Dimension();
        parallel_state->partition.resize(mesh->GetNE());
        for (int element = 0; element < mesh->GetNE(); ++element) {
            parallel_state->partition[element] = std::min(
                ranks - 1, element * ranks / mesh->GetNE());
        }
        parallel_state->mesh = std::make_unique<ParMesh>(
            parallel_state->comm, *mesh,
            parallel_state->partition.data());
        parallel_state->collection =
            std::make_unique<H1_FECollection>(fe_order, dim);
        parallel_state->scalar_fes =
            std::make_unique<ParFiniteElementSpace>(
                parallel_state->mesh.get(),
                parallel_state->collection.get());
        parallel_state->displacement_fes =
            std::make_unique<ParFiniteElementSpace>(
                parallel_state->mesh.get(),
                parallel_state->collection.get(),
                dim, Ordering::byVDIM);

        parallel_state->true_to_serial_design_dofs.SetSize(
            parallel_state->scalar_fes->GetTrueVSize());
        parallel_state->true_to_serial_design_dofs = -1;
        Array<int> local_dofs;
        Array<int> serial_dofs;
        int local_element = 0;
        bool design_dof_map_ready = true;
        for (int element = 0; element < mesh->GetNE(); ++element) {
            if (parallel_state->partition[element] != rank) {
                continue;
            }
            parallel_state->scalar_fes->GetElementDofs(
                local_element++, local_dofs);
            level_set_fes->GetElementDofs(element, serial_dofs);
            if (local_dofs.Size() != serial_dofs.Size()) {
                design_dof_map_ready = false;
                break;
            }
            for (int i = 0; i < local_dofs.Size(); ++i) {
                const int local_dof = local_dofs[i] >= 0
                    ? local_dofs[i] : -1 - local_dofs[i];
                const int serial_dof = serial_dofs[i] >= 0
                    ? serial_dofs[i] : -1 - serial_dofs[i];
                const int true_dof = parallel_state->scalar_fes
                    ->GetLocalTDofNumber(local_dof);
                if (true_dof >= 0) {
                    int& mapped =
                        parallel_state->true_to_serial_design_dofs[true_dof];
                    design_dof_map_ready = design_dof_map_ready
                        && (mapped < 0 || mapped == serial_dof);
                    mapped = serial_dof;
                }
            }
        }
        design_dof_map_ready = design_dof_map_ready
            && local_element == parallel_state->mesh->GetNE();
        for (int i = 0;
             i < parallel_state->true_to_serial_design_dofs.Size(); ++i) {
            design_dof_map_ready = design_dof_map_ready
                && parallel_state->true_to_serial_design_dofs[i] >= 0;
        }
        if (!all_succeeded(
                parallel_state->comm, design_dof_map_ready)) {
            all_ready = 0;
        }

        Vector active_true_dofs(level_set_fes->GetTrueVSize());
        active_true_dofs = 0.0;
        for (int i = 0; i < lset.activeDesignDofs.Size(); ++i) {
            active_true_dofs[lset.activeDesignDofs[i]] = 1.0;
        }
        GridFunction global_active_design(level_set_fes.get());
        global_active_design.SetFromTrueDofs(active_true_dofs);
        ParGridFunction active_design(
            parallel_state->mesh.get(), &global_active_design,
            parallel_state->partition.data());
        parallel_state->active_design_dofs.SetSize(active_design.Size());
        for (int dof = 0; dof < active_design.Size(); ++dof) {
            parallel_state->active_design_dofs[dof] =
                active_design[dof] > 0.5 ? 1 : 0;
        }
        write_mumps_diagnostic(
            rank, "event=stage_complete stage=parmesh");
    }
    if (all_ready && parallel_state->rank == 0) {
        log(LogLevel::Message,
            "Prepared the global mesh for "
                + std::to_string(parallel_state->ranks) + " MPI ranks.");
    }
    write_mumps_diagnostic(
        rank, all_ready
            ? "event=stage_complete stage=set_mesh"
            : "event=stage_error stage=set_mesh");
    return all_ready != 0;
}

bool App::Solver::assembleSolutionSpace()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank != 0) {
        return false;
    }
    int command = static_cast<int>(ParallelCommand::assemble);
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return abort_on_distributed_exception(
        rank, "assemble",
        [this]() { return assembleSolutionSpaceParallelLocal(); });
}

bool App::Solver::assembleSolutionSpaceParallelLocal()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const bool assembly_ready = parallel_state && mesh && mesh_is_ready
        && parallel_state->mesh && parallel_state->collection
        && parallel_state->scalar_fes
        && parallel_state->displacement_fes;
    if (!all_succeeded(MPI_COMM_WORLD, assembly_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Call setMesh() before parallel assembly.");
        }
        return false;
    }
    ParallelState& state = *parallel_state;
    write_mumps_diagnostic(
        state.rank, "event=stage_begin stage=assemble");
    assembly_is_ready = false;
    forward_is_ready = false;
    const auto started_at = std::chrono::steady_clock::now();

    // A new assembly invalidates every state, adjoint, factorization, and
    // preconditioner from the previous design.
    result.U.clear();
    result.residualNorms.clear();
    pass_adjoint_history.clear();
    stop_adjoint_history.clear();
    outlet_pressure.clear();
    state.initial_mumps.reset();
    state.effective_mumps.reset();
    state.initial_u_amg.reset();
    state.initial_p_amg.reset();
    state.effective_u_amg.reset();
    state.effective_p_amg.reset();
    state.initial_matrix.reset();
    state.effective_matrix.reset();
    state.initial_displacement.reset();
    state.initial_pressure.reset();
    state.effective_displacement.reset();
    state.effective_pressure.reset();
    state.M.reset();
    state.C.reset();
    state.K.reset();
    state.Muu.reset();
    state.Cuu.reset();
    state.Kuu.reset();
    state.Mpp.reset();
    state.Kpp.reset();
    state.phi.reset();

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
    Vector global_phi;
    if (state.rank == 0) {
        global_phi = lset.phi;
    }
    broadcast_vector(state.comm, state.rank, lset.design);
    broadcast_vector(state.comm, state.rank, global_phi);
    if (state.rank != 0) {
        lset.phi = global_phi;
    }
    MPI_Bcast(&level_set_scale, 1, MPI_DOUBLE, 0, state.comm);
    broadcast_vector(state.comm, state.rank, source_pressure);
    broadcast_vector(state.comm, state.rank, source_pressure_derivative);

    int time_steps = state.rank == 0 ? result.timeSteps : 0;
    MPI_Bcast(&time_steps, 1, MPI_INT, 0, state.comm);
    GridFunction global_level_set(level_set_fes.get());
    global_level_set.SetFromTrueDofs(global_phi);
    state.phi = std::make_unique<ParGridFunction>(
        state.mesh.get(), &global_level_set, state.partition.data());
    write_mumps_diagnostic(
        state.rank, "event=stage_complete stage=level_set_transfer");

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
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Muu");

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
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Kuu");

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
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Mpp");

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
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Kpp");

    ParMixedBilinearForm Kup_form(
        state.scalar_fes.get(), state.displacement_fes.get());
    auto* Kup_integrator = new ImplicitSurfaceNormalIntegrator(
        *state.phi, cut_integration_order, level_set_order, -1.0, false);
    Kup_form.AddDomainIntegrator(Kup_integrator, design_marker);
    Kup_form.Assemble();
    Kup_form.Finalize();
    std::unique_ptr<HypreParMatrix> Kup(Kup_form.ParallelAssemble());
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Kup");

    ParMixedBilinearForm Mpu_form(
        state.displacement_fes.get(), state.scalar_fes.get());
    auto* Mpu_integrator = new ImplicitSurfaceNormalIntegrator(
        *state.phi, cut_integration_order, level_set_order, 1.0, true);
    Mpu_form.AddDomainIntegrator(Mpu_integrator, design_marker);
    Mpu_form.Assemble();
    Mpu_form.Finalize();
    std::unique_ptr<HypreParMatrix> Mpu(Mpu_form.ParallelAssemble());
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Mpu");
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
    const real_t beta = settings.newmarkBeta;
    const real_t gamma = settings.newmarkGamma;
    if (beta <= 0.0 || gamma < 0.5
        || beta < 0.25 * std::pow(gamma + 0.5, 2.0)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "The Newmark parameters do not satisfy the stability bound.");
        }
        return false;
    }
    const real_t a_3 = gamma / (beta * settings.dt);
    const real_t a_6 = 1.0 / (beta * settings.dt * settings.dt);

    // Separately assembled Hypre matrices can have different off-process
    // column maps. Assemble linear combinations as forms so this works for N ranks.
    ConstantCoefficient damped_solid_density(alpha_d * physics->rho_s);
    ConstantCoefficient damped_solid_lambda(beta_d * lambda);
    ConstantCoefficient damped_solid_mu(beta_d * mu);
    ConstantCoefficient damped_fictitious_solid_density(
        physics->epsilon * alpha_d * physics->rho_s);
    ConstantCoefficient damped_fictitious_solid_lambda(
        physics->epsilon * beta_d * lambda);
    ConstantCoefficient damped_fictitious_solid_mu(
        physics->epsilon * beta_d * mu);
    ParBilinearForm Cuu_form(state.displacement_fes.get());
    Cuu_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<VectorMassIntegrator>(damped_solid_density),
        *state.phi, cut_integration_order, level_set_order,
        physics->epsilon, true), design_marker);
    Cuu_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<ElasticityIntegrator>(
            damped_solid_lambda, damped_solid_mu),
        *state.phi, cut_integration_order, level_set_order,
        physics->epsilon, true), design_marker);
    Cuu_form.AddDomainIntegrator(new VectorMassIntegrator(
        damped_fictitious_solid_density), fixed_air_marker);
    Cuu_form.AddDomainIntegrator(new ElasticityIntegrator(
        damped_fictitious_solid_lambda,
        damped_fictitious_solid_mu), fixed_air_marker);
    Cuu_form.Assemble();
    Cuu_form.Finalize();
    state.Cuu.reset(Cuu_form.ParallelAssemble());
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Cuu");

    const int inlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::left)
        : static_cast<int>(CartesianBoundary2D::left);
    const int outlet_boundary = dim == 3
        ? static_cast<int>(CartesianBoundary3D::right)
        : static_cast<int>(CartesianBoundary2D::right);
    Array<int> absorbing_marker(state.mesh->bdr_attributes.Max());
    absorbing_marker = 0;
    absorbing_marker[inlet_boundary - 1] = 1;
    absorbing_marker[outlet_boundary - 1] = 1;
    ConstantCoefficient inverse_impedance(
        1.0 / (physics->rho_a * physics->c_a));
    bool cpp_is_finite = true;
    if (!state.Cpp) {
        ParBilinearForm Cpp_form(state.scalar_fes.get());
        Cpp_form.AddBoundaryIntegrator(
            new BoundaryMassIntegrator(inverse_impedance), absorbing_marker);
        Cpp_form.Assemble();
        Cpp_form.Finalize();
        cpp_is_finite = Cpp_form.SpMat().CheckFinite() == 0;
        state.Cpp.reset(Cpp_form.ParallelAssemble());
        write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=Cpp");
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
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=M");
    state.C = block_matrix(state.Cuu.get(), nullptr, nullptr, state.Cpp.get());
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=C");
    state.K = block_matrix(state.Kuu.get(), Kup.get(), nullptr, state.Kpp.get());
    write_mumps_diagnostic(state.rank, "event=matrix_complete matrix=K");

    const real_t effective_mass_scale = a_6 + a_3 * alpha_d;
    const real_t effective_stiffness_scale = 1.0 + a_3 * beta_d;
    ConstantCoefficient effective_solid_density(
        effective_mass_scale * physics->rho_s);
    ConstantCoefficient effective_solid_lambda(
        effective_stiffness_scale * lambda);
    ConstantCoefficient effective_solid_mu(
        effective_stiffness_scale * mu);
    ConstantCoefficient effective_fictitious_solid_density(
        physics->epsilon * effective_mass_scale * physics->rho_s);
    ConstantCoefficient effective_fictitious_solid_lambda(
        physics->epsilon * effective_stiffness_scale * lambda);
    ConstantCoefficient effective_fictitious_solid_mu(
        physics->epsilon * effective_stiffness_scale * mu);
    ParBilinearForm effective_displacement_form(
        state.displacement_fes.get());
    effective_displacement_form.AddDomainIntegrator(
        new ImplicitDomainIntegrator(
            std::make_unique<VectorMassIntegrator>(effective_solid_density),
            *state.phi, cut_integration_order, level_set_order,
            physics->epsilon, true), design_marker);
    effective_displacement_form.AddDomainIntegrator(
        new ImplicitDomainIntegrator(
            std::make_unique<ElasticityIntegrator>(
                effective_solid_lambda, effective_solid_mu),
            *state.phi, cut_integration_order, level_set_order,
            physics->epsilon, true), design_marker);
    effective_displacement_form.AddDomainIntegrator(
        new VectorMassIntegrator(effective_fictitious_solid_density),
        fixed_air_marker);
    effective_displacement_form.AddDomainIntegrator(new ElasticityIntegrator(
        effective_fictitious_solid_lambda,
        effective_fictitious_solid_mu), fixed_air_marker);
    effective_displacement_form.Assemble();
    effective_displacement_form.Finalize();
    state.effective_displacement.reset(
        effective_displacement_form.ParallelAssemble());

    ConstantCoefficient effective_acoustic_mass(
        a_6 / (physics->rho_a * physics->c_a * physics->c_a));
    ConstantCoefficient effective_inverse_impedance(
        a_3 / (physics->rho_a * physics->c_a));
    ParBilinearForm effective_pressure_form(state.scalar_fes.get());
    effective_pressure_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<MassIntegrator>(effective_acoustic_mass),
        *state.phi, cut_integration_order, level_set_order,
        physics->epsilon, false), design_marker);
    effective_pressure_form.AddDomainIntegrator(new ImplicitDomainIntegrator(
        std::make_unique<DiffusionIntegrator>(acoustic_stiffness),
        *state.phi, cut_integration_order, level_set_order,
        physics->epsilon, false), design_marker);
    effective_pressure_form.AddDomainIntegrator(
        new MassIntegrator(effective_acoustic_mass), fixed_air_marker);
    effective_pressure_form.AddDomainIntegrator(
        new DiffusionIntegrator(acoustic_stiffness), fixed_air_marker);
    effective_pressure_form.AddBoundaryIntegrator(
        new BoundaryMassIntegrator(effective_inverse_impedance),
        absorbing_marker);
    effective_pressure_form.Assemble();
    effective_pressure_form.Finalize();
    state.effective_pressure.reset(
        effective_pressure_form.ParallelAssemble());

    Array2D<const HypreParMatrix*> effective_blocks(2, 2);
    effective_blocks(0, 0) = state.effective_displacement.get();
    effective_blocks(0, 1) = Kup.get();
    effective_blocks(1, 0) = Mpu.get();
    effective_blocks(1, 1) = state.effective_pressure.get();
    Array2D<real_t> effective_coefficients(2, 2);
    effective_coefficients = 1.0;
    effective_coefficients(1, 0) = a_6;
    state.effective_matrix.reset(HypreParMatrixFromBlocks(
        effective_blocks, &effective_coefficients));
    write_mumps_diagnostic(
        state.rank, "event=matrix_complete matrix=effective");

    bool reference_matrices_are_finite = true;
    if (!state.reference_M) {
        ParBilinearForm reference_Muu_form(state.displacement_fes.get());
        reference_Muu_form.AddDomainIntegrator(
            new VectorMassIntegrator(fictitious_solid_density));
        reference_Muu_form.Assemble();
        reference_Muu_form.Finalize();
        state.reference_Muu.reset(reference_Muu_form.ParallelAssemble());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_Muu");

        ParBilinearForm reference_Kuu_form(state.displacement_fes.get());
        reference_Kuu_form.AddDomainIntegrator(new ElasticityIntegrator(
            fictitious_solid_lambda, fictitious_solid_mu));
        reference_Kuu_form.Assemble();
        reference_Kuu_form.Finalize();
        state.reference_Kuu.reset(reference_Kuu_form.ParallelAssemble());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_Kuu");

        ParBilinearForm reference_Mpp_form(state.scalar_fes.get());
        reference_Mpp_form.AddDomainIntegrator(
            new MassIntegrator(acoustic_mass));
        reference_Mpp_form.Assemble();
        reference_Mpp_form.Finalize();
        state.reference_Mpp.reset(reference_Mpp_form.ParallelAssemble());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_Mpp");

        ParBilinearForm reference_Kpp_form(state.scalar_fes.get());
        reference_Kpp_form.AddDomainIntegrator(
            new DiffusionIntegrator(acoustic_stiffness));
        reference_Kpp_form.Assemble();
        reference_Kpp_form.Finalize();
        state.reference_Kpp.reset(reference_Kpp_form.ParallelAssemble());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_Kpp");

        ParBilinearForm reference_Cuu_form(state.displacement_fes.get());
        reference_Cuu_form.AddDomainIntegrator(
            new VectorMassIntegrator(damped_fictitious_solid_density));
        reference_Cuu_form.AddDomainIntegrator(new ElasticityIntegrator(
            damped_fictitious_solid_lambda,
            damped_fictitious_solid_mu));
        reference_Cuu_form.Assemble();
        reference_Cuu_form.Finalize();
        state.reference_Cuu.reset(reference_Cuu_form.ParallelAssemble());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_Cuu");

        state.reference_M = block_matrix(
            state.reference_Muu.get(), nullptr,
            nullptr, state.reference_Mpp.get());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_M");
        state.reference_C = block_matrix(
            state.reference_Cuu.get(), nullptr,
            nullptr, state.Cpp.get());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_C");
        state.reference_K = block_matrix(
            state.reference_Kuu.get(), nullptr,
            nullptr, state.reference_Kpp.get());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_K");

        ParBilinearForm reference_effective_displacement_form(
            state.displacement_fes.get());
        reference_effective_displacement_form.AddDomainIntegrator(
            new VectorMassIntegrator(effective_fictitious_solid_density));
        reference_effective_displacement_form.AddDomainIntegrator(
            new ElasticityIntegrator(
                effective_fictitious_solid_lambda,
                effective_fictitious_solid_mu));
        reference_effective_displacement_form.Assemble();
        reference_effective_displacement_form.Finalize();
        state.reference_effective_displacement.reset(
            reference_effective_displacement_form.ParallelAssemble());

        ParBilinearForm reference_effective_pressure_form(
            state.scalar_fes.get());
        reference_effective_pressure_form.AddDomainIntegrator(
            new MassIntegrator(effective_acoustic_mass));
        reference_effective_pressure_form.AddDomainIntegrator(
            new DiffusionIntegrator(acoustic_stiffness));
        reference_effective_pressure_form.AddBoundaryIntegrator(
            new BoundaryMassIntegrator(effective_inverse_impedance),
            absorbing_marker);
        reference_effective_pressure_form.Assemble();
        reference_effective_pressure_form.Finalize();
        state.reference_effective_pressure.reset(
            reference_effective_pressure_form.ParallelAssemble());
        state.reference_effective_matrix = block_matrix(
            state.reference_effective_displacement.get(), nullptr,
            nullptr, state.reference_effective_pressure.get());
        write_mumps_diagnostic(
            state.rank, "event=matrix_complete matrix=reference_effective");

        reference_matrices_are_finite =
            reference_Muu_form.SpMat().CheckFinite() == 0
            && reference_Kuu_form.SpMat().CheckFinite() == 0
            && reference_Mpp_form.SpMat().CheckFinite() == 0
            && reference_Kpp_form.SpMat().CheckFinite() == 0
            && reference_Cuu_form.SpMat().CheckFinite() == 0
            && reference_effective_displacement_form.SpMat().CheckFinite() == 0
            && reference_effective_pressure_form.SpMat().CheckFinite() == 0;
    }
    const bool matrices_are_finite = Muu_form.SpMat().CheckFinite() == 0
        && Kuu_form.SpMat().CheckFinite() == 0
        && Cuu_form.SpMat().CheckFinite() == 0
        && Mpp_form.SpMat().CheckFinite() == 0
        && Kpp_form.SpMat().CheckFinite() == 0
        && Kup_form.SpMat().CheckFinite() == 0
        && Mpu_form.SpMat().CheckFinite() == 0
        && effective_displacement_form.SpMat().CheckFinite() == 0
        && effective_pressure_form.SpMat().CheckFinite() == 0
        && cpp_is_finite
        && reference_matrices_are_finite;
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
    assembly_is_ready = true;
    write_mumps_diagnostic(
        state.rank, "event=stage_complete stage=assemble");
    return true;
}

bool App::Solver::solve()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank != 0) {
        return false;
    }
    int command = static_cast<int>(ParallelCommand::solve);
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return abort_on_distributed_exception(
        rank, "solve", [this]() { return solveParallelLocal(); });
}

bool App::Solver::solveParallelLocal()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const bool solver_ready = parallel_state && mesh
        && mesh_is_ready && assembly_is_ready;
    if (!all_succeeded(MPI_COMM_WORLD, solver_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Call setMesh() before the parallel solve.");
        }
        status.store(SolverStatus::Error);
        return false;
    }
    ParallelState& state = *parallel_state;
    write_mumps_diagnostic(state.rank, "event=stage_begin stage=solve");
    status.store(SolverStatus::Working);
    result.success = 0;
    frequency_response = {};
    forward_is_ready = false;
    performance_data.forwardFgmresIterations = 0;
    performance_data.forwardFgmresSolves = 0;
    performance_data.maximumForwardFgmresIterations = 0;
    performance_data.mumpsInitialFactorizationSeconds = 0.0;
    performance_data.mumpsEffectiveFactorizationSeconds = 0.0;
    performance_data.mumpsSolveSeconds = 0.0;
    performance_data.maximumForwardResidual = 0.0;

    bool designed_assembly_ready = state.M && state.C && state.K
        && state.Muu && state.Cuu && state.Kuu
        && state.Mpp && state.Cpp && state.Kpp && state.phi
        && state.reference_M && state.reference_C && state.reference_K
        && state.effective_matrix && state.effective_displacement
        && state.effective_pressure
        && state.reference_effective_matrix
        && state.reference_effective_displacement
        && state.reference_effective_pressure
        && state.reference_Muu && state.reference_Cuu
        && state.reference_Kuu && state.reference_Mpp
        && state.reference_Kpp
        && lset.design.Size() > 0;
    if (!all_succeeded(state.comm, designed_assembly_ready)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "Call assembleSolutionSpace() before solve().");
        }
        status.store(SolverStatus::Error);
        return false;
    }

    auto fail = [this](SolverStatus failure) {
        status.store(failure);
        return false;
    };

    const int first_analysis = state.reference_ready ? 1 : 0;
    for (int analysis = first_analysis; analysis < 2; ++analysis) {
        const bool reference_analysis = analysis == 0;
        if (reference_analysis) {
            state.reference_ready = false;
        }

        if (!reference_analysis && state.rank == 0) {
            streamToGlvis();
        }

        const HypreParMatrix& analysis_M = reference_analysis
            ? *state.reference_M : *state.M;
        const HypreParMatrix& analysis_C = reference_analysis
            ? *state.reference_C : *state.C;
        const HypreParMatrix& analysis_K = reference_analysis
            ? *state.reference_K : *state.K;
        const HypreParMatrix& analysis_Muu = reference_analysis
            ? *state.reference_Muu : *state.Muu;
        const HypreParMatrix& analysis_Mpp = reference_analysis
            ? *state.reference_Mpp : *state.Mpp;
        const HypreParMatrix& assembled_effective_matrix = reference_analysis
            ? *state.reference_effective_matrix : *state.effective_matrix;
        const HypreParMatrix& assembled_effective_displacement =
            reference_analysis
                ? *state.reference_effective_displacement
                : *state.effective_displacement;
        const HypreParMatrix& assembled_effective_pressure = reference_analysis
            ? *state.reference_effective_pressure
            : *state.effective_pressure;

        int communicator_relation = MPI_UNEQUAL;
        MPI_Comm_compare(
            state.comm, analysis_M.GetComm(), &communicator_relation);
        long long global_rows = analysis_M.GetGlobalNumRows();
        long long minimum_rows = global_rows;
        long long maximum_rows = global_rows;
        MPI_Allreduce(
            MPI_IN_PLACE, &minimum_rows, 1,
            MPI_LONG_LONG, MPI_MIN, state.comm);
        MPI_Allreduce(
            MPI_IN_PLACE, &maximum_rows, 1,
            MPI_LONG_LONG, MPI_MAX, state.comm);
        const int expected_local_size =
            state.displacement_fes->GetTrueVSize()
            + state.scalar_fes->GetTrueVSize();
        const bool distributed_contract_ready =
            (communicator_relation == MPI_IDENT
                || communicator_relation == MPI_CONGRUENT)
            && analysis_M.GetGlobalNumRows()
                == analysis_M.GetGlobalNumCols()
            && analysis_C.GetGlobalNumRows() == global_rows
            && analysis_C.GetGlobalNumCols() == global_rows
            && analysis_K.GetGlobalNumRows() == global_rows
            && analysis_K.GetGlobalNumCols() == global_rows
            && minimum_rows == maximum_rows
            && analysis_M.Height() == expected_local_size
            && state.system_inlet_load.Size() == expected_local_size
            && state.system_outlet_functional.Size() == expected_local_size
            && state.phi->Size() == state.scalar_fes->GetVSize()
            && state.active_design_dofs.Size() == state.phi->Size();
        if (!all_succeeded(state.comm, distributed_contract_ready)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The distributed matrix, vector, or level-set dimensions are inconsistent.");
            }
            return fail(SolverStatus::Error);
        }

        const auto* physics =
            std::get_if<VibroacousticSettings>(&settings.physics);
        if (physics == nullptr) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "Parallel solve currently supports vibroacoustics only.");
            }
            return fail(SolverStatus::Error);
        }

        std::unique_ptr<HypreParMatrix> K_hat(
            new HypreParMatrix(assembled_effective_matrix));
        K_hat->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);

        std::unique_ptr<HypreParMatrix> effective_displacement(
            new HypreParMatrix(assembled_effective_displacement));
        std::unique_ptr<HypreParMatrix> effective_pressure(
            new HypreParMatrix(assembled_effective_pressure));
        effective_displacement->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);

        std::unique_ptr<HypreParMatrix> M_system(
            new HypreParMatrix(analysis_M));
        M_system->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);
        std::unique_ptr<HypreParMatrix> initial_displacement(
            new HypreParMatrix(analysis_Muu));
        initial_displacement->EliminateBC(
            state.displacement_essential_tdofs, Operator::DIAG_ONE);
        std::unique_ptr<HypreParMatrix> initial_pressure(
            new HypreParMatrix(analysis_Mpp));

        int initial_relation = MPI_UNEQUAL;
        int effective_relation = MPI_UNEQUAL;
        MPI_Comm_compare(
            state.comm, M_system->GetComm(), &initial_relation);
        MPI_Comm_compare(
            state.comm, K_hat->GetComm(), &effective_relation);
        const bool solve_operators_ready =
            (initial_relation == MPI_IDENT
                || initial_relation == MPI_CONGRUENT)
            && (effective_relation == MPI_IDENT
                || effective_relation == MPI_CONGRUENT)
            && M_system->Height() == expected_local_size
            && K_hat->Height() == expected_local_size
            && M_system->GetGlobalNumRows() == global_rows
            && K_hat->GetGlobalNumRows() == global_rows;
        if (!all_succeeded(state.comm, solve_operators_ready)) {
            if (state.rank == 0) {
                log(LogLevel::Error,
                    "The distributed Newmark solve operators have incompatible dimensions.");
            }
            return fail(SolverStatus::Error);
        }

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
                    direct->SetPrintLevel(1);
                    direct->SetMatrixSymType(MUMPSSolver::UNSYMMETRIC);
                }
            }
            auto factor_started_at = std::chrono::steady_clock::now();
            const std::string analysis_name = reference_analysis
                ? "reference" : "designed";
            factor_mumps(
                *state.initial_mumps,
                *M_system,
                state.rank,
                state.ranks,
                analysis_name + "_initial");
            performance_data.mumpsInitialFactorizationSeconds +=
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - factor_started_at).count();
            factor_started_at = std::chrono::steady_clock::now();
            factor_mumps(
                *state.effective_mumps,
                *K_hat,
                state.rank,
                state.ranks,
                analysis_name + "_effective");
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
        write_mumps_diagnostic(
            state.rank,
            std::string("event=stage_begin stage=")
                + (reference_analysis
                    ? "reference_transient" : "designed_transient"));
        const auto transient_started_at = std::chrono::steady_clock::now();
        const LogFunction rank_log = state.rank == 0
            ? log
            : LogFunction([](LogLevel, std::string) {});
        const bool transient_ready = detail::runNewmark(
                settings,
                *physics,
                analysis_M,
                analysis_C,
                analysis_K,
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

        // The wrappers borrow the AMG blocks. Destroy the wrappers before a
        // reference solve releases those blocks.
        solve_initial = {};
        solve_effective = {};
        initial_fgmres.reset();
        effective_fgmres.reset();
        initial_preconditioner.reset();
        effective_preconditioner.reset();
        if (!all_succeeded(state.comm, transient_ready)) {
            state.initial_u_amg.reset();
            state.initial_p_amg.reset();
            state.effective_u_amg.reset();
            state.effective_p_amg.reset();
            return fail(SolverStatus::Diverged);
        }
        const double transient_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - transient_started_at).count();
        write_mumps_diagnostic(
            state.rank,
            std::string("event=stage_complete stage=")
                + (reference_analysis
                    ? "reference_transient" : "designed_transient"));
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

    const auto fourier_started_at = std::chrono::steady_clock::now();
    write_mumps_diagnostic(state.rank, "event=stage_begin stage=fft");
    const bool fourier_ready = postprocessFourierResponse();
    if (!all_succeeded(state.comm, fourier_ready)) {
        return fail(SolverStatus::Error);
    }
    performance_data.fourierSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - fourier_started_at).count();
    write_mumps_diagnostic(state.rank, "event=stage_complete stage=fft");
    result.success = 1;
    status.store(SolverStatus::Converged);
    forward_is_ready = true;
    if (state.rank == 0) {
        log(LogLevel::Message,
            "Completed the distributed Newmark solve and outlet FFT.");
    }
    write_mumps_diagnostic(state.rank, "event=stage_complete stage=solve");
    return true;
}

bool App::Solver::differentiateFrequencyResponses(
    const std::vector<std::complex<double>>& pass_spectrum_derivative,
    const std::vector<std::complex<double>>& stop_spectrum_derivative,
    Vector& pass_design_gradient,
    Vector& stop_design_gradient)
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank != 0) {
        return false;
    }
    int command = static_cast<int>(ParallelCommand::differentiate);
    MPI_Bcast(&command, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return abort_on_distributed_exception(
        rank, "differentiate", [&]() {
            return differentiateFrequencyResponsesParallelLocal(
                pass_spectrum_derivative,
                stop_spectrum_derivative,
                pass_design_gradient,
                stop_design_gradient);
        });
}

bool App::Solver::differentiateFrequencyResponsesParallelLocal(
    const std::vector<std::complex<double>>& pass_spectrum_derivative,
    const std::vector<std::complex<double>>& stop_spectrum_derivative,
    Vector& pass_design_gradient,
    Vector& stop_design_gradient)
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const bool adjoint_ready = parallel_state && forward_is_ready;
    if (!all_succeeded(MPI_COMM_WORLD, adjoint_ready)) {
        if (rank == 0) {
            log(LogLevel::Error,
                "Run the parallel forward solver before its adjoint.");
        }
        return false;
    }
    ParallelState& state = *parallel_state;
    write_mumps_diagnostic(
        state.rank, "event=stage_begin stage=differentiate");

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
    write_mumps_diagnostic(
        state.rank, "event=stage_complete stage=adjoint");

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

    Vector local_pass_gradient;
    Vector local_stop_gradient;
    int local_cut_elements = 0;
    int local_differentiated_dofs = 0;
    const auto differentiation_started_at = std::chrono::steady_clock::now();
    const bool cut_derivative_ready = detail::differentiateCutElements(
            settings,
            *state.mesh,
            *state.phi->FESpace(),
            *state.displacement_fes,
            *state.scalar_fes,
            *state.phi,
            state.active_design_dofs,
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
    write_mumps_diagnostic(
        state.rank, "event=stage_complete stage=cut_differentiation");

    ParGridFunction parallel_pass_gradient(state.scalar_fes.get());
    ParGridFunction parallel_stop_gradient(state.scalar_fes.get());
    parallel_pass_gradient = local_pass_gradient;
    parallel_stop_gradient = local_stop_gradient;
    std::unique_ptr<HypreParVector> pass_true_gradient(
        parallel_pass_gradient.ParallelAssemble());
    std::unique_ptr<HypreParVector> stop_true_gradient(
        parallel_stop_gradient.ParallelAssemble());
    Vector pass_physical_gradient;
    Vector stop_physical_gradient;
    auto gather_gradient = [&](const HypreParVector& local_gradient,
                               Vector& serial_gradient) {
        const int local_size = local_gradient.Size();
        const bool local_map_is_valid =
            state.true_to_serial_design_dofs.Size() == local_size;
        if (!all_succeeded(state.comm, local_map_is_valid)) {
            return false;
        }

        std::vector<int> counts(state.ranks);
        MPI_Gather(&local_size, 1, MPI_INT,
                   counts.data(), 1, MPI_INT, 0, state.comm);
        std::vector<int> displacements(state.ranks);
        int gathered_size = 0;
        if (state.rank == 0) {
            for (int rank = 0; rank < state.ranks; ++rank) {
                displacements[rank] = gathered_size;
                gathered_size += counts[rank];
            }
        }
        std::vector<double> gathered_values(
            state.rank == 0 ? gathered_size : 0);
        std::vector<int> gathered_dofs(
            state.rank == 0 ? gathered_size : 0);
        MPI_Gatherv(
            const_cast<real_t*>(local_gradient.HostRead()), local_size,
            MPI_DOUBLE, gathered_values.data(), counts.data(),
            displacements.data(), MPI_DOUBLE, 0, state.comm);
        MPI_Gatherv(
            state.true_to_serial_design_dofs.GetData(), local_size,
            MPI_INT, gathered_dofs.data(), counts.data(),
            displacements.data(), MPI_INT, 0, state.comm);

        bool gathered = true;
        if (state.rank == 0) {
            serial_gradient.SetSize(level_set_fes->GetTrueVSize());
            serial_gradient = 0.0;
            Array<int> visits(serial_gradient.Size());
            visits = 0;
            for (int i = 0; i < gathered_size; ++i) {
                const int dof = gathered_dofs[i];
                if (dof < 0 || dof >= serial_gradient.Size()) {
                    gathered = false;
                    continue;
                }
                serial_gradient[dof] = gathered_values[i];
                ++visits[dof];
            }
            for (int dof = 0; dof < visits.Size(); ++dof) {
                gathered = gathered && visits[dof] == 1;
            }
        }
        return all_succeeded(state.comm, gathered);
    };
    if (!gather_gradient(*pass_true_gradient, pass_physical_gradient)
        || !gather_gradient(*stop_true_gradient, stop_physical_gradient)) {
        if (state.rank == 0) {
            log(LogLevel::Error,
                "Could not gather the distributed design gradients.");
        }
        return false;
    }
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
    write_mumps_diagnostic(
        state.rank,
        filter_success
            ? "event=stage_complete stage=design_gradient_redistribution"
            : "event=stage_error stage=design_gradient_redistribution");
    if (filter_success && state.rank == 0) {
        log(LogLevel::Message,
            "Completed the distributed pass/stop adjoints and reduced the design gradients.");
    }
    if (filter_success) {
        write_mumps_diagnostic(
            state.rank, "event=stage_complete stage=differentiate");
    }
    return filter_success != 0;
}
#endif
