#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "executor.hpp"
#include "exporter.hpp"
#include "global_types.hpp"
#include "logging.hpp"
#include "optimizer.hpp"
#include "renderer.hpp"
#include "solver.hpp"

int main(int, char**)
{
    try {
        const std::string glvisHost = "localhost";
        const int glvisPort = 19916;

        // Construct the top-level data. Everyone below receives references to these.
        // TODO: Make App settings construct defaults
        auto settings = std::make_unique<App::AppSettings>();
        auto& appSettings = *settings;
        auto& solverSettings = appSettings.solverSettings;
        auto& optimizerSettings = appSettings.optSettings;
        auto& exporterSettings = appSettings.exporterSettings;

        solverSettings.glvisHost = glvisHost;
        solverSettings.glvisPort = glvisPort;

        auto result = std::make_unique<App::SolverResult>();
        auto& solverResult = *result;

        // TODO: This needs an initial guess constructor to construct the first lset guess
        // Probably an equally spaced square grid of cylinders/circles with some radius each; basically a sonic crystal whose shape we can weakly try to guess from the bandgap. Or just hardocde a single crystal structure. how about that?
        const int dimension = solverSettings.nz > 0 ? 3 : 2;
        const int nz = solverSettings.nz > 0 ? solverSettings.nz : 1;
        const double sx = solverSettings.inletLength
            + solverSettings.designLength
            + solverSettings.outletLength;

        // LevelSet's GridFunction borrows this finite-element space, so the mesh,
        // collection, and space all remain top-level and outlive the LevelSet.
        auto levelSetMesh = solverSettings.nz > 0
            ? std::make_unique<mfem::Mesh>(mfem::Mesh::MakeCartesian3D(
                solverSettings.nx,
                solverSettings.ny,
                nz,
                mfem::Element::HEXAHEDRON,
                sx,
                solverSettings.sy,
                solverSettings.sz))
            : std::make_unique<mfem::Mesh>(mfem::Mesh::MakeCartesian2D(
                solverSettings.nx,
                solverSettings.ny,
                mfem::Element::QUADRILATERAL,
                true,
                sx,
                solverSettings.sy));
        auto levelSetFec = std::make_unique<mfem::H1_FECollection>(1, dimension);
        auto levelSetFes = std::make_unique<mfem::FiniteElementSpace>(
            levelSetMesh.get(), levelSetFec.get());
        auto lset = std::make_unique<App::LevelSet>(*levelSetFes);
        auto& geometry = *lset;
        geometry.design = 0.5;
        geometry.phi.SetFromTrueDofs(geometry.design);

        // Keep the renderer alive longer than the objects that will publish to it.
        auto renderer = std::make_unique<App::Renderer>(
            appSettings,
            solverResult,
            geometry);
        App::LogFunction log = [&renderer](App::LogLevel level, std::string message) {
            renderer->log(level, std::move(message));
        };

        // in opt, result is const, not edited.
        auto optimizer = std::make_unique<App::optimizer_t>(
            optimizerSettings,
            geometry,
            solverResult,
            log);
        auto solver = std::make_unique<App::solver_t>(
            solverSettings,
            geometry,
            solverResult,
            log);
        auto exporter = std::make_unique<App::exporter_t>(
            exporterSettings,
            geometry,
            log);
        auto executor = std::make_unique<App::executor_t>(log);

        renderer->setup();
        log(App::LogLevel::Message, "Renderer initialized");

        // For now, the visualization panel stays in the normal ImGui frame.
        // TODO: Bind GLVis through the existing network stream before embedding it natively.
        while (!renderer->shouldClose) {
            renderer->displayFrame(*solver, *optimizer, *exporter, *executor);
        }

        // TODO: Executor should own and join its backend thread in its destructor.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Application startup failed: " << error.what() << '\n';
        return 1;
    }
}
