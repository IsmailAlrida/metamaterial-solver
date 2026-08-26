#include "exporter.hpp"

namespace App {

Exporter::Exporter(const ExporterSettings& settings,
                   const LevelSet& geometry,
                   const LogFunction& log)
    : settings(settings),
      geometry(geometry),
      log(log)
{
}

bool Exporter::exportMesh()
{
    // TODO: Export the extruded solid mesh and its JSON manifest.
    log(LogLevel::Warning, "Mesh export is not implemented yet.");
    return false;
}

} // namespace App
