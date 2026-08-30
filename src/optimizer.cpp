#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "ParOptComplexStep.h"
#include "ParOptOptimizer.h"

namespace App {

namespace {

struct OptimizationCancelled {
};

} // namespace

Optimizer::Optimizer(const OptimizerSettings& settings,
                     Solver& solver,
                     LevelSet& geometry,
                     const SolverResult& result,
                     const LogFunction& log)
    : settings(settings),
      solver(solver),
      geometry(geometry),
      result(result),
      log(log)
{
}

class Optimizer::Problem final : public ::ParOptProblem {
public:
    Problem(Optimizer& optimizer, ObjectiveEvaluation initial_objective)
        : ::ParOptProblem(MPI_COMM_SELF),
          optimizer(optimizer),
          objective(std::move(initial_objective))
    {
        active_design_size = optimizer.geometry.activeDesignDofs.Size();
        constraint_size = (objective.has_pass ? 1 : 0)
            + (objective.has_stop ? 1 : 0);
        if (active_design_size == 0 || constraint_size == 0) {
            throw std::runtime_error(
                "ParOpt requires active design variables and at least one objective group.");
        }

        setProblemSizes(active_design_size + 1, constraint_size, 0);
        setNumInequalities(constraint_size, 0);

        const double initial_worst = std::max(
            objective.has_pass ? objective.pass : 0.0,
            objective.has_stop ? objective.stop : 0.0);
        objective_scale = std::max(
            initial_worst, std::numeric_limits<double>::epsilon());
        initial_bound = initial_worst / objective_scale;
        evaluated_design.resize(active_design_size);
        for (int design = 0; design < active_design_size; ++design) {
            evaluated_design[design] = optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]];
        }
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
        ParOptScalar* x;
        ParOptScalar* lower;
        ParOptScalar* upper;
        variables->getArray(&x);
        lower_bounds->getArray(&lower);
        upper_bounds->getArray(&upper);
        for (int design = 0; design < active_design_size; ++design) {
            x[design] = evaluated_design[design];
            lower[design] = 0.0;
            upper[design] = 1.0;
        }

        // The constraints bound z from below. Keep its box effectively open,
        // matching the paper instead of imposing an extra optimization rule.
        x[active_design_size] = initial_bound;
        lower[active_design_size] = -1.0e20;
        upper[active_design_size] = 1.0e20;
    }

    int evalObjCon(
        ParOptVec* variables,
        ParOptScalar* objective_value,
        ParOptScalar* constraints) override
    {
        evaluate(variables);
        if (objective_evaluations > 0) {
            optimizer.iteration.store(objective_evaluations);
            optimizer.log(LogLevel::Message,
                "Completed optimization iteration "
                    + std::to_string(objective_evaluations)
                    + ": pass/stop = " + std::to_string(objective.pass)
                    + " / " + std::to_string(objective.stop) + ".");
        }
        ++objective_evaluations;

        ParOptScalar* x;
        variables->getArray(&x);
        const double bound = ParOptRealPart(x[active_design_size]);
        *objective_value = bound;
        optimizer.mma_bound.store(bound);

        int constraint = 0;
        if (objective.has_pass) {
            constraints[constraint++] =
                bound - objective.pass / objective_scale;
        }
        if (objective.has_stop) {
            constraints[constraint++] =
                bound - objective.stop / objective_scale;
        }
        return 0;
    }

    int evalObjConGradient(
        ParOptVec* variables,
        ParOptVec* objective_gradient,
        ParOptVec** constraint_gradients) override
    {
        if (!matches(variables)) {
            throw std::runtime_error(
                "ParOpt requested a gradient before evaluating this design.");
        }
        differentiate();

        objective_gradient->zeroEntries();
        ParOptScalar* gradient;
        objective_gradient->getArray(&gradient);
        gradient[active_design_size] = 1.0;

        int constraint = 0;
        if (objective.has_pass) {
            fillConstraintGradient(
                pass_gradient, constraint_gradients[constraint++]);
        }
        if (objective.has_stop) {
            fillConstraintGradient(
                stop_gradient, constraint_gradients[constraint++]);
        }
        return 0;
    }

    bool matches(ParOptVec* variables) const
    {
        ParOptScalar* x;
        variables->getArray(&x);
        for (int design = 0; design < active_design_size; ++design) {
            if (ParOptRealPart(x[design]) != evaluated_design[design]) {
                return false;
            }
        }
        return true;
    }

