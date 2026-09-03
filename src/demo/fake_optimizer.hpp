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
                      const LogFunction& log)
            : settings(settings),
              solver(solver),
              geometry(geometry),
              log(log)
        {
        }

        void prepare_run()
        {
            clear_requests();
            runPrepared.store(true);
            status.store(OptimizerStatus::Working);
        }

        void run()
        {
            if (!runPrepared.exchange(false)) {
                clear_requests();
            }
            iteration.store(0);
            status.store(OptimizerStatus::Working);
            passObjective.store(0.0);
            stopObjective.store(0.0);
            epigraphBound.store(0.0);

            try {
                if (cancelRequested.load()) {
                    status.store(OptimizerStatus::Cancelled);
                    clear_requests();
                    return;
                }
                if (!solver.setMesh()
                    || !solver.assembleSolutionSpace()
                    || !solver.solve()) {
                    status.store(
                        solver.get_status() == SolverStatus::Diverged
                            ? OptimizerStatus::Diverged
                            : OptimizerStatus::Error);
                    clear_requests();
                    return;
                }
                passObjective.store(10.0);
                stopObjective.store(14.0);
                epigraphBound.store(14.0);

                const int maximumIterations = std::max(settings.maxIterations, 1);

                while (iteration.load() < maximumIterations) {
                    if (cancelRequested.load()) {
                        status.store(OptimizerStatus::Cancelled);
                        clear_requests();
                        return;
                    }

                    optimize();

                    if (!solver.assembleSolutionSpace()
                        || !solver.solve()) {
                        status.store(
                            solver.get_status() == SolverStatus::Diverged
                                ? OptimizerStatus::Diverged
                                : OptimizerStatus::Error);
                        clear_requests();
                        return;
                    }

                    iteration.fetch_add(1);
                }

                status.store(OptimizerStatus::MaximumIterations);
                clear_requests();
            }
            catch (...) {
                status.store(OptimizerStatus::Error);
                clear_requests();
                throw;
            }
        }

        void optimize()
        {
            const int count = geometry.activeDesignDofs.Size();
            const double phase = static_cast<double>(iteration.load() + 1) * 0.38;

            for (int i = 0; i < count; ++i) {
                const double position = static_cast<double>(i) / std::max(1, count - 1);
                geometry.design[geometry.activeDesignDofs[i]] = std::clamp(
                    0.5 + 0.34 * std::sin(position * 18.0 + phase), 0.0, 1.0);
            }
            geometry.enforceDesignConstraints();
            const double progress = static_cast<double>(iteration.load() + 1);
            passObjective.store(10.0 / (1.0 + progress));
            stopObjective.store(14.0 / (1.0 + progress));
            epigraphBound.store(std::max(passObjective.load(), stopObjective.load()));
            log(LogLevel::Message,
                "Demo optimizer updated the shared level-set design.");
        }

        void request_cancel()
        {
            const OptimizerStatus currentStatus = status.load();
            if (currentStatus != OptimizerStatus::Working) {
                return;
            }
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

        double get_epigraph_bound() const
        {
            return epigraphBound.load();
        }

    private:
        void clear_requests()
        {
            cancelRequested.store(false);
        }

        const OptimizerSettings& settings;
        FakeSolver& solver;
        LevelSet& geometry;
        const LogFunction& log;
        std::atomic_bool cancelRequested{false};
        std::atomic_bool runPrepared{false};
        std::atomic<OptimizerStatus> status{OptimizerStatus::Idle};
        std::atomic<int> iteration{0};
        std::atomic<double> passObjective{0.0};
        std::atomic<double> stopObjective{0.0};
        std::atomic<double> epigraphBound{0.0};
};

} // namespace App::Demo
