#pragma once

#include "global_types.hpp"

namespace App {

class Optimizer {
    public:
        Optimizer(const OptimizerSettings& settings,
                  LevelSet& geometry,
                  const SolverResult& result);

        void optimize();

    private:
        const OptimizerSettings& settings;
        LevelSet& geometry;
        const SolverResult& result;
};

} // namespace App