private:
    void evaluate(ParOptVec* variables)
    {
        if (matches(variables)) {
            return;
        }
        if (optimizer.cancel_requested.load()) {
            throw OptimizationCancelled{};
        }
        const auto started_at = std::chrono::steady_clock::now();

        ParOptScalar* x;
        variables->getArray(&x);
        const mfem::Vector previous_design(optimizer.geometry.design);
        for (int design = 0; design < active_design_size; ++design) {
            const double value = ParOptRealPart(x[design]);
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "ParOpt proposed a non-finite design variable.");
            }
            optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]] =
                    std::clamp(value, 0.0, 1.0);
        }
        optimizer.geometry.enforceDesignConstraints();

        if (!optimizer.solver.assembleSolutionSpace()
            || !optimizer.solver.solve()) {
            optimizer.geometry.design = previous_design;
            optimizer.geometry.enforceDesignConstraints();
            throw std::runtime_error("The forward analysis failed inside ParOpt.");
        }

        ObjectiveEvaluation next_objective;
        if (!optimizer.evaluateObjectives(next_objective)) {
            optimizer.geometry.design = previous_design;
            optimizer.geometry.enforceDesignConstraints();
            throw std::runtime_error("The FFT objective could not be evaluated.");
        }

        objective = std::move(next_objective);
        for (int design = 0; design < active_design_size; ++design) {
            evaluated_design[design] = optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]];
        }
        gradients_ready = false;
        optimizer.pass_objective.store(objective.pass);
        optimizer.stop_objective.store(objective.stop);
        optimizer.performance_data.forwardCallbackSeconds +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
    }

    void differentiate()
    {
        if (gradients_ready) {
            return;
        }
        const auto started_at = std::chrono::steady_clock::now();
        static const std::vector<std::complex<double>> no_derivative;
        if (!optimizer.solver.differentiateFrequencyResponses(
                objective.has_pass
                    ? objective.pass_spectrum_derivative : no_derivative,
                objective.has_stop
                    ? objective.stop_spectrum_derivative : no_derivative,
                pass_gradient,
                stop_gradient)) {
            throw std::runtime_error(
                "The pass/stop discrete adjoint failed.");
        }
        optimizer.performance_data.gradientCallbackSeconds +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
        gradients_ready = true;
    }

    void fillConstraintGradient(
        const mfem::Vector& design_gradient,
        ParOptVec* constraint_gradient) const
    {
        constraint_gradient->zeroEntries();
        ParOptScalar* gradient;
        constraint_gradient->getArray(&gradient);
        for (int design = 0; design < active_design_size; ++design) {
            gradient[design] = -design_gradient[
                optimizer.geometry.activeDesignDofs[design]] / objective_scale;
        }
        gradient[active_design_size] = 1.0;
    }

    Optimizer& optimizer;
    ObjectiveEvaluation objective;
    mfem::Vector pass_gradient;
    mfem::Vector stop_gradient;
    std::vector<double> evaluated_design;
    double objective_scale = 1.0;
    double initial_bound = 1.0;
    int active_design_size = 0;
    int constraint_size = 0;
    int objective_evaluations = 0;
    bool gradients_ready = false;
};

void Optimizer::run()
{
    cancel_requested.store(false);
    iteration.store(0);
    pass_objective.store(0.0);
    stop_objective.store(0.0);
    mma_bound.store(0.0);
    status.store(OptimizerStatus::Working);

    try {
        if (!solver.setMesh()
            || !solver.assembleSolutionSpace()
            || !solver.solve()) {
            status.store(
                solver.get_status() == SolverStatus::Diverged
                    ? OptimizerStatus::Diverged
                    : OptimizerStatus::Error);
            return;
        }

        if (cancel_requested.load()) {
            status.store(OptimizerStatus::Cancelled);
            return;
        }
        optimize();
    }
    catch (const std::exception& error) {
        status.store(OptimizerStatus::Error);
        log(LogLevel::Error,
            "Optimization failed: " + std::string(error.what()));
    }
    catch (...) {
        status.store(OptimizerStatus::Error);
        log(LogLevel::Error, "Optimization failed with an unknown error.");
    }
}

