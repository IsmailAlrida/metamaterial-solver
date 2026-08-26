#pragma once

#include "global_types.hpp"

namespace App {

class Exporter {
    
    public:
        Exporter(const ExporterSettings& settings,
                 const LevelSet& geometry);
        bool exportMesh();

    private:
        const ExporterSettings& settings;
        const LevelSet& geometry;

};

} // namespace App
