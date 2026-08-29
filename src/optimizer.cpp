#include "optimizer.hpp"

#include <algorithm>

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
    status.store(OptimizerStatus::Working);

    try {
        if (!solver.run()) {
            status.store(
                solver.get_status() == SolverStatus::Diverged
                    ? OptimizerStatus::Diverged
                    : OptimizerStatus::Error);
            return;
        }

        const int maximum_iterations = std::max(settings.maxIterations, 1);

        while (iteration.load() < maximum_iterations) {
            if (cancel_requested.load()) {
                status.store(OptimizerStatus::Cancelled);
                return;
            }

            // TODO: Stop early when the paper's objective convergence criterion is implemented.
            optimize();

            // Finish evaluating the updated design before observing Cancel so
            // the retained level set and solver result describe the same iteration.
            if (!solver.run()) {
                status.store(
                    solver.get_status() == SolverStatus::Diverged
                        ? OptimizerStatus::Diverged
                        : OptimizerStatus::Error);
                return;
            }

            iteration.fetch_add(1);
        }

        status.store(OptimizerStatus::MaximumIterations);
    }
    catch (...) {
        status.store(OptimizerStatus::Error);
        throw;
    }
}

void Optimizer::optimize()
{
    // TODO: Implement the paper's MMA optimization loop.
    log(LogLevel::Warning, "The optimizer is not implemented yet.");
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

} // namespace App