bool Optimizer::evaluateObjectives(ObjectiveEvaluation& objective) const
{
    objective = {};
    const FrequencyResponse& response = solver.frequencyResponse();
    const std::size_t bin_count = response.frequency.size();
    if (bin_count == 0
        || response.outlet.size() != bin_count
        || response.reference.size() != bin_count
        || response.valid.size() != bin_count) {
        log(LogLevel::Error,
            "The solver has not produced a complete frequency response.");
        return false;
    }

    std::vector<const FrequencyBand*> bands;
    bands.reserve(settings.frequencyBands.size());
    for (const FrequencyBand& band : settings.frequencyBands) {
        if (!std::isfinite(band.startHz)
            || !std::isfinite(band.endHz)
            || !std::isfinite(band.targetTransmission)
            || band.startHz < 0.0
            || band.startHz >= band.endHz
            || band.targetTransmission <= 0.0) {
            log(LogLevel::Error,
                "Every frequency band needs a positive target and ordered finite bounds.");
            return false;
        }
        bands.push_back(&band);
    }
    std::sort(bands.begin(), bands.end(),
        [](const FrequencyBand* first, const FrequencyBand* second) {
            if (first->startHz == second->startHz) {
                return first->endHz < second->endHz;
            }
            return first->startHz < second->startHz;
        });
    for (std::size_t band = 1; band < bands.size(); ++band) {
        if (bands[band]->startHz < bands[band - 1]->endHz) {
            log(LogLevel::Error, "Frequency bands cannot overlap.");
            return false;
        }
    }

    objective.pass_spectrum_derivative.assign(bin_count, {0.0, 0.0});
    objective.stop_spectrum_derivative.assign(bin_count, {0.0, 0.0});
    int active_bins = 0;
    for (std::size_t bin = 0; bin < bin_count; ++bin) {
        if (response.valid[bin] == 0) {
            continue;
        }

        const double frequency = response.frequency[bin];
        const FrequencyBand* selected = nullptr;
        for (const FrequencyBand* band : bands) {
            if (frequency < band->startHz) {
                break;
            }
            if (frequency <= band->endHz) {
                selected = band;
                break;
            }
        }
        if (selected == nullptr) {
            continue;
        }

        const double reference_amplitude = std::abs(response.reference[bin]);
        const double outlet_amplitude = std::abs(response.outlet[bin]);
        if (!std::isfinite(reference_amplitude)
            || !std::isfinite(outlet_amplitude)
            || reference_amplitude <= 0.0) {
            log(LogLevel::Error,
                "A selected FFT bin contains a non-finite response.");
            return false;
        }

        const double target = selected->targetTransmission;
        const double transmission = outlet_amplitude / reference_amplitude;
        const double relative_error = (transmission - target) / target;
        const double error = relative_error * relative_error;
        const double derivative_scale = outlet_amplitude
                > std::numeric_limits<double>::epsilon()
                    * std::max(1.0, reference_amplitude)
            ? 2.0 * (transmission - target)
                / (target * target * reference_amplitude * outlet_amplitude)
            : 0.0;
        const std::complex<double> derivative =
            derivative_scale * response.outlet[bin];

        if (selected->type == FrequencyBandType::pass) {
            objective.pass += error;
            objective.has_pass = true;
            objective.pass_spectrum_derivative[bin] = derivative;
        }
        else {
            objective.stop += error;
            objective.has_stop = true;
            objective.stop_spectrum_derivative[bin] = derivative;
        }
        ++active_bins;
    }

    if (active_bins == 0) {
        log(LogLevel::Error,
            "No valid FFT bins fall inside the configured objective bands.");
        return false;
    }
    return true;
}

