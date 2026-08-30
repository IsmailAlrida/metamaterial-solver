#pragma once

#include <filesystem>

#include "exporter.hpp"
#include "global_types.hpp"
#include "logging.hpp"

namespace App::Demo {

class FakeExporter {
    public:
        FakeExporter(const AppSettings& settings,
                     const SolverResult& result,
                     const LevelSet& geometry,
                     const LogFunction& log)
            : exporter(settings, result, geometry, log),
              log(log)
        {
        }

        bool exportRunData(const std::filesystem::path& directory,
                           OptimizerStatus status,
                           int iteration,
                           double pass,
                           double stop,
                           double mmaBound)
        {
            return exporter.exportRunData(
                directory, status, iteration, pass, stop, mmaBound);
        }

        bool exportMesh()
        {
            log(LogLevel::Message,
                "Demo export complete: synthetic solid mesh and JSON manifest prepared.");
            return true;
        }

    private:
        Exporter exporter;
        const LogFunction& log;
};

} // namespace App::Demo
