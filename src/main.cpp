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
        auto lset = std::make_unique<App::LevelSet>(
            dimension,
            solverSettings.nx,
            solverSettings.ny,
            nz);
        auto& geometry = *lset;

        // Keep the renderer alive longer than the computers that will publish to it.
        auto renderer = std::make_unique<App::Renderer>(appSettings);
        App::LogFunction log = [&renderer](App::LogLevel level, std::string message) {
            renderer->log(level, std::move(message));
        };

        // in opt, result is const, not edited.
        auto optimizer = std::make_unique<App::Optimizer>(
            optimizerSettings,
            geometry,
            solverResult,
            log);
        auto solver = std::make_unique<App::Solver>(
            solverSettings,
            geometry,
            solverResult,
            log);
        auto exporter = std::make_unique<App::Exporter>(
            exporterSettings,
            geometry,
            log);
        auto executor = std::make_unique<App::Executor>(log);

        renderer->setup();
        log(App::LogLevel::Message, "Renderer initialized");

        // For now, the visualization panel stays in the normal ImGui frame.
        // TODO: Bind GLVis through the existing network stream before embedding it natively.
        while (!renderer->shouldClose) {
            renderer->displayFrame();
        }

        // TODO: Executor should own and join its backend thread in its destructor.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Application startup failed: " << error.what() << '\n';
        return 1;
    }
}
