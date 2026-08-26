#pragma once

#include "global_types.hpp"
#include "logging.hpp"

namespace App::Demo {

class FakeExporter {
    public:
        FakeExporter(const ExporterSettings& settings,
                     const LevelSet& geometry,
                     const LogFunction& log)
            : settings(settings),
              geometry(geometry),
              log(log)
        {
        }

        bool exportMesh()
        {
            log(LogLevel::Message,
                "Demo export complete: synthetic solid mesh and JSON manifest prepared.");
            return true;
        }

    private:
        const ExporterSettings& settings;
        const LevelSet& geometry;
        const LogFunction& log;
};

} // namespace App::Demo
