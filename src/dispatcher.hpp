#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <utility>

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

template <typename optimizer_t, typename exporter_t>
class Dispatcher {
public:
    enum class State {
        Idle,
        Working,
        Exporting,
        Error
    };

    enum class Event {
        Start,
        Cancel,
        Export
    };

    Dispatcher(optimizer_t& optimizer,
               exporter_t& exporter,
               const LogFunction& log)
        : optimizer(optimizer),
          exporter(exporter),
          log(log)
    {
    }

    ~Dispatcher()
    {
        optimizer.request_cancel();
        if (worker.valid()) {
            worker.wait();
        }
    }

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void dispatch(Event event)
    {
        dispatch(event, {});
    }

    void dispatch(Event event, std::filesystem::path directory)
    {
        if (event == Event::Cancel && state == State::Working) {
            optimizer.request_cancel();
            return;
        }
        if (state != State::Idle) {
            log(LogLevel::Warning,
                "Event is invalid for the current dispatcher state.");
            return;
        }

        try {
            if (event == Event::Start) {
                optimizer.prepare_run();
                state = State::Working;
                worker = std::async(
                    std::launch::async, [this]() { optimizer.run(); });
                return;
            }
            if (event == Event::Export) {
                if (!optimizer.is_exportable()) {
                    log(LogLevel::Warning,
                        "Export requires a completed optimization result.");
                    return;
                }
                if (directory.empty()) {
                    log(LogLevel::Warning,
                        "Select an export directory first.");
                    return;
                }
                export_directory = std::move(directory);
                state = State::Exporting;
                worker = std::async(std::launch::async, [this]() {
                    if (!exporter.exportRunData(
                            export_directory,
                            optimizer.get_status(),
                            optimizer.get_iteration(),
                            optimizer.get_pass_objective(),
                            optimizer.get_stop_objective(),
                            optimizer.get_epigraph_bound())) {
                        throw std::runtime_error("Run-data export failed.");
                    }
                });
            }
        }
        catch (const std::exception& error) {
            log(LogLevel::Error, error.what());
            state = State::Error;
            recover_error = true;
        }
    }

    // Called once per frame to poll the one active operation without blocking.
    void dispatch()
    {
        using namespace std::chrono_literals;

        if (state == State::Error && recover_error) {
            recover_error = false;
            state = State::Idle;
            return;
        }
        if (!worker.valid()
            || worker.wait_for(0ms) != std::future_status::ready) {
            return;
        }

        try {
            worker.get();
            state = State::Idle;
        }
        catch (const std::exception& error) {
            log(LogLevel::Error, error.what());
            state = State::Error;
            recover_error = true;
        }
        catch (...) {
            log(LogLevel::Error, "Unknown dispatcher worker error.");
            state = State::Error;
            recover_error = true;
        }
    }

    State get_state() const { return state; }
    SolverStatus get_solver_status() const
    {
        return optimizer.get_solver_status();
    }
    OptimizerStatus get_optimizer_status() const
    {
        return optimizer.get_status();
    }
    int get_iteration() const { return optimizer.get_iteration(); }
    bool is_exportable() const { return optimizer.is_exportable(); }
    double get_pass_objective() const
    {
        return optimizer.get_pass_objective();
    }
    double get_stop_objective() const
    {
        return optimizer.get_stop_objective();
    }
    double get_epigraph_bound() const
    {
        return optimizer.get_epigraph_bound();
    }

private:
    optimizer_t& optimizer;
    exporter_t& exporter;
    const LogFunction& log;
    State state = State::Idle;
    bool recover_error = false;
    std::filesystem::path export_directory;
    std::future<void> worker;
};

} // namespace App
