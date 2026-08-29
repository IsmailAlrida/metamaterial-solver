#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "exporter.hpp"
#include "global_types.hpp"
#include "logging.hpp"
#include "optimizer.hpp"
#include "renderer.hpp"
#include "solver.hpp"
#include "dispatcher.hpp"

// OK so what is left?
int main(int, char**)
{
    try {
        // Construct the top-level data. Everyone below receives references to these.
        // TODO: Make App settings construct defaults
        auto settings = std::make_unique<App::AppSettings>();
        auto& appSettings = *settings;
        auto& solverSettings = appSettings.solverSettings;
        auto& optimizerSettings = appSettings.optSettings;
        auto& exporterSettings = appSettings.exporterSettings;

        auto result = std::make_unique<App::SolverResult>();
        auto& solverResult = *result;

        // TODO: This needs an initial guess constructor to construct the first lset guess
        // Probably an equally spaced square grid of cylinders/circles with some radius each; basically a sonic crystal whose shape we can weakly try to guess from the bandgap. Or just hardocde a single crystal structure. how about that?
        auto lset = std::make_unique<App::LevelSet>();
        auto& geometry = *lset;
        // TODO: Make the initial design guess the same shape as the paper has

        // Keep the renderer alive longer than the objects that will publish to it.
        auto renderer = std::make_unique<App::Renderer>(
            appSettings,
            solverResult,
            geometry);
        App::LogFunction log = [&renderer](App::LogLevel level, std::string message) {
            renderer->log(level, std::move(message));
        };

        auto solver = std::make_unique<App::solver_t>(
            solverSettings,
            optimizerSettings,
            geometry,
            solverResult,
            log);
        // in opt, result is const, not edited.
        auto optimizer = std::make_unique<App::optimizer_t>(
            optimizerSettings,
            *solver,
            geometry,
            solverResult,
            log);
        auto exporter = std::make_unique<App::exporter_t>(
            exporterSettings,
            geometry,
            log);
        auto dispatcher = std::make_unique<App::dispatcher_t>(
            *optimizer,
            *exporter,
            log);
        renderer->setup();
        log(App::LogLevel::Message, "Renderer initialized");

        while (!renderer->shouldClose) {
            dispatcher->dispatch();
            renderer->displayFrame(*dispatcher);
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Application startup failed: " << error.what() << '\n';
        return 1;
    }
}
