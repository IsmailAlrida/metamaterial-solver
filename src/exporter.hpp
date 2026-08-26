#pragma once

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

class Exporter {
    
    public:
        Exporter(const ExporterSettings& settings,
                 const LevelSet& geometry,
                 const LogFunction& log);
        bool exportMesh();

    private:
        const ExporterSettings& settings;
        const LevelSet& geometry;
        const LogFunction& log;

};

} // namespace App
