#include "exporter.hpp"

namespace App {

Exporter::Exporter(const ExporterSettings& settings,
                   const LevelSet& geometry)
    : settings(settings),
      geometry(geometry)
{
}

bool Exporter::exportMesh()
{
    // TODO: Export the extruded solid mesh and its JSON manifest.
    return false;
}

} // namespace App
