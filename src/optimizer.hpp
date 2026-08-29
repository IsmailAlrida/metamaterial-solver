#pragma once

#include <atomic>
#include "global_types.hpp"
#include "logging.hpp"
#include "solver.hpp"

namespace App {

class Optimizer {
    public:
        Optimizer(const OptimizerSettings& settings,
                  Solver& solver,
                  LevelSet& geometry,
                  const SolverResult& result,
                  const LogFunction& log);

        void run();
        void optimize();
        void request_cancel();
        OptimizerStatus get_status() const;
        SolverStatus get_solver_status() const;
        int get_iteration() const;
        bool is_exportable() const;

    private:
        const OptimizerSettings& settings;
        Solver& solver;
        LevelSet& geometry;
        const SolverResult& result;
        const LogFunction& log;
        std::atomic_bool cancel_requested{false};
        std::atomic<OptimizerStatus> status{OptimizerStatus::Idle};
        std::atomic<int> iteration{0};
};

} // namespace App
