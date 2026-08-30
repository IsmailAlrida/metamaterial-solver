#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "dispatcher.hpp"

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TestOptimizer {
    void prepare(bool throw_from_run = false)
    {
        cancel_requested.store(false);
        {
            std::lock_guard<std::mutex> lock(pause_mutex);
            pause_requested = false;
        }
        release.store(false);
        started.store(false);
        finished.store(false);
        throw_on_run.store(throw_from_run);
        run_calls.store(0);
        iteration.store(0);
        status.store(App::OptimizerStatus::Idle);
    }

    void prepare_run()
    {
        cancel_requested.store(false);
        {
            std::lock_guard<std::mutex> lock(pause_mutex);
            pause_requested = false;
        }
        status.store(App::OptimizerStatus::Working);
    }

    void run()
    {
        run_calls.fetch_add(1);
        status.store(App::OptimizerStatus::Working);
        started.store(true);

        if (throw_on_run.load()) {
            throw std::runtime_error("test optimizer failed");
        }

        while (!release.load() && !cancel_requested.load()) {
            std::unique_lock<std::mutex> lock(pause_mutex);
            if (pause_requested) {
                status.store(App::OptimizerStatus::Paused);
                pause_condition.wait(lock, [&]() {
                    return !pause_requested || cancel_requested.load();
                });
                if (!cancel_requested.load()) {
                    status.store(App::OptimizerStatus::Working);
                }
            }
            lock.unlock();
            std::this_thread::yield();
        }

        if (cancel_requested.load()) {
            status.store(App::OptimizerStatus::Cancelled);
        }
        else {
            iteration.store(3);
            status.store(App::OptimizerStatus::MaximumIterations);
        }

        finished.store(true);
    }

    void request_cancel()
    {
        cancel_requested.store(true);
        {
            std::lock_guard<std::mutex> lock(pause_mutex);
            pause_requested = false;
        }
        pause_condition.notify_all();
    }

    void request_pause()
    {
        std::lock_guard<std::mutex> lock(pause_mutex);
        pause_requested = true;
    }

    void resume()
    {
        {
            std::lock_guard<std::mutex> lock(pause_mutex);
            pause_requested = false;
        }
        pause_condition.notify_all();
    }

    bool is_pause_requested() const
    {
        std::lock_guard<std::mutex> lock(pause_mutex);
        return pause_requested;
    }

    App::OptimizerStatus get_status() const
    {
        return status.load();
    }

    App::SolverStatus get_solver_status() const
    {
        return App::SolverStatus::Converged;
    }

    int get_iteration() const
    {
        return iteration.load();
    }

    bool is_exportable() const
    {
        const App::OptimizerStatus current_status = status.load();
        return current_status == App::OptimizerStatus::MaximumIterations
            || current_status == App::OptimizerStatus::Paused
            || current_status == App::OptimizerStatus::Cancelled;
    }

    double get_pass_objective() const { return 1.0; }
    double get_stop_objective() const { return 2.0; }
    double get_mma_bound() const { return 2.0; }

    std::atomic_bool cancel_requested{false};
    std::atomic_bool release{false};
    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool throw_on_run{false};
    mutable std::mutex pause_mutex;
    std::condition_variable pause_condition;
    bool pause_requested = false;
    std::atomic<int> run_calls{0};
    std::atomic<int> iteration{0};
    std::atomic<App::OptimizerStatus> status{App::OptimizerStatus::Idle};
};

struct TestExporter {
    bool exportRunData(const std::filesystem::path&,
                       App::OptimizerStatus,
                       int,
                       double,
                       double,
                       double)
    {
        calls.fetch_add(1);
        started.store(true);
        while (block.load() && !release.load()) {
            std::this_thread::yield();
        }
        if (cancel_flag != nullptr) {
            saw_cancel_during_export.store(cancel_flag->load());
        }
        return succeeds.load();
    }

    std::atomic<int> calls{0};
    std::atomic_bool succeeds{true};
    std::atomic_bool block{false};
    std::atomic_bool release{false};
    std::atomic_bool started{false};
    std::atomic_bool saw_cancel_during_export{false};
    std::atomic_bool* cancel_flag = nullptr;
};

template <typename Predicate>
void wait_until(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(message);
        }
        std::this_thread::yield();
    }
}

template <typename DispatcherType>
void poll_until(DispatcherType& dispatcher,
                typename DispatcherType::State state,
                const char* message)
{
    wait_until([&]() {
        dispatcher.dispatch();
        return dispatcher.get_state() == state;
    }, message);
}

} // namespace

