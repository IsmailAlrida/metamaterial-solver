#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <mpi.h>

#include "optimizer.hpp"

namespace {

constexpr std::array<double, 6> frequencies{
    100.0, 200.0, 300.0, 400.0, 500.0, 600.0};
constexpr double minimum_transmission = 0.05;
constexpr double transmission_span = 0.95;

enum class Group {
    pass,
    stop
};

struct CaseSpec {
    App::OptimizerSettings settings;
    std::array<double, frequencies.size()> target{};
    std::array<Group, frequencies.size()> group{};
};

struct Objectives {
    double pass = 0.0;
    double stop = 0.0;
};

struct CaseResult {
    std::vector<double> design;
    Objectives expected_objectives;
    App::OptimizerStatus status = App::OptimizerStatus::Idle;
    App::SolverStatus solver_status = App::SolverStatus::Idle;
    double pass = 0.0;
    double stop = 0.0;
    double bound = 0.0;
    int iterations = 0;
    int solve_calls = 0;
    int gradient_calls = 0;
    bool exportable = false;
    bool logged_error = false;
    std::string error;
};

class FakeSolver final : public App::ForwardSolver {
public:
    FakeSolver(App::LevelSet& geometry, double spectrum_scale)
        : geometry(geometry), spectrum_scale(spectrum_scale)
    {
        response.frequency.assign(frequencies.begin(), frequencies.end());
        response.outlet.resize(frequencies.size());
        response.reference.resize(frequencies.size());
        response.valid.assign(frequencies.size(), 1);
    }

    bool setMesh() override
    {
        ++set_mesh_calls;
        status = App::SolverStatus::Working;
        return true;
    }

    bool assembleSolutionSpace() override
    {
        ++assembly_calls;
        return true;
    }

    bool solve() override
    {
        ++solve_calls;
        if (geometry.design.Size() < static_cast<int>(frequencies.size())) {
            status = App::SolverStatus::Error;
            return false;
        }

        for (std::size_t bin = 0; bin < frequencies.size(); ++bin) {
            const double design = geometry.design[static_cast<int>(bin)];
            if (!std::isfinite(design)) {
                status = App::SolverStatus::Error;
                return false;
            }
            const double phase = 0.17 * static_cast<double>(bin + 1);
            const std::complex<double> direction = std::polar(1.0, phase);
            const double transmission =
                minimum_transmission + transmission_span * design;
            response.reference[bin] = spectrum_scale
                * std::polar(1.0, -0.09 * static_cast<double>(bin + 1));
            response.outlet[bin] = spectrum_scale * transmission * direction;
        }
        status = App::SolverStatus::Converged;
        return true;
    }

    App::SolverStatus get_status() const override
    {
        return status;
    }

    const App::FrequencyResponse& frequencyResponse() const override
    {
        return response;
    }

    bool differentiateFrequencyResponses(
        const std::vector<std::complex<double>>& pass_derivative,
        const std::vector<std::complex<double>>& stop_derivative,
        mfem::Vector& pass_gradient,
        mfem::Vector& stop_gradient) override
    {
        ++gradient_calls;
        if ((!pass_derivative.empty()
                && pass_derivative.size() != frequencies.size())
            || (!stop_derivative.empty()
                && stop_derivative.size() != frequencies.size())) {
            status = App::SolverStatus::Error;
            return false;
        }

        pass_gradient.SetSize(geometry.design.Size());
        stop_gradient.SetSize(geometry.design.Size());
        pass_gradient = 0.0;
        stop_gradient = 0.0;
        for (std::size_t bin = 0; bin < frequencies.size(); ++bin) {
            const std::complex<double> response_derivative =
                spectrum_scale * transmission_span
                * std::polar(1.0, 0.17 * static_cast<double>(bin + 1));
            if (!pass_derivative.empty()) {
                pass_gradient[static_cast<int>(bin)] = std::real(
                    std::conj(pass_derivative[bin]) * response_derivative);
            }
            if (!stop_derivative.empty()) {
                stop_gradient[static_cast<int>(bin)] = std::real(
                    std::conj(stop_derivative[bin]) * response_derivative);
            }
        }
        return true;
    }

