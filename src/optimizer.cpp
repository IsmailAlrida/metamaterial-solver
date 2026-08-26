#include "optimizer.hpp"

namespace App {

Optimizer::Optimizer(const OptimizerSettings& settings,
                     LevelSet& geometry,
                     const SolverResult& result,
                     const LogFunction& log)
    : settings(settings),
      geometry(geometry),
      result(result),
      log(log)
{
}

void Optimizer::optimize()
{
    // TODO: Implement the paper's MMA optimization loop.
    log(LogLevel::Warning, "The optimizer is not implemented yet.");
}

} // namespace App
