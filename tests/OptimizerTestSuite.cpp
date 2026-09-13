#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <mpi.h>

#include "ParOptInteriorPoint.h"
#include "ParOptMMA.h"
#include "ParOptOptions.h"
#include "ParOptProblem.h"
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

enum class AnalyticProblem {
    symmetric_minimax,
    opening_direction,
    upper_bound_escape,
    flat_response,
    evaluation_failure
};

class AnalyticParOptProblem final : public ParOptProblem {
public:
    AnalyticParOptProblem(
        AnalyticProblem problem,
        double initial_design,
        double objective_magnitude)
        : ParOptProblem(MPI_COMM_SELF),
          problem(problem),
          initial_design(initial_design),
          objective_magnitude(objective_magnitude)
    {
        constraint_count = problem == AnalyticProblem::symmetric_minimax
            ? 2 : 1;
        setProblemSizes(2, constraint_count, 0);
        setNumInequalities(constraint_count, 0);
    }

    ParOptQuasiDefMat* createQuasiDefMat() override
    {
        return new ParOptQuasiDefBlockMat(this, 0);
    }

    void getVarsAndBounds(
        ParOptVec* variables,
        ParOptVec* lower_bounds,
        ParOptVec* upper_bounds) override
    {
        ParOptScalar* x = nullptr;
        ParOptScalar* lower = nullptr;
        ParOptScalar* upper = nullptr;
        variables->getArray(&x);
        lower_bounds->getArray(&lower);
        upper_bounds->getArray(&upper);
        x[0] = initial_design;
        x[1] = maximumObjective(initial_design) / objective_magnitude;
        lower[0] = 0.0;
        upper[0] = 1.0;
        lower[1] = 0.0;
        upper[1] = 1.0e20;
    }

    int evalObjCon(
        ParOptVec* variables,
        ParOptScalar* objective,
        ParOptScalar* constraints) override
    {
        if (problem == AnalyticProblem::evaluation_failure
            && evaluations++ != 0) {
            return 1;
        }
        ParOptScalar* x = nullptr;
        variables->getArray(&x);
        const double design = ParOptRealPart(x[0]);
        const double bound = ParOptRealPart(x[1]);
        *objective = bound;
        std::vector<double> values;
        objectiveValues(design, values);
        for (int index = 0; index < constraint_count; ++index) {
            constraints[index] = bound
                - values[static_cast<std::size_t>(index)] / objective_magnitude;
        }
        return 0;
    }

    int evalObjConGradient(
        ParOptVec* variables,
        ParOptVec* objective_gradient,
        ParOptVec** constraint_gradients) override
    {
        ParOptScalar* x = nullptr;
        variables->getArray(&x);
        std::vector<double> gradients;
        objectiveGradients(ParOptRealPart(x[0]), gradients);
        objective_gradient->zeroEntries();
        ParOptScalar* objective_entries = nullptr;
        objective_gradient->getArray(&objective_entries);
        objective_entries[1] = 1.0;
        for (int index = 0; index < constraint_count; ++index) {
            constraint_gradients[index]->zeroEntries();
            ParOptScalar* entries = nullptr;
            constraint_gradients[index]->getArray(&entries);
            entries[0] = -gradients[static_cast<std::size_t>(index)]
                / objective_magnitude;
            entries[1] = 1.0;
        }
        return 0;
    }

    void objectiveValues(double x, std::vector<double>& objectives) const
    {
        objectives.assign(static_cast<std::size_t>(constraint_count), 0.0);
        if (problem == AnalyticProblem::flat_response) {
            objectives[0] = objective_magnitude;
        }
        else if (problem == AnalyticProblem::upper_bound_escape) {
            objectives[0] = objective_magnitude * x * x;
        }
        else {
            const double difference = 1.0 - x;
            objectives[0] = objective_magnitude * difference * difference;
            if (problem == AnalyticProblem::symmetric_minimax) {
                objectives[1] = objective_magnitude * x * x;
            }
        }
    }

    double maximumObjective(double x) const
    {
        std::vector<double> objectives;
        objectiveValues(x, objectives);
        return *std::max_element(objectives.begin(), objectives.end());
    }

private:
    void objectiveGradients(double x, std::vector<double>& gradients) const
    {
        gradients.assign(static_cast<std::size_t>(constraint_count), 0.0);
        if (problem == AnalyticProblem::upper_bound_escape) {
            gradients[0] = 2.0 * objective_magnitude * x;
        }
        else if (problem != AnalyticProblem::flat_response) {
            gradients[0] = -2.0 * objective_magnitude * (1.0 - x);
            if (problem == AnalyticProblem::symmetric_minimax) {
                gradients[1] = 2.0 * objective_magnitude * x;
            }
        }
    }

