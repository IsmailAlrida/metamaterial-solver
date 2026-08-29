#pragma once

#include <chrono>
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

        // TODO: Add pause mechanism later to pause from either opt/solve.
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

        Dispatcher(OptimizerType& optimizer,
                   ExporterType& exporter,
                   const LogFunction& log)
            : optimizer(optimizer),
              exporter(exporter),
              log(log),
              transitions{
                  {{State::Idle, Event::Start},
                   {State::Working, [this]() {
                       optimizer.run();
                   }}},
                  {{State::Working, Event::Cancel},
                   {State::Working, [this]() {
                       optimizer.request_cancel();
                   }}},
                  {{State::Idle, Event::Export},
                   {State::Exporting, [this]() {
                       if (!exporter.exportMesh()) {
                           throw std::runtime_error("Mesh export failed.");
                       }
                   }}}
              }
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
            const auto transition = transitions.find({state, event});

            if (transition == transitions.end()) {
                log(LogLevel::Warning,
                    "Event is invalid for the current dispatcher state.");
                return;
            }

            const auto& [next_state, callback] = transition->second;

            if (event == Event::Cancel) {
                callback();
                return;
            }

            if (event == Event::Export && !optimizer.is_exportable()) {
                log(LogLevel::Warning,
                    "Export requires a completed optimization result.");
                return;
            }

            state = next_state;

            try {
                worker = std::async(std::launch::async, callback);
            }
            catch (const std::exception& error) {
                log(LogLevel::Error, error.what());
                state = State::Error;
            }
        }

        // Called once per frame to poll the active operation without blocking.
        void dispatch()
        {
            using namespace std::chrono_literals;

            if (state == State::Error && !worker.valid()) {
                // Log the error later
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
            }
            catch (...) {
                log(LogLevel::Error, "Unknown dispatcher callback error.");
                state = State::Error;
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

    private:
        OptimizerType& optimizer;
        ExporterType& exporter;
        const LogFunction& log;

        // TODO: Switch this to a flat array later if the transition table ever grows.
        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions;
        State state = State::Idle;
        std::future<void> worker;
};

} // namespace App