int main()
{
    try {
        TestOptimizer optimizer;
        TestExporter exporter;
        std::mutex log_mutex;
        std::vector<std::string> logs;
        App::LogFunction log = [&](App::LogLevel, std::string message) {
            std::lock_guard<std::mutex> lock(log_mutex);
            logs.push_back(std::move(message));
        };

        using Dispatcher = App::Dispatcher<TestOptimizer, TestExporter>;

        optimizer.prepare();
        Dispatcher dispatcher(optimizer, exporter, log);
        dispatcher.dispatch(Dispatcher::Event::Start);
        dispatcher.dispatch(Dispatcher::Event::Pause);
        wait_until([&]() { return optimizer.started.load(); },
                   "Start did not launch the workflow.");
        dispatcher.dispatch(Dispatcher::Event::Start);
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        require(optimizer.run_calls.load() == 1,
                "Repeated Start launched another workflow.");
        require(exporter.calls.load() == 0,
                "Export ran while the workflow was active.");

        poll_until(dispatcher, Dispatcher::State::Paused,
                   "An immediate Pause was lost before the worker started.");
        require(dispatcher.is_pause_requested(),
                "Paused optimizer lost its pause request.");
        require(dispatcher.is_exportable(),
                "A paused, converged design was not exportable.");
        dispatcher.dispatch(Dispatcher::Event::Pause);
        require(dispatcher.get_state() == Dispatcher::State::Paused,
                "A repeated Pause changed dispatcher state.");

        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        require(dispatcher.get_state() == Dispatcher::State::Exporting,
                "Paused export did not enter Exporting.");
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        poll_until(dispatcher, Dispatcher::State::Paused,
                   "Paused export did not return to Paused.");
        require(exporter.calls.load() == 1,
                "Paused export did not run exactly once.");

        dispatcher.dispatch(Dispatcher::Event::Resume);
        require(dispatcher.get_state() == Dispatcher::State::Working,
                "Resume did not return to Working.");
        dispatcher.dispatch(Dispatcher::Event::Resume);
        require(dispatcher.get_state() == Dispatcher::State::Working,
                "A repeated Resume changed dispatcher state.");
        require(optimizer.run_calls.load() == 1,
                "Resume launched a second optimizer worker.");
        dispatcher.dispatch(Dispatcher::Event::Pause);
        poll_until(dispatcher, Dispatcher::State::Paused,
                   "Second pause did not complete.");
        dispatcher.dispatch(Dispatcher::Event::Cancel);
        dispatcher.dispatch(Dispatcher::Event::Cancel);
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Cancelled paused workflow did not return to Idle.");
        require(optimizer.get_status() == App::OptimizerStatus::Cancelled,
                "Cancel did not wake the paused optimizer.");

        optimizer.prepare();
        dispatcher.dispatch(Dispatcher::Event::Start);
        wait_until([&]() { return optimizer.started.load(); },
                   "Completion workflow did not start.");
        optimizer.release.store(true);
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Completed workflow did not return to Idle.");
        require(dispatcher.get_optimizer_status()
                    == App::OptimizerStatus::MaximumIterations,
                "Maximum-iteration status was not retained.");
        require(dispatcher.is_exportable(),
                "Maximum-iteration result was not exportable.");

        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        require(dispatcher.get_state() == Dispatcher::State::Exporting,
                "Export did not enter Exporting.");
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Export did not return to Idle.");
        require(exporter.calls.load() == 2,
                "Idle export did not run exactly once.");

        optimizer.prepare();
        dispatcher.dispatch(Dispatcher::Event::Start);
        dispatcher.dispatch(Dispatcher::Event::Cancel);
        wait_until([&]() { return optimizer.started.load(); },
                   "Cancellation test workflow did not start.");
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "An immediate Cancel was lost before the worker started.");
        require(optimizer.get_status() == App::OptimizerStatus::Cancelled,
                "Cancel was not observed by the workflow.");

        optimizer.prepare(true);
        dispatcher.dispatch(Dispatcher::Event::Start);
        poll_until(dispatcher, Dispatcher::State::Error,
                   "Worker exception did not enter Error.");
        dispatcher.dispatch();
        require(dispatcher.get_state() == Dispatcher::State::Idle,
                "Error did not recover to Idle on the next poll.");

        optimizer.prepare();
        optimizer.status.store(App::OptimizerStatus::MaximumIterations);
        exporter.succeeds.store(false);
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        poll_until(dispatcher, Dispatcher::State::Error,
                   "Export failure did not enter Error.");
        dispatcher.dispatch();
        require(dispatcher.get_state() == Dispatcher::State::Idle,
                "Export error did not recover to Idle.");

        TestOptimizer shutdown_optimizer;
        TestExporter shutdown_exporter;
        shutdown_optimizer.prepare();
        shutdown_exporter.block.store(true);
        shutdown_exporter.cancel_flag = &shutdown_optimizer.cancel_requested;
        std::thread release_export;
        {
            Dispatcher shutdown_dispatcher(
                shutdown_optimizer,
                shutdown_exporter,
                log);
            shutdown_dispatcher.dispatch(Dispatcher::Event::Start);
            wait_until([&]() { return shutdown_optimizer.started.load(); },
                       "Shutdown workflow did not start.");
            shutdown_dispatcher.dispatch(Dispatcher::Event::Pause);
            poll_until(shutdown_dispatcher, Dispatcher::State::Paused,
                       "Shutdown workflow did not pause.");
            shutdown_dispatcher.dispatch(Dispatcher::Event::Export, ".");
            wait_until([&]() { return shutdown_exporter.started.load(); },
                       "Shutdown export did not start.");
            release_export = std::thread([&]() {
                std::this_thread::sleep_for(10ms);
                shutdown_exporter.release.store(true);
            });
        }
        release_export.join();
        require(!shutdown_exporter.saw_cancel_during_export.load(),
                "Dispatcher cancelled optimization before export finished.");
        require(shutdown_optimizer.finished.load(),
                "Dispatcher destruction did not wait for active work.");

        std::cout << "dispatcher backend check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "dispatcher backend check failed: "
                  << error.what() << '\n';
        return 1;
    }
}