    AnalyticProblem problem;
    double initial_design;
    double objective_magnitude;
    int constraint_count = 0;
    int evaluations = 0;
};

struct AnalyticResult {
    App::ParOptMmaResult run;
    std::vector<double> design;
    std::vector<double> objectives;
    double trueBound = 0.0;
};

AnalyticResult run_analytic_problem(
    AnalyticProblem analytic_problem,
    double initial_design,
    double objective_magnitude = 1.0,
    int iterations = 80)
{
    const auto release = [](auto* object) {
        if (object != nullptr) {
            object->decref();
        }
    };
    std::unique_ptr<AnalyticParOptProblem, decltype(release)> problem(
        new AnalyticParOptProblem(
            analytic_problem, initial_design, objective_magnitude), release);
    problem->incref();
    std::unique_ptr<ParOptOptions, decltype(release)> options(
        new ParOptOptions(MPI_COMM_SELF), release);
    options->incref();
    ParOptInteriorPoint::addDefaultOptions(options.get());
    ParOptMMA::addDefaultOptions(options.get());
    options->setOption("output_file", "");
    options->setOption("mma_output_file", "");
    options->setOption("mma_max_iterations", iterations);
    options->setOption("penalty_gamma", 1000.0);
    options->setOption("max_major_iters", 100);
    options->setOption("abs_res_tol", 1.0e-10);
    options->setOption("use_diag_hessian", 1);
    options->setOption("starting_point_strategy", "affine_step");
    options->setOption("barrier_strategy", "mehrotra");
    options->setOption("use_line_search", 0);
    std::unique_ptr<ParOptMMA, decltype(release)> mma(
        new ParOptMMA(problem.get(), options.get()), release);
    mma->incref();
    std::unique_ptr<ParOptInteriorPoint, decltype(release)> interior_point(
        new ParOptInteriorPoint(mma.get(), options.get()), release);
    interior_point->incref();

    AnalyticResult result;
    result.run = App::runParOptMma(*mma, *interior_point);
    ParOptVec* optimized = nullptr;
    mma->getOptimizedPoint(&optimized);
    if (optimized != nullptr) {
        ParOptScalar* x = nullptr;
        optimized->getArray(&x);
        result.design = {ParOptRealPart(x[0])};
        problem->objectiveValues(result.design[0], result.objectives);
        result.trueBound = problem->maximumObjective(result.design[0]);
    }
    return result;
}

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
        if (fail_gradient_call == gradient_calls) {
            status = App::SolverStatus::Error;
            return false;
        }
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
        if (nonfinite_gradient_call == gradient_calls) {
            mfem::Vector& corrupted = pass_derivative.empty()
                ? stop_gradient : pass_gradient;
            corrupted[0] = std::numeric_limits<double>::infinity();
        }
        return true;
    }

    int set_mesh_calls = 0;
    int assembly_calls = 0;
    int solve_calls = 0;
    int gradient_calls = 0;
    int fail_gradient_call = 0;
    int nonfinite_gradient_call = 0;

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
        const double error = relative_error * relative_error;
        if (test.group[bin] == Group::pass) {
            value.pass += error;
        }
        else {
            value.stop += error;
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
    int iterations,
    int fail_gradient_call = 0,
    int nonfinite_gradient_call = 0)
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
    solver.fail_gradient_call = fail_gradient_call;
    solver.nonfinite_gradient_call = nonfinite_gradient_call;
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

bool analytic_run_finished(const AnalyticResult& result)
{
    return result.run.error.empty() && result.run.completedIterations > 0;
}

bool analytic_result_is_finite(const AnalyticResult& result)
{
    const auto finite = [](const std::vector<double>& values) {
        return std::all_of(values.begin(), values.end(),
            [](double value) { return std::isfinite(value); });
    };
    return std::isfinite(result.trueBound)
        && std::isfinite(result.run.l1)
        && std::isfinite(result.run.linfinity)
        && std::isfinite(result.run.infeasibility)
        && finite(result.design)
        && finite(result.objectives);
}

