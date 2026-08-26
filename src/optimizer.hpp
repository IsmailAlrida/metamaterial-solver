#pragma once

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

class Optimizer {
    public:
        Optimizer(const OptimizerSettings& settings,
                  LevelSet& geometry,
                  const SolverResult& result,
                  const LogFunction& log);

        void optimize();

    private:
        const OptimizerSettings& settings;
        LevelSet& geometry;
        const SolverResult& result;
        const LogFunction& log;
};

} // namespace App
