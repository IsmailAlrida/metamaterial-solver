#include "optimizer.hpp"

namespace App {

Optimizer::Optimizer(const OptimizerSettings& settings,
                     LevelSet& geometry,
                     const SolverResult& result)
    : settings(settings),
      geometry(geometry),
      result(result)
{
}

void Optimizer::optimize()
{
    // TODO: Implement the paper's MMA optimization loop.
}

} // namespace App
