#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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
    void reset(bool should_throw = false)
    {
        cancel.store(false);
        release.store(false);
        started.store(false);
        finished.store(false);
        throw_on_run.store(should_throw);
        calls.store(0);
        status.store(App::OptimizerStatus::Idle);
    }

    void prepare_run()
    {
        cancel.store(false);
        status.store(App::OptimizerStatus::Working);
    }

    void run()
    {
        calls.fetch_add(1);
        started.store(true);
        if (throw_on_run.load()) {
            throw std::runtime_error("test optimizer failed");
        }
        while (!release.load() && !cancel.load()) {
            std::this_thread::yield();
        }
        status.store(cancel.load()
            ? App::OptimizerStatus::Cancelled
            : App::OptimizerStatus::MaximumIterations);
        finished.store(true);
    }

    void request_cancel() { cancel.store(true); }
    App::OptimizerStatus get_status() const { return status.load(); }
    App::SolverStatus get_solver_status() const
    {
        return App::SolverStatus::Converged;
    }
    int get_iteration() const { return 3; }
    bool is_exportable() const
    {
        return status.load() == App::OptimizerStatus::MaximumIterations;
    }
    double get_pass_objective() const { return 1.0; }
    double get_stop_objective() const { return 2.0; }
    double get_mma_bound() const { return 2.0; }

    std::atomic_bool cancel{false};
    std::atomic_bool release{false};
    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool throw_on_run{false};
    std::atomic<int> calls{0};
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
        return succeeds.load();
    }

    std::atomic<int> calls{0};
    std::atomic_bool succeeds{true};
};

template <typename predicate_t>
void wait_until(predicate_t predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(message);
        }
        std::this_thread::yield();
    }
}

template <typename dispatcher_t>
void poll_until(dispatcher_t& dispatcher,
                typename dispatcher_t::State state,
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
        App::LogFunction log = [](App::LogLevel, std::string) {};
        using Dispatcher = App::Dispatcher<TestOptimizer, TestExporter>;

        optimizer.reset();
        Dispatcher dispatcher(optimizer, exporter, log);
        dispatcher.dispatch(Dispatcher::Event::Start);
        wait_until([&]() { return optimizer.started.load(); },
                   "Start did not launch the worker.");
        dispatcher.dispatch(Dispatcher::Event::Start);
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        require(optimizer.calls.load() == 1,
                "A second operation overlapped the active worker.");
        require(exporter.calls.load() == 0,
                "Export overlapped optimization.");
        dispatcher.dispatch(Dispatcher::Event::Cancel);
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Cancellation did not return to Idle.");

        optimizer.reset();
        dispatcher.dispatch(Dispatcher::Event::Start);
        wait_until([&]() { return optimizer.started.load(); },
                   "Completion run did not start.");
        optimizer.release.store(true);
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Completion did not return to Idle.");
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        poll_until(dispatcher, Dispatcher::State::Idle,
                   "Export did not return to Idle.");
        require(exporter.calls.load() == 1,
                "Idle export did not run exactly once.");

        optimizer.reset(true);
        dispatcher.dispatch(Dispatcher::Event::Start);
        poll_until(dispatcher, Dispatcher::State::Error,
                   "Worker exception did not enter Error.");
        dispatcher.dispatch();
        require(dispatcher.get_state() == Dispatcher::State::Idle,
                "Error did not recover on the next poll.");

        optimizer.reset();
        optimizer.status.store(App::OptimizerStatus::MaximumIterations);
        exporter.succeeds.store(false);
        dispatcher.dispatch(Dispatcher::Event::Export, ".");
        poll_until(dispatcher, Dispatcher::State::Error,
                   "Export failure did not enter Error.");
        dispatcher.dispatch();
        require(dispatcher.get_state() == Dispatcher::State::Idle,
                "Export error did not recover on the next poll.");

        TestOptimizer shutdown_optimizer;
        shutdown_optimizer.reset();
        {
            Dispatcher shutdown_dispatcher(
                shutdown_optimizer, exporter, log);
            shutdown_dispatcher.dispatch(Dispatcher::Event::Start);
            wait_until([&]() { return shutdown_optimizer.started.load(); },
                       "Shutdown run did not start.");
        }
        require(shutdown_optimizer.finished.load(),
                "Dispatcher destruction did not cancel and wait.");

        std::cout << "dispatcher backend check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "dispatcher backend check failed: "
                  << error.what() << '\n';
        return 1;
    }
}