void print_analytic_result(
    const char* name,
    const AnalyticResult& result)
{
    std::cerr << "[DIAGNOSTIC] " << name
              << ": converged=" << result.run.converged
              << ", iterations=" << result.run.completedIterations
              << ", true bound=" << result.trueBound
              << ", KKT l1/linfinity/infeasibility="
              << result.run.l1 << "/" << result.run.linfinity << "/"
              << result.run.infeasibility;
    if (!result.design.empty()) {
        std::cerr << ", design=" << result.design[0];
    }
    if (!result.run.error.empty()) {
        std::cerr << ", error=" << result.run.error;
    }
    std::cerr << '\n';
}

bool check_analytic_symmetric_minimax()
{
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::symmetric_minimax, 0.1);
    const bool complete = result.design.size() == 1
        && result.objectives.size() == 2;
    const double design = complete ? result.design[0] : 0.0;
    const double actual_maximum = std::max(
        design * design,
        (1.0 - design) * (1.0 - design));
    const bool passed = analytic_run_finished(result)
        && complete
        && analytic_result_is_finite(result)
        && near(design, 0.5, 2.0e-3)
        && near(result.trueBound, 0.25, 5.0e-3)
        && near(result.trueBound, actual_maximum);
    if (!passed) {
        print_analytic_result("analytic symmetric minimax", result);
    }
    return passed;
}

bool check_analytic_opening_direction()
{
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::opening_direction, 0.0);
    const bool complete = result.design.size() == 1
        && result.objectives.size() == 1;
    const double design = complete ? result.design[0] : 0.0;
    const double objective = (1.0 - design) * (1.0 - design);
    const bool passed = analytic_run_finished(result)
        && complete
        && analytic_result_is_finite(result)
        && design > 0.9
        && result.trueBound < 0.02
        && near(result.trueBound, objective);
    if (!passed) {
        print_analytic_result("analytic lower-bound escape", result);
    }
    return passed;
}

bool check_paropt_first_move_limit()
{
    const AnalyticResult opening = run_analytic_problem(
        AnalyticProblem::opening_direction, 0.0, 1.0, 1);
    const AnalyticResult closing = run_analytic_problem(
        AnalyticProblem::upper_bound_escape, 1.0, 1.0, 1);
    const bool passed = opening.run.error.empty() && closing.run.error.empty()
        && opening.design.size() == 1 && closing.design.size() == 1
        && opening.design[0] > 0.0 && opening.design[0] <= 0.2
        && closing.design[0] < 1.0 && closing.design[0] >= 0.8;
    if (!passed) {
        print_analytic_result("ParOpt first opening move", opening);
        print_analytic_result("ParOpt first closing move", closing);
    }
    return passed;
}

bool check_analytic_upper_bound_escape()
{
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::upper_bound_escape, 1.0);
    const bool complete = result.design.size() == 1
        && result.objectives.size() == 1;
    const double design = complete ? result.design[0] : 1.0;
    const double objective = design * design;
    const bool passed = analytic_run_finished(result)
        && complete
        && analytic_result_is_finite(result)
        && design < 0.1
        && result.trueBound < 0.02
        && near(result.trueBound, objective);
    if (!passed) {
        print_analytic_result("analytic upper-bound escape", result);
    }
    return passed;
}

bool check_analytic_flat_response()
{
    constexpr double initial_design = 0.37;
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::flat_response, initial_design);
    const bool passed = analytic_run_finished(result)
        && result.design.size() == 1
        && result.objectives.size() == 1
        && analytic_result_is_finite(result)
        && near(result.design[0], initial_design, 1.0e-7)
        && near(result.trueBound, 1.0, 1.0e-5);
    if (!passed) {
        print_analytic_result("analytic zero-gradient response", result);
    }
    return passed;
}

bool check_analytic_raw_objective_scale()
{
    constexpr double scale = 1.0e8;
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::symmetric_minimax, 0.1, scale);
    const bool complete = result.design.size() == 1
        && result.objectives.size() == 2;
    const double design = complete ? result.design[0] : 0.0;
    const double actual_maximum = scale * std::max(
        design * design,
        (1.0 - design) * (1.0 - design));
    const bool passed = analytic_run_finished(result)
        && complete
        && analytic_result_is_finite(result)
        && near(design, 0.5, 2.0e-3)
        && near(result.trueBound / scale, 0.25, 5.0e-3)
        && near(result.trueBound, actual_maximum);
    if (!passed) {
        print_analytic_result("analytic raw objective scale", result);
    }
    return passed;
}

