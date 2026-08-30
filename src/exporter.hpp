#pragma once

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

class Exporter {
    
    public:
        Exporter(const ExporterSettings& settings,
                 const LevelSet& geometry,
                 const LogFunction& log);

        // This stays as the single public mesh-export action. The implementation
        // can choose the 2D, extruded-2D, or direct-3D path from the level-set
        // finite-element space instead of exposing three almost identical APIs.
        bool exportMesh();

    private:
        // The LevelSet remains the authoritative geometry. Any explicit vertices,
        // triangles, Assimp scenes, and manifest data should be temporary exporter
        // products rather than new app-wide state.
        const ExporterSettings& settings;
        const LevelSet& geometry;
        const LogFunction& log;

        // TODO: Optionally add a separate saveRunData() action when the exact run
        // data format is decided. It will need selected SolverResult/optimizer data;
        // do not make the mesh-export path own or duplicate the simulation history.

};

} // namespace App
