#pragma once

#include <atomic>
#include <complex>
#include <condition_variable>
#include <mutex>
#include <vector>
#include "global_types.hpp"
#include "logging.hpp"
#include "solver.hpp"

namespace App {

struct OptimizerPerformance {
    double paroptSeconds = 0.0;
    double forwardCallbackSeconds = 0.0;
    double gradientCallbackSeconds = 0.0;
};

class Optimizer {
    public:
        Optimizer(const OptimizerSettings& settings,
                  Solver& solver,
                  LevelSet& geometry,
                  const SolverResult& result,
                  const LogFunction& log);

        void prepare_run();
        void run();
        bool optimize();
        void request_pause();
        void resume();
        bool is_pause_requested() const;
        void request_cancel();
        OptimizerStatus get_status() const;
        SolverStatus get_solver_status() const;
        int get_iteration() const;
        bool is_exportable() const;
        double get_pass_objective() const;
        double get_stop_objective() const;
        double get_mma_bound() const;
        const OptimizerPerformance& performance() const;

    private:
        class Problem;

        struct ObjectiveEvaluation {
            double pass = 0.0;
            double stop = 0.0;
            bool has_pass = false;
            bool has_stop = false;

            // Each complex entry stores dPhi/dRe + i*dPhi/dIm for the
            // corresponding unnormalized outlet FFT coefficient.
            std::vector<std::complex<double>> pass_spectrum_derivative;
            std::vector<std::complex<double>> stop_spectrum_derivative;
        };

        bool evaluateObjectives(ObjectiveEvaluation& objective) const;
        void pause_at_boundary();
        void clear_requests();

        const OptimizerSettings& settings;
        Solver& solver;
        LevelSet& geometry;
        const SolverResult& result;
        const LogFunction& log;
        std::atomic_bool cancel_requested{false};
        mutable std::mutex pause_mutex;
        std::condition_variable pause_condition;
        bool pause_requested = false;
        std::atomic_bool run_prepared{false};
        std::atomic<OptimizerStatus> status{OptimizerStatus::Idle};
        std::atomic<int> iteration{0};
        std::atomic<double> pass_objective{0.0};
        std::atomic<double> stop_objective{0.0};
        std::atomic<double> mma_bound{0.0};
        OptimizerPerformance performance_data;
};

} // namespace App
