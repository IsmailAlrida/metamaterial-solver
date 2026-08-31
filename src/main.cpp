#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
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

namespace {

volatile std::sig_atomic_t interrupt_requested = 0;

void request_shutdown(int)
{
    interrupt_requested = 1;
}

} // namespace

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
    std::signal(SIGINT, request_shutdown);
    int provided_thread_level = 0;
#if METAMATERIAL_USE_MPI
    mfem::Mpi::Init(
        argc, argv, MPI_THREAD_SERIALIZED, &provided_thread_level);
    mfem::Hypre::Init();
#else
    if (MPI_Init_thread(
            &argc, &argv, MPI_THREAD_SERIALIZED, &provided_thread_level)
            != MPI_SUCCESS) {
        std::cerr << "Could not initialize MPI for ParOpt.\n";
        return 1;
    }
#endif
    if (provided_thread_level < MPI_THREAD_SERIALIZED) {
        std::cerr << "The MPI runtime does not support the optimizer worker thread.\n";
#if !METAMATERIAL_USE_MPI
        MPI_Finalize();
#endif
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
        App::AppSettings appSettings;
        auto& solverSettings = appSettings.solverSettings;
        auto& optimizerSettings = appSettings.optSettings;

        App::SolverResult solverResult;

        // The solver owns its FE spaces and initializes these persistent vectors.
        App::LevelSet geometry;

        // Keep the renderer alive longer than the objects that will publish to it.
        std::optional<App::Renderer> renderer;
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
        App::solver_t solver(
            solverSettings,
            geometry,
            solverResult,
            log);

#if METAMATERIAL_USE_MPI
        if (rank != 0) {
            solver.parallelWorkerLoop();
        }
        else
#endif
        {
            try {
                renderer.emplace(appSettings, solverResult);
                App::optimizer_t optimizer(
                    optimizerSettings, solver, geometry, log);
                App::exporter_t exporter(
                    appSettings, solverResult, geometry, log);
                App::dispatcher_t dispatcher(optimizer, exporter, log);
                renderer->setup();
                log(App::LogLevel::Message, "Renderer initialized");

                while (!renderer->shouldClose && !interrupt_requested) {
                    dispatcher.dispatch();
                    renderer->displayFrame(dispatcher);
                }
            }
            catch (...) {
#if METAMATERIAL_USE_MPI
                solver.shutdownParallelWorkers();
#endif
                throw;
            }
#if METAMATERIAL_USE_MPI
            solver.shutdownParallelWorkers();
#endif
        }

    } catch (const std::exception& error) {
        std::cerr << "Application startup failed on MPI rank " << rank
                  << ": " << error.what() << '\n';
        exit_code = 1;
    }
    catch (...) {
        std::cerr << "Application failed with an unknown error on MPI rank "
                  << rank << ".\n";
        exit_code = 1;
    }
#if !METAMATERIAL_USE_MPI
    MPI_Finalize();
#endif
    return exit_code;
}
