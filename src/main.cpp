#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "mpi.h"

#include "exporter.hpp"
#include "global_types.hpp"
#include "logging.hpp"
#include "optimizer.hpp"
#include "renderer.hpp"
#include "solver.hpp"
#include "dispatcher.hpp"

// OK so what is left?
/*
    So the dispatcher is done, we have a nice asynchronous dispatcher.

    We need to put the white noise and FFT calculator in the solver

    Then we need to implement the MMA optimizer algorithm

    Then an exporter algorihtm to make the mesh (marching cubes?)

    then a publication path from the FFT of the solver and the renderer

    THen polish the renderer

    Then test

    Then go to sleep

    oh yeah periodic boundary conditions for unit cell design

    also ensure we implement the boundary conditions of the paper for a prelim duct

    ALso the FFT plot should be in dB in the y axis, so we can see the attenuation


*/
int main(int argc, char** argv)
{
    int provided_thread_level = 0;
    if (MPI_Init_thread(
            &argc, &argv, MPI_THREAD_SERIALIZED, &provided_thread_level)
            != MPI_SUCCESS) {
        std::cerr << "Could not initialize MPI for ParOpt.\n";
        return 1;
    }
    if (provided_thread_level < MPI_THREAD_SERIALIZED) {
        std::cerr << "The MPI runtime does not support the optimizer worker thread.\n";
        MPI_Finalize();
        return 1;
    }
    int rank = 0;
#if METAMATERIAL_USE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

    int exit_code = 0;
    try {
        mfem::Device device(METAMATERIAL_USE_CUDA ? "cuda" : "cpu");
        if (rank == 0) {
            device.Print();
        }

        // Construct the top-level data. Everyone below receives references to these.
        // TODO: Make App settings construct defaults
        auto settings = std::make_unique<App::AppSettings>();
        auto& appSettings = *settings;
        auto& solverSettings = appSettings.solverSettings;
        auto& optimizerSettings = appSettings.optSettings;
        auto& exporterSettings = appSettings.exporterSettings;

        auto result = std::make_unique<App::SolverResult>();
        auto& solverResult = *result;

        // The solver attaches the FE space and initializes the paper's cosine design.
        auto lset = std::make_unique<App::LevelSet>();
        auto& geometry = *lset;

        // Keep the renderer alive longer than the objects that will publish to it.
        std::unique_ptr<App::Renderer> renderer;
        App::LogFunction log = [&renderer, rank](
            App::LogLevel level, std::string message) {
            if (rank != 0) {
                return;
            }
            if (renderer) {
                renderer->log(level, std::move(message));
            }
            else {
                std::cerr << message << '\n';
            }
        };

        // Every MPI process owns one local handle to the same collective solver.
        auto solver = std::make_unique<App::solver_t>(
            solverSettings,
            optimizerSettings,
            geometry,
            solverResult,
            log,
            METAMATERIAL_USE_MPI != 0);

#if METAMATERIAL_USE_MPI
        if (rank != 0) {
            solver->parallelWorkerLoop();
        }
        else
#endif
        {
            renderer = std::make_unique<App::Renderer>(
                appSettings,
                solverResult,
                geometry);
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
        }

    } catch (const std::exception& error) {
        std::cerr << "Application startup failed on MPI rank " << rank
                  << ": " << error.what() << '\n';
        exit_code = 1;
    }
    MPI_Finalize();
    return exit_code;
}
