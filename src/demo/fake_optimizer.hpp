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
            passObjective.store(0.0);
            stopObjective.store(0.0);
            mmaBound.store(0.0);

            try {
                if (!solver.setMesh(METAMATERIAL_USE_MPI != 0)
                    || !solver.assembleSolutionSpace(METAMATERIAL_USE_MPI != 0)
                    || !solver.solve(METAMATERIAL_USE_MPI != 0)) {
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

                    if (!solver.setMesh(METAMATERIAL_USE_MPI != 0)
                        || !solver.assembleSolutionSpace(METAMATERIAL_USE_MPI != 0)
                        || !solver.solve(METAMATERIAL_USE_MPI != 0)) {
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
            const double progress = static_cast<double>(iteration.load() + 1);
            passObjective.store(10.0 / (1.0 + progress));
            stopObjective.store(14.0 / (1.0 + progress));
            mmaBound.store(std::max(passObjective.load(), stopObjective.load()));
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

        double get_pass_objective() const
        {
            return passObjective.load();
        }

        double get_stop_objective() const
        {
            return stopObjective.load();
        }

        double get_mma_bound() const
        {
            return mmaBound.load();
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
        std::atomic<double> passObjective{0.0};
        std::atomic<double> stopObjective{0.0};
        std::atomic<double> mmaBound{0.0};
};

} // namespace App::Demo