bool check_analytic_evaluation_failure()
{
    const AnalyticResult result = run_analytic_problem(
        AnalyticProblem::evaluation_failure, 0.2);
    return !result.run.error.empty()
        && result.run.completedIterations == 0;
}

bool check_paper_optimizer_defaults()
{
    const App::OptimizerSettings settings;
    if (settings.objectiveMode != App::ObjectiveMode::band
        || settings.maxIterations != 400
        || !near(settings.mmaInitialAsymptote, 0.5)
        || !near(settings.mmaDecreaseAsymptote, 0.7)
        || !near(settings.mmaIncreaseAsymptote, 1.2)
        || !near(settings.mmaConstraintPenalty, 1000.0)
        || settings.frequencyBands.size() != 2) {
        return false;
    }

    const App::FrequencyBand& pass = settings.frequencyBands[0];
    const App::FrequencyBand& stop = settings.frequencyBands[1];
    return pass.type == App::FrequencyBandType::pass
        && near(pass.startHz, 1000.0) && near(pass.endHz, 2500.0)
        && near(pass.targetTransmission, 1.0)
        && stop.type == App::FrequencyBandType::stop
        && near(stop.startHz, 2500.0) && near(stop.endHz, 4000.0)
        && near(stop.targetTransmission, 1.0e-2);
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
            "design and transmission objectives unchanged (tolerance 1e-6)";
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

bool check_first_adapter_step()
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
        print_result("one-step optimizer adapter", result, reason.str());
    }
    return passed;
}

bool check_converged_status()
{
    const CaseSpec test = freeform_case();
    const std::vector<double> expected = optimum(test);
    const CaseResult result = run_case(test, expected, 1.0, 30);
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

bool check_fake_response_adapter_escape()
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
        print_result("fake-response adapter escape", result, reason.str());
    }
    return passed;
}

bool check_failed_adapter_gradient_restores_design()
{
    const CaseSpec test = low_pass_case();
    const std::vector<double> initial(frequencies.size(), 0.5);
    const CaseResult callback_failure = run_case(test, initial, 1.0, 6, 2);
    const CaseResult nonfinite_gradient = run_case(test, initial, 1.0, 6, 0, 2);
    const auto restored = [&](const CaseResult& result) {
        return result.status == App::OptimizerStatus::Error
            && result.iterations == 0
            && result.solve_calls >= 3
            && result.gradient_calls == 2
            && design_error(result.design, initial) < 1.0e-20;
    };
    return restored(callback_failure) && restored(nonfinite_gradient);
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
        std::cerr << "Optimizer test suite needs MPI_THREAD_SERIALIZED.\n";
        MPI_Finalize();
        return 1;
    }
    check(check_analytic_symmetric_minimax(),
        "ParOpt MMA failed min max{x^2, (1-x)^2}: expected x=0.5, z=0.25.");
    check(check_analytic_opening_direction(),
        "ParOpt MMA did not leave a lower bound with an exact improving gradient.");
    check(check_paropt_first_move_limit(),
        "ParOpt MMA violated its default 0.2 first move limit.");
    check(check_analytic_upper_bound_escape(),
        "ParOpt MMA did not leave an upper bound with an exact improving gradient.");
    check(check_analytic_flat_response(),
        "ParOpt MMA moved a design when the analytical objective had zero gradient.");
    check(check_analytic_raw_objective_scale(),
        "ParOpt MMA changed the minimax solution under fixed objective normalization.");
    check(check_analytic_evaluation_failure(),
        "The corrected ParOpt MMA driver did not report an objective failure.");
    check(check_paper_optimizer_defaults(),
        "OptimizerSettings no longer matches the published low-pass baseline.");
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
    check(check_first_adapter_step(),
        "The optimizer adapter did not improve its exact analytical response.");
    check(check_converged_status(),
        "An already optimal design did not report ParOpt convergence.");
    check(check_cancelled_status(),
        "Prepared cancellation did not stop before the forward solve.");
    check(check_fake_response_adapter_escape(),
        "The optimizer adapter ignored the fake response's exact opening gradient.");
    check(check_failed_adapter_gradient_restores_design(),
        "The optimizer adapter retained a candidate whose gradient failed.");

    MPI_Finalize();
    if (failures != 0) {
        return 1;
    }
    std::cout << "optimizer test suite passed\n";
    return 0;
}
