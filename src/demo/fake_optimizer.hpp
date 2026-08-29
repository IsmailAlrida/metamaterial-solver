#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "fake_solver.hpp"
#include "global_types.hpp"
#include "logging.hpp"

namespace App::Demo {

class FakeOptimizer {
    public:
        FakeOptimizer(const OptimizerSettings& settings,
                      FakeSolver& solver,
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

        void run()
        {
            cancelRequested.store(false);
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

                const int maximumIterations = std::max(settings.maxIterations, 1);

                while (iteration.load() < maximumIterations) {
                    if (cancelRequested.load()) {
                        status.store(OptimizerStatus::Cancelled);
                        return;
                    }

                    optimize();

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

        void optimize()
        {
            const int count = geometry.design.Size();
            const double phase = static_cast<double>(iteration.load() + 1) * 0.38;

            for (int i = 0; i < count; ++i) {
                const double position = static_cast<double>(i) / std::max(1, count - 1);
                geometry.design[i] = std::clamp(
                    0.5 + 0.34 * std::sin(position * 18.0 + phase), 0.0, 1.0);
            }
            geometry.phi->SetFromTrueDofs(geometry.design);
            log(LogLevel::Message,
                "Demo optimizer updated the shared level-set design.");
        }

        void request_cancel()
        {
            cancelRequested.store(true);
        }

        OptimizerStatus get_status() const
        {
            return status.load();
        }

        SolverStatus get_solver_status() const
        {
            return solver.get_status();
        }

        int get_iteration() const
        {
            return iteration.load();
        }

        bool is_exportable() const
        {
            const OptimizerStatus currentStatus = status.load();
            return currentStatus == OptimizerStatus::Converged
                || currentStatus == OptimizerStatus::MaximumIterations
                || (currentStatus == OptimizerStatus::Cancelled
                    && solver.get_status() == SolverStatus::Converged);
        }

    private:
        const OptimizerSettings& settings;
        FakeSolver& solver;
        LevelSet& geometry;
        const SolverResult& result;
        const LogFunction& log;
        std::atomic_bool cancelRequested{false};
        std::atomic<OptimizerStatus> status{OptimizerStatus::Idle};
        std::atomic<int> iteration{0};
};

} // namespace App::Demo