    int set_mesh_calls = 0;
    int assembly_calls = 0;
    int solve_calls = 0;
    int gradient_calls = 0;

private:
    App::LevelSet& geometry;
    double spectrum_scale;
    App::FrequencyResponse response;
    App::SolverStatus status = App::SolverStatus::Idle;
};

CaseSpec low_pass_case()
{
    CaseSpec test;
    test.settings.objectiveMode = App::ObjectiveMode::band;
    test.settings.frequencyBands = {
        {App::FrequencyBandType::pass, 100.0, 300.0, 1.0},
        {App::FrequencyBandType::stop, 300.0, 600.0, 0.2}
    };
    test.target = {1.0, 1.0, 1.0, 0.2, 0.2, 0.2};
    test.group = {Group::pass, Group::pass, Group::pass,
                  Group::stop, Group::stop, Group::stop};
    return test;
}

CaseSpec high_pass_case()
{
    CaseSpec test;
    test.settings.objectiveMode = App::ObjectiveMode::band;
    test.settings.frequencyBands = {
        {App::FrequencyBandType::stop, 100.0, 300.0, 0.2},
        {App::FrequencyBandType::pass, 300.0, 600.0, 1.0}
    };
    test.target = {0.2, 0.2, 0.2, 1.0, 1.0, 1.0};
    test.group = {Group::stop, Group::stop, Group::stop,
                  Group::pass, Group::pass, Group::pass};
    return test;
}

CaseSpec band_pass_case()
{
    CaseSpec test;
    test.settings.objectiveMode = App::ObjectiveMode::band;
    test.settings.frequencyBands = {
        {App::FrequencyBandType::stop, 100.0, 200.0, 0.2},
        {App::FrequencyBandType::pass, 200.0, 500.0, 1.0},
        {App::FrequencyBandType::stop, 500.0, 600.0, 0.2}
    };
    test.target = {0.2, 0.2, 1.0, 1.0, 1.0, 0.2};
    test.group = {Group::stop, Group::stop, Group::pass,
                  Group::pass, Group::pass, Group::stop};
    return test;
}

CaseSpec freeform_case()
{
    CaseSpec test;
    test.settings.objectiveMode = App::ObjectiveMode::freeform;
    test.settings.freeformObjective.frequencyHz.assign(
        frequencies.begin(), frequencies.end());
    test.target = {1.0, 0.2, 0.8, 0.35, 1.0, 0.2};
    test.settings.freeformObjective.targetTransmission.assign(
        test.target.begin(), test.target.end());
    test.group.fill(Group::pass);
    return test;
}

std::vector<double> optimum(const CaseSpec& test)
{
    std::vector<double> design(frequencies.size());
    for (std::size_t bin = 0; bin < frequencies.size(); ++bin) {
        design[bin] = (test.target[bin] - minimum_transmission)
            / transmission_span;
    }
    return design;
}

Objectives objectives(const CaseSpec& test, const std::vector<double>& design)
{
    Objectives value;
    int freeform_bins = 0;
    for (std::size_t bin = 0; bin < frequencies.size(); ++bin) {
        const double transmission = minimum_transmission
            + transmission_span * design[bin];
        const double relative_error =
            (transmission - test.target[bin]) / test.target[bin];
        if (test.group[bin] == Group::pass) {
            value.pass += relative_error * relative_error;
        }
        else {
            value.stop += relative_error * relative_error;
        }
        if (test.settings.objectiveMode == App::ObjectiveMode::freeform) {
            ++freeform_bins;
        }
    }
    if (freeform_bins != 0) {
        value.pass /= freeform_bins;
    }
    return value;
}

double design_error(
    const std::vector<double>& design,
    const std::vector<double>& expected)
{
    double error = 0.0;
    for (std::size_t i = 0; i < design.size(); ++i) {
        const double difference = design[i] - expected[i];
        error += difference * difference;
    }
    return error;
}

CaseResult run_case(
    CaseSpec test,
    const std::vector<double>& initial_design,
    double spectrum_scale,
    int iterations)
{
    test.settings.maxIterations = iterations;
    App::LevelSet geometry;
    geometry.design.SetSize(static_cast<int>(frequencies.size()) + 1);
    geometry.design = 0.0;
    mfem::Array<int> active(static_cast<int>(frequencies.size()));
    for (int i = 0; i < active.Size(); ++i) {
        active[i] = i;
        geometry.design[i] = initial_design[static_cast<std::size_t>(i)];
    }
    geometry.setActiveDesignDofs(active);

    bool logged_error = false;
    std::string error;
    const App::LogFunction log = [&](App::LogLevel level, std::string message) {
        if (level == App::LogLevel::Error) {
            logged_error = true;
            error = std::move(message);
        }
    };
    FakeSolver solver(geometry, spectrum_scale);
    App::Optimizer optimizer(test.settings, solver, geometry, log);
    optimizer.run();

    CaseResult result;
    result.design.resize(frequencies.size());
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        result.design[i] = geometry.design[static_cast<int>(i)];
    }
    result.expected_objectives = objectives(test, result.design);
    result.status = optimizer.get_status();
    result.solver_status = optimizer.get_solver_status();
    result.pass = optimizer.get_pass_objective();
    result.stop = optimizer.get_stop_objective();
    result.bound = optimizer.get_mma_bound();
    result.iterations = optimizer.get_iteration();
    result.solve_calls = solver.solve_calls;
    result.gradient_calls = solver.gradient_calls;
    result.exportable = optimizer.is_exportable();
    result.logged_error = logged_error;
    result.error = std::move(error);
    return result;
}