bool Optimizer::optimize()
{
    performance_data = {};
    if (settings.maxIterations <= 0
        || settings.mmaInitialAsymptote < 0.0
        || settings.mmaInitialAsymptote > 1.0
        || settings.mmaDecreaseAsymptote < 0.0
        || settings.mmaDecreaseAsymptote > 1.0
        || settings.mmaIncreaseAsymptote < 1.0
        || settings.mmaConstraintPenalty < 0.0) {
        log(LogLevel::Error, "The MMA settings are invalid.");
        status.store(OptimizerStatus::Error);
        return false;
    }

    int mpi_initialized = 0;
    int mpi_finalized = 0;
    MPI_Initialized(&mpi_initialized);
    MPI_Finalized(&mpi_finalized);
    if (mpi_initialized == 0 || mpi_finalized != 0) {
        log(LogLevel::Error,
            "ParOpt requires an active MPI runtime for the optimization worker.");
        status.store(OptimizerStatus::Error);
        return false;
    }

    ObjectiveEvaluation objective;
    if (!evaluateObjectives(objective)) {
        status.store(OptimizerStatus::Error);
        return false;
    }
    pass_objective.store(objective.pass);
    stop_objective.store(objective.stop);
    mma_bound.store(
        std::max(objective.pass, objective.stop) > 0.0 ? 1.0 : 0.0);
    log(LogLevel::Message,
        "Initial pass/stop objectives: "
            + std::to_string(objective.pass) + " / "
            + std::to_string(objective.stop) + ".");

    try {
        auto release = [](auto* object) {
            if (object != nullptr) {
                object->decref();
            }
        };

        std::unique_ptr<Problem, decltype(release)> problem(
            new Problem(*this, std::move(objective)), release);
        problem->incref();

        std::unique_ptr<ParOptOptions, decltype(release)> options(
            new ParOptOptions(MPI_COMM_SELF), release);
        options->incref();
        ParOptOptimizer::addDefaultOptions(options.get());
        int option_error = 0;
        option_error |= options->setOption("algorithm", "mma");
        option_error |= options->setOption("mma_output_file", "");
        option_error |= options->setOption(
            "mma_max_iterations", settings.maxIterations);
        option_error |= options->setOption(
            "mma_init_asymptote_offset", settings.mmaInitialAsymptote);
        option_error |= options->setOption(
            "mma_asymptote_contract", settings.mmaDecreaseAsymptote);
        option_error |= options->setOption(
            "mma_asymptote_relax", settings.mmaIncreaseAsymptote);
        option_error |= options->setOption(
            "penalty_gamma", settings.mmaConstraintPenalty);
        option_error |= options->setOption("mma_l1_tol", 0.0);
        option_error |= options->setOption("mma_linfty_tol", 0.0);
        option_error |= options->setOption("mma_infeas_tol", 0.0);
        if (option_error != 0) {
            throw std::runtime_error(
                "ParOpt rejected one or more MMA settings.");
        }

        std::unique_ptr<ParOptOptimizer, decltype(release)> optimizer(
            new ParOptOptimizer(problem.get(), options.get()), release);
        optimizer->incref();
        log(LogLevel::Message,
            "Starting ParOpt MMA with "
                + std::to_string(geometry.activeDesignDofs.Size())
                + " active design variables and "
                + std::to_string(settings.maxIterations)
                + " requested iterations.");
        const auto paropt_started_at = std::chrono::steady_clock::now();
        optimizer->optimize();
        performance_data.paroptSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - paropt_started_at).count();

        if (cancel_requested.load()) {
            status.store(OptimizerStatus::Cancelled);
            log(LogLevel::Warning,
                "Optimization cancelled after the latest completed design.");
            return false;
        }
        if (iteration.load() != settings.maxIterations) {
            throw std::runtime_error(
                "ParOpt stopped before the requested fixed iteration count.");
        }

        ParOptVec* optimized_design = nullptr;
        optimizer->getOptimizedPoint(
            &optimized_design,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (optimized_design == nullptr || !problem->matches(optimized_design)) {
            throw std::runtime_error(
                "ParOpt returned a design that has not completed its forward analysis.");
        }

        status.store(OptimizerStatus::MaximumIterations);
        log(LogLevel::Message,
            "Completed " + std::to_string(iteration.load())
                + " ParOpt MMA iterations.");
        return true;
    }
    catch (const OptimizationCancelled&) {
        status.store(OptimizerStatus::Cancelled);
        log(LogLevel::Warning,
            "Optimization cancelled after the latest completed design.");
        return false;
    }
    catch (const std::exception& error) {
        status.store(solver.get_status() == SolverStatus::Diverged
            ? OptimizerStatus::Diverged
            : OptimizerStatus::Error);
        log(LogLevel::Error,
            "ParOpt optimization failed: " + std::string(error.what()));
        return false;
    }
}

void Optimizer::request_cancel()
{
    cancel_requested.store(true);
}

OptimizerStatus Optimizer::get_status() const
{
    return status.load();
}

SolverStatus Optimizer::get_solver_status() const
{
    return solver.get_status();
}

int Optimizer::get_iteration() const
{
    return iteration.load();
}

bool Optimizer::is_exportable() const
{
    const OptimizerStatus current_status = status.load();
    return current_status == OptimizerStatus::Converged
        || current_status == OptimizerStatus::MaximumIterations
        || (current_status == OptimizerStatus::Cancelled
            && solver.get_status() == SolverStatus::Converged);
}

double Optimizer::get_pass_objective() const
{
    return pass_objective.load();
}

double Optimizer::get_stop_objective() const
{
    return stop_objective.load();
}

double Optimizer::get_mma_bound() const
{
    return mma_bound.load();
}

const OptimizerPerformance& Optimizer::performance() const
{
    return performance_data;
}

} // namespace App
