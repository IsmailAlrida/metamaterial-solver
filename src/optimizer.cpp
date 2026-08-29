#include "optimizer.hpp"

namespace App {

Optimizer::Optimizer(const OptimizerSettings& settings,
                     Solver& solver,
                     LevelSet& geometry,
                     const SolverResult& result,
                     const LogFunction& log)
    : settings(settings),
      solver(solver),
      geometry(geometry),
      result(result),
      log(log)
{
}

void Optimizer::run()
{
    cancel_requested.store(false);
    iteration.store(0);
    pass_objective.store(0.0);
    stop_objective.store(0.0);
    mma_bound.store(0.0);
    status.store(OptimizerStatus::Working);

    try {
        if (!solver.setMesh()
            || !solver.assembleSolutionSpace()
            || !solver.solve()) {
            status.store(
                solver.get_status() == SolverStatus::Diverged
                    ? OptimizerStatus::Diverged
                    : OptimizerStatus::Error);
            return;
        }
        status.store(cancel_requested.load()
            ? OptimizerStatus::Cancelled
            : OptimizerStatus::Converged);
    }
    catch (...) {
        status.store(OptimizerStatus::Error);
        throw;
    }
}

bool Optimizer::optimize()
{
    // TODO: Replace this forward-only seam with the ParOpt optimization update.
    log(LogLevel::Warning, "Optimization is not implemented in the real backend yet.");
    return false;
}

void Optimizer::request_cancel()
{
    cancel_requested.store(true);
}

OptimizerStatus Optimizer::get_status() const
{
    return status.load();
}

SolverStatus Optimizer::get_solver_status() const
{
    return solver.get_status();
}

int Optimizer::get_iteration() const
{
    return iteration.load();
}

bool Optimizer::is_exportable() const
{
    const OptimizerStatus current_status = status.load();
    return current_status == OptimizerStatus::Converged
        || current_status == OptimizerStatus::MaximumIterations
        || (current_status == OptimizerStatus::Cancelled
            && solver.get_status() == SolverStatus::Converged);
}

double Optimizer::get_pass_objective() const
{
    return pass_objective.load();
}

double Optimizer::get_stop_objective() const
{
    return stop_objective.load();
}

double Optimizer::get_mma_bound() const
{
    return mma_bound.load();
}

} // namespace App