const char* status_name(App::OptimizerStatus status)
{
    switch (status) {
        case App::OptimizerStatus::Idle: return "Idle";
        case App::OptimizerStatus::Working: return "Working";
        case App::OptimizerStatus::Converged: return "Converged";
        case App::OptimizerStatus::MaximumIterations: return "MaximumIterations";
        case App::OptimizerStatus::Diverged: return "Diverged";
        case App::OptimizerStatus::Cancelled: return "Cancelled";
        case App::OptimizerStatus::Error: return "Error";
    }
    return "Unknown";
}

const char* status_name(App::SolverStatus status)
{
    switch (status) {
        case App::SolverStatus::Idle: return "Idle";
        case App::SolverStatus::Working: return "Working";
        case App::SolverStatus::Converged: return "Converged";
        case App::SolverStatus::Diverged: return "Diverged";
        case App::SolverStatus::Error: return "Error";
    }
    return "Unknown";
}

void print_result(const char* name, const CaseResult& result,
                  const std::string& reason = {})
{
    std::cerr << "[DIAGNOSTIC] " << name;
    if (!reason.empty()) {
        std::cerr << ": " << reason;
    }
    std::cerr << "\n  optimizer=" << status_name(result.status)
              << ", solver=" << status_name(result.solver_status)
              << ", iterations=" << result.iterations
              << ", solves=" << result.solve_calls
              << ", gradients=" << result.gradient_calls
              << ", pass/stop/bound=" << result.pass << "/"
              << result.stop << "/" << result.bound;
    if (!result.error.empty()) {
        std::cerr << ", error=" << result.error;
    }
    std::cerr << "\n  design:";
    for (double value : result.design) {
        std::cerr << ' ' << value;
    }
    std::cerr << '\n';
}

bool near(double left, double right, double tolerance = 1.0e-8)
{
    return std::abs(left - right) <= tolerance
        * std::max({1.0, std::abs(left), std::abs(right)});
}

std::string unsuccessful_reason(const CaseResult& result)
{
    const bool finished = result.status == App::OptimizerStatus::Converged
        || result.status == App::OptimizerStatus::MaximumIterations;
    const double worst = std::max(result.pass, result.stop);
    std::ostringstream reason;
    if (!finished) {
        reason << "optimizer ended in " << status_name(result.status)
               << "; expected Converged or MaximumIterations";
    }
    else if (result.solver_status != App::SolverStatus::Converged) {
        reason << "solver ended in " << status_name(result.solver_status)
               << "; expected Converged";
    }
    else if (!result.exportable) {
        reason << "completed result is not exportable";
    }
    else if (result.logged_error) {
        reason << "optimizer logged an error: " << result.error;
    }
    else if (result.iterations <= 0) {
        reason << "optimizer reported " << result.iterations
               << " iterations; expected at least one";
    }
    else if (result.solve_calls < 2 || result.gradient_calls == 0) {
        reason << "callback counts were solves=" << result.solve_calls
               << ", gradients=" << result.gradient_calls
               << "; expected at least 2/1";
    }
    else if (!std::isfinite(result.pass) || !std::isfinite(result.stop)
             || !std::isfinite(result.bound)) {
        reason << "non-finite pass/stop/bound=" << result.pass << "/"
               << result.stop << "/" << result.bound;
    }
    else if (result.bound + 1.0e-5 * std::max(1.0, worst) < worst) {
        reason << "epigraph bound " << result.bound
               << " is below active objective " << worst
               << " beyond tolerance "
               << 1.0e-5 * std::max(1.0, worst);
    }
    else if (!near(result.pass, result.expected_objectives.pass)
             || !near(result.stop, result.expected_objectives.stop)) {
        reason << "published pass/stop=" << result.pass << "/"
               << result.stop << ", recomputed="
               << result.expected_objectives.pass << "/"
               << result.expected_objectives.stop;
    }
    else {
        for (std::size_t i = 0; i < result.design.size(); ++i) {
            if (!std::isfinite(result.design[i])
                || result.design[i] < 0.0 || result.design[i] > 1.0) {
                reason << "design[" << i << "]=" << result.design[i]
                       << " lies outside [0,1]";
                break;
            }
        }
    }
    return reason.str();
}

