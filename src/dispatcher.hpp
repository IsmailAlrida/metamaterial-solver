#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <stdexcept>
#include <utility>

#include "global_types.hpp"
#include "logging.hpp"

namespace App {

template <typename OptimizerType, typename ExporterType>
class Dispatcher {

    public:
        using Callback = std::function<void()>;

        enum class State {
            Idle,
            Working,
            Paused,
            Exporting,
            Error
        };

        enum class Event {
            Start,
            Pause,
            Resume,
            Cancel,
            Export
        };

        Dispatcher(OptimizerType& optimizer,
                   ExporterType& exporter,
                   const LogFunction& log)
            : optimizer(optimizer),
              exporter(exporter),
              log(log),
              transitions{
                  {{State::Idle, Event::Start},
                   {State::Working, [this]() {
                       this->optimizer.run();
                   }}},
                  {{State::Working, Event::Pause},
                   {State::Working, [this]() {
                       this->optimizer.request_pause();
                   }}},
                  {{State::Paused, Event::Resume},
                   {State::Working, [this]() {
                       this->optimizer.resume();
                   }}},
                  {{State::Working, Event::Cancel},
                   {State::Working, [this]() {
                       this->optimizer.request_cancel();
                   }}},
                  {{State::Paused, Event::Cancel},
                   {State::Working, [this]() {
                       this->optimizer.request_cancel();
                   }}},
                  {{State::Idle, Event::Export},
                   {State::Exporting, [this]() {
                       this->export_run_data();
                   }}},
                  {{State::Paused, Event::Export},
                   {State::Exporting, [this]() {
                       this->export_run_data();
                   }}}
              }
        {
        }

        ~Dispatcher()
        {
            if (export_worker.valid()) {
                export_worker.wait();
            }

            optimizer.request_cancel();
            if (optimizer_worker.valid()) {
                optimizer_worker.wait();
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
            const auto transition = transitions.find({state, event});

            if (transition == transitions.end()) {
                log(LogLevel::Warning,
                    "Event is invalid for the current dispatcher state.");
                return;
            }

            const auto& [next_state, callback] = transition->second;

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

                export_return_state = state;
                export_directory = std::move(directory);
                state = next_state;
                try {
                    export_worker = std::async(std::launch::async, callback);
                }
                catch (const std::exception& error) {
                    log(LogLevel::Error, error.what());
                    error_return_state = export_return_state;
                    recover_error = true;
                    state = State::Error;
                }
                return;
            }

            if (event == Event::Pause
                || event == Event::Resume
                || event == Event::Cancel) {
                state = next_state;
                callback();
                return;
            }

            state = next_state;
            try {
                std::promise<bool> launch;
                std::future<bool> gate = launch.get_future();
                optimizer_worker = std::async(
                    std::launch::async,
                    [callback, gate = std::move(gate)]() mutable {
                        if (gate.get()) {
                            callback();
                        }
                    });
                try {
                    optimizer.prepare_run();
                }
                catch (...) {
                    launch.set_value(false);
                    optimizer_worker.wait();
                    throw;
                }
                launch.set_value(true);
            }
            catch (const std::exception& error) {
                log(LogLevel::Error, error.what());
                error_return_state = State::Idle;
                recover_error = true;
                state = State::Error;
            }
        }

        // Called once per frame to poll active operations without blocking.
        void dispatch()
        {
            using namespace std::chrono_literals;

            if (state == State::Error && recover_error) {
                recover_error = false;
                state = error_return_state;
                return;
            }

            if (export_worker.valid()
                && export_worker.wait_for(0ms) == std::future_status::ready) {
                try {
                    export_worker.get();
                    state = export_return_state;
                }
                catch (const std::exception& error) {
                    log(LogLevel::Error, error.what());
                    error_return_state = export_return_state;
                    recover_error = true;
                    state = State::Error;
                }
                catch (...) {
                    log(LogLevel::Error, "Unknown export callback error.");
                    error_return_state = export_return_state;
                    recover_error = true;
                    state = State::Error;
                }
                return;
            }

            if (optimizer_worker.valid()
                && optimizer_worker.wait_for(0ms) == std::future_status::ready) {
                try {
                    optimizer_worker.get();
                    state = State::Idle;
                }
                catch (const std::exception& error) {
                    log(LogLevel::Error, error.what());
                    error_return_state = State::Idle;
                    recover_error = true;
                    state = State::Error;
                }
                catch (...) {
                    log(LogLevel::Error, "Unknown dispatcher callback error.");
                    error_return_state = State::Idle;
                    recover_error = true;
                    state = State::Error;
                }
                return;
            }

            if (state == State::Working
                && optimizer.get_status() == OptimizerStatus::Paused
                && optimizer.is_pause_requested()) {
                state = State::Paused;
            }
        }

        State get_state() const
        {
            return state;
        }

        SolverStatus get_solver_status() const
        {
            return optimizer.get_solver_status();
        }

        OptimizerStatus get_optimizer_status() const
        {
            return optimizer.get_status();
        }

        int get_iteration() const
        {
            return optimizer.get_iteration();
        }

        bool is_exportable() const
        {
            return optimizer.is_exportable();
        }

        bool is_pause_requested() const
        {
            return optimizer.is_pause_requested();
        }

        double get_pass_objective() const
        {
            return optimizer.get_pass_objective();
        }

        double get_stop_objective() const
        {
            return optimizer.get_stop_objective();
        }

        double get_mma_bound() const
        {
            return optimizer.get_mma_bound();
        }

    private:
        void export_run_data()
        {
            if (!exporter.exportRunData(
                    export_directory,
                    optimizer.get_status(),
                    optimizer.get_iteration(),
                    optimizer.get_pass_objective(),
                    optimizer.get_stop_objective(),
                    optimizer.get_mma_bound())) {
                throw std::runtime_error("Run-data export failed.");
            }
        }

        OptimizerType& optimizer;
        ExporterType& exporter;
        const LogFunction& log;

        // TODO: Switch this to a flat array later if the transition table ever grows.
        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions;
        State state = State::Idle;
        State export_return_state = State::Idle;
        State error_return_state = State::Idle;
        bool recover_error = false;
        std::filesystem::path export_directory;
        std::future<void> optimizer_worker;
        std::future<void> export_worker;
};

} // namespace App
