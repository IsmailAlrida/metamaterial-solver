#pragma once

#include <algorithm>
#include <cmath>

#include "global_types.hpp"
#include "logging.hpp"

namespace App::Demo {

class FakeOptimizer {
    public:
        FakeOptimizer(const OptimizerSettings& settings,
                      LevelSet& geometry,
                      const SolverResult& result,
                      const LogFunction& log)
            : settings(settings),
              geometry(geometry),
              result(result),
              log(log)
        {
        }

        void optimize()
        {
            ++iteration;
            const int count = geometry.design.Size();
            const double phase = static_cast<double>(iteration) * 0.38;

            for (int i = 0; i < count; ++i) {
                const double position = static_cast<double>(i) / std::max(1, count - 1);
                geometry.design[i] = std::clamp(
                    0.5 + 0.34 * std::sin(position * 18.0 + phase), 0.0, 1.0);
            }
            geometry.phi->SetFromTrueDofs(geometry.design);
            log(LogLevel::Message,
                "Demo optimizer updated the shared level-set design.");
        }

    private:
        const OptimizerSettings& settings;
        LevelSet& geometry;
        const SolverResult& result;
        const LogFunction& log;
        int iteration = 0;
};

} // namespace App::Demo