bool successful(const CaseResult& result)
{
    return unsuccessful_reason(result).empty();
}

bool check_filter_case(const char* name, const CaseSpec& test)
{
    const std::vector<double> initial(frequencies.size(), 0.5);
    const std::vector<double> expected = optimum(test);
    const CaseResult result = run_case(test, initial, 1.0, 30);
    std::string reason = unsuccessful_reason(result);
    const double initial_error = design_error(initial, expected);
    const double final_error = design_error(result.design, expected);
    if (reason.empty() && final_error >= 0.5 * initial_error) {
        std::ostringstream message;
        message << "design error fell from " << initial_error << " to "
                << final_error << "; required final < "
                << 0.5 * initial_error;
        reason = message.str();
    }
    const bool passed = reason.empty();
    if (!passed) {
        print_result(name, result, reason);
    }
    return passed;
}

bool check_common_spectrum_scale()
{
    const CaseSpec test = band_pass_case();
    const std::vector<double> initial(frequencies.size(), 0.5);
    const CaseResult unit = run_case(test, initial, 1.0, 30);
    const CaseResult scaled = run_case(test, initial, 1.0e8, 30);
    if (!successful(unit) || !successful(scaled)
        || !near(unit.pass, scaled.pass, 1.0e-6)
        || !near(unit.stop, scaled.stop, 1.0e-6)
        || !near(unit.bound, scaled.bound, 1.0e-6)) {
        const std::string reason = "common FFT scaling should leave the "
            "design and normalized objectives unchanged (tolerance 1e-6)";
        print_result("unit spectrum", unit,
                     unsuccessful_reason(unit).empty()
                        ? reason : unsuccessful_reason(unit));
        print_result("scaled spectrum", scaled,
                     unsuccessful_reason(scaled).empty()
                        ? reason : unsuccessful_reason(scaled));
        return false;
    }
    for (std::size_t i = 0; i < unit.design.size(); ++i) {
        if (!near(unit.design[i], scaled.design[i], 1.0e-6)) {
            std::ostringstream reason;
            reason << "design[" << i << "] differs after common FFT scaling: "
                   << unit.design[i] << " versus " << scaled.design[i]
                   << " (relative tolerance 1e-6)";
            print_result("unit spectrum", unit, reason.str());
            print_result("scaled spectrum", scaled, reason.str());
            return false;
        }
    }
    return true;
}

bool check_epigraph_tracks_active_objective()
{
    const CaseSpec test = freeform_case();
    const std::vector<double> initial(frequencies.size(), 0.5);
    const CaseResult result = run_case(test, initial, 1.0, 1);
    const double worst = std::max(result.pass, result.stop);
    const double initial_worst = objectives(test, initial).pass;
    const bool finished = result.status == App::OptimizerStatus::Converged
        || result.status == App::OptimizerStatus::MaximumIterations;
    const bool passed = finished
        && result.solver_status == App::SolverStatus::Converged
        && result.iterations == 1
        && result.solve_calls >= 2
        && std::isfinite(result.bound)
        && worst < 0.75 * initial_worst
        && near(result.bound, worst, 5.0e-3);
    if (!passed) {
        std::ostringstream reason;
        reason << "after one iteration: initial worst=" << initial_worst
               << ", final worst=" << worst << " (required < "
               << 0.75 * initial_worst << "), bound=" << result.bound
               << " (required within 0.5% of " << worst << ")";
        print_result("one-step epigraph", result, reason.str());
    }
    return passed;
}

