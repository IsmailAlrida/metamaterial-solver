#pragma once

#include <filesystem>

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

class Exporter {
    
    public:
        Exporter(const AppSettings& settings,
                 const SolverResult& result,
                 const LevelSet& geometry,
                 const LogFunction& log);

        bool exportRunData(const std::filesystem::path& directory,
                           OptimizerStatus status,
                           int iteration,
                           double pass,
                           double stop,
                           double mmaBound);

        // This stays as the single public mesh-export action. The implementation
        // can choose the 2D, extruded-2D, or direct-3D path from the level-set
        // finite-element space instead of exposing three almost identical APIs.
        bool exportMesh();

    private:
        // The LevelSet remains the authoritative geometry. Any explicit vertices,
        // triangles, Assimp scenes, and manifest data should be temporary exporter
        // products rather than new app-wide state.
        const AppSettings& settings;
        const SolverResult& result;
        const LevelSet& geometry;
        const LogFunction& log;

};

} // namespace App