bool check_converged_status()
{
    const CaseSpec test = freeform_case();
    const CaseResult result = run_case(test, optimum(test), 1.0, 30);
    const bool passed = result.status == App::OptimizerStatus::Converged
        && result.solver_status == App::SolverStatus::Converged
        && result.exportable
        && !result.logged_error
        && std::isfinite(result.bound)
        && near(result.pass, 0.0, 1.0e-6)
        && near(result.stop, 0.0, 1.0e-6);
    if (!passed) {
        print_result("already optimal", result,
            "an exact optimum should terminate as Converged with zero objectives");
    }
    return passed;
}

bool check_cancelled_status()
{
    CaseSpec test = low_pass_case();
    test.settings.maxIterations = 10;
    App::LevelSet geometry;
    geometry.design.SetSize(static_cast<int>(frequencies.size()));
    geometry.design = 0.5;
    mfem::Array<int> active(geometry.design.Size());
    for (int i = 0; i < active.Size(); ++i) {
        active[i] = i;
    }
    geometry.setActiveDesignDofs(active);

    const App::LogFunction log = [](App::LogLevel, std::string) {};
    FakeSolver solver(geometry, 1.0);
    App::Optimizer optimizer(test.settings, solver, geometry, log);
    optimizer.prepare_run();
    optimizer.request_cancel();
    optimizer.run();
    const bool passed = optimizer.get_status() == App::OptimizerStatus::Cancelled
        && optimizer.get_iteration() == 0
        && solver.set_mesh_calls == 0
        && solver.assembly_calls == 0
        && solver.solve_calls == 0
        && !optimizer.is_exportable();
    if (!passed) {
        std::cerr << "[DIAGNOSTIC] prepared cancellation: optimizer="
                  << status_name(optimizer.get_status())
                  << ", iterations=" << optimizer.get_iteration()
                  << ", setMesh/assemble/solve=" << solver.set_mesh_calls
                  << "/" << solver.assembly_calls << "/"
                  << solver.solve_calls
                  << "; expected Cancelled, 0, and 0/0/0\n";
    }
    return passed;
}

bool check_wall_trap_escape()
{
    const CaseSpec test = low_pass_case();
    const std::vector<double> wall(frequencies.size(), 0.0);
    const Objectives initial = objectives(test, wall);
    const CaseResult result = run_case(test, wall, 1.0, 6);
    const double initial_worst = std::max(initial.pass, initial.stop);
    const double final_worst = std::max(result.pass, result.stop);
    const bool passed = successful(result)
        && final_worst < initial_worst
        && result.design[0] > wall[0]
        && result.design[1] > wall[1]
        && result.design[2] > wall[2];
    if (!passed) {
        std::ostringstream reason;
        reason << "worst objective changed from " << initial_worst
               << " to " << final_worst
               << "; pass-band design variables must move above zero";
        print_result("wall escape", result, reason.str());
    }
    return passed;
}

} // namespace

int main(int argc, char** argv)
{
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided)
            != MPI_SUCCESS) {
        std::cerr << "Optimizer test suite could not initialize MPI.\n";
        return 1;
    }

    int failures = 0;
    const auto check = [&](bool passed, const char* message) {
        if (!passed) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    if (provided < MPI_THREAD_SERIALIZED) {
        check(false, "Optimizer test suite needs MPI_THREAD_SERIALIZED.");
    }
    else {
        check(check_filter_case("low-pass", low_pass_case()),
            "The real Optimizer failed the curated low-pass objective.");
        check(check_filter_case("high-pass", high_pass_case()),
            "The real Optimizer failed the curated high-pass objective.");
        check(check_filter_case("band-pass", band_pass_case()),
            "The real Optimizer failed the curated band-pass objective.");
        check(check_filter_case("freeform", freeform_case()),
            "The real Optimizer failed the curated freeform objective.");
        check(check_common_spectrum_scale(),
            "Common FFT scaling changed the optimizer result.");
        check(check_epigraph_tracks_active_objective(),
            "The epigraph bound lagged behind the active objective like a geometry variable.");
        check(check_converged_status(),
            "An already optimal design did not report convergence.");
        check(check_cancelled_status(),
            "Prepared cancellation did not stop before the forward solve.");
        check(check_wall_trap_escape(),
            "The optimizer stayed in a wall despite an exact opening gradient.");
    }

    MPI_Finalize();
    if (failures != 0) {
        return 1;
    }
    std::cout << "optimizer test suite passed\n";
    return 0;
}
