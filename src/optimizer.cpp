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

#include "ParOptOptimizer.h"

namespace App {

namespace {

struct OptimizationCancelled {
};

// ParOpt's MMA implementation exposes its subproblem through ParOptProblem's
// virtual methods. Preserve the base MMA approximation for geometry while the
// epigraph variable remains exactly linear and free of geometry move limits.
class EpigraphMMA final : public ::ParOptMMA {
public:
    EpigraphMMA(
        ::ParOptProblem* problem,
        ::ParOptOptions* options,
        int epigraph_index)
        : ::ParOptMMA(problem, options),
          epigraph_index(epigraph_index)
    {
        int variables = 0;
        int constraints = 0;
        int sparse_constraints = 0;
        problem->getProblemSizes(
            &variables, &constraints, &sparse_constraints);
        if (epigraph_index < 0 || epigraph_index >= variables) {
            throw std::out_of_range("The MMA epigraph index is invalid.");
        }
        centered_constraints.resize(constraints);
        centered = createDesignVec();
        centered->incref();
    }

    ~EpigraphMMA() override
    {
        centered->decref();
    }

    void getVarsAndBounds(
        ::ParOptVec* variables,
        ::ParOptVec* lower_bounds,
        ::ParOptVec* upper_bounds) override
    {
        ::ParOptMMA::getVarsAndBounds(
            variables, lower_bounds, upper_bounds);
        ::ParOptScalar* lower;
        ::ParOptScalar* upper;
        lower_bounds->getArray(&lower);
        upper_bounds->getArray(&upper);
        lower[epigraph_index] = -1.0e20;
        upper[epigraph_index] = 1.0e20;
    }

    int evalObjCon(
        ::ParOptVec* variables,
        ::ParOptScalar* objective,
        ::ParOptScalar* constraints) override
    {
        ::ParOptVec* center = nullptr;
        getOptimizedPoint(&center);
        ::ParOptScalar* candidate_values;
        ::ParOptScalar* center_values;
        variables->getArray(&candidate_values);
        center->getArray(&center_values);
        const ::ParOptScalar epigraph_step =
            candidate_values[epigraph_index]
            - center_values[epigraph_index];

        freezeEpigraph(variables, center_values[epigraph_index]);
        ::ParOptScalar centered_objective = 0.0;
        const int failed = ::ParOptMMA::evalObjCon(
            centered, &centered_objective,
            centered_constraints.data());
        *objective = centered_objective + epigraph_step;
        for (std::size_t constraint = 0;
             constraint < centered_constraints.size(); ++constraint) {
            constraints[constraint] =
                centered_constraints[constraint] + epigraph_step;
        }
        return failed;
    }

    int evalObjConGradient(
        ::ParOptVec* variables,
        ::ParOptVec* objective_gradient,
        ::ParOptVec** constraint_gradients) override
    {
        const int failed = ::ParOptMMA::evalObjConGradient(
            frozenAtCenter(variables),
            objective_gradient,
            constraint_gradients);
        ::ParOptScalar* gradient;
        objective_gradient->getArray(&gradient);
        gradient[epigraph_index] = 1.0;
        for (std::size_t constraint = 0;
             constraint < centered_constraints.size(); ++constraint) {
            constraint_gradients[constraint]->getArray(&gradient);
            gradient[epigraph_index] = 1.0;
        }
        return failed;
    }

    int evalHvecProduct(
        ::ParOptVec* variables,
        ::ParOptScalar* multipliers,
        ::ParOptVec* sparse_multipliers,
        ::ParOptVec* direction,
        ::ParOptVec* product) override
    {
        const int failed = ::ParOptMMA::evalHvecProduct(
            frozenAtCenter(variables), multipliers,
            sparse_multipliers, direction, product);
        ::ParOptScalar* values;
        product->getArray(&values);
        values[epigraph_index] = 0.0;
        return failed;
    }

    int evalHessianDiag(
        ::ParOptVec* variables,
        ::ParOptScalar* multipliers,
        ::ParOptVec* sparse_multipliers,
        ::ParOptVec* diagonal) override
    {
        const int failed = ::ParOptMMA::evalHessianDiag(
            frozenAtCenter(variables), multipliers,
            sparse_multipliers, diagonal);
        ::ParOptScalar* values;
        diagonal->getArray(&values);
        values[epigraph_index] = 0.0;
        return failed;
    }

private:
    void freezeEpigraph(
        ::ParOptVec* variables,
        ::ParOptScalar center_value)
    {
        centered->copyValues(variables);
        ::ParOptScalar* values;
        centered->getArray(&values);
        values[epigraph_index] = center_value;
    }

    ::ParOptVec* frozenAtCenter(::ParOptVec* variables)
    {
        ::ParOptVec* center = nullptr;
        getOptimizedPoint(&center);
        ::ParOptScalar* center_values;
        center->getArray(&center_values);
        freezeEpigraph(variables, center_values[epigraph_index]);
        return centered;
    }

    int epigraph_index;
    ::ParOptVec* centered = nullptr;
    std::vector<::ParOptScalar> centered_constraints;
};

struct MMAResult {
    bool converged = false;
    int iterations = 0;
    double l1 = std::numeric_limits<double>::infinity();
    double linfinity = std::numeric_limits<double>::infinity();
    double infeasibility = std::numeric_limits<double>::infinity();
};

MMAResult runMMA(
    ::ParOptMMA& mma,
    ::ParOptInteriorPoint& interior_point,
    ::ParOptOptions& options)
{
    MMAResult result;
    const int maximum_iterations =
        options.getIntOption("mma_max_iterations");
    const double l1_tolerance =
        options.getFloatOption("mma_l1_tol");
    const double linfinity_tolerance =
        options.getFloatOption("mma_linfty_tol");
    const double infeasibility_tolerance =
        options.getFloatOption("mma_infeas_tol");

    if (mma.initializeSubProblem(nullptr) != 0) {
        throw std::runtime_error(
            "ParOpt could not initialize the first MMA subproblem.");
    }
    interior_point.resetDesignAndBounds();
    for (int iteration = 0;
         iteration < maximum_iterations; ++iteration) {
        if (interior_point.optimize() != 0) {
            throw std::runtime_error(
                "ParOpt's interior-point MMA subproblem failed.");
        }

        ::ParOptVec* variables = nullptr;
        ::ParOptVec* sparse_lower = nullptr;
        ::ParOptVec* lower = nullptr;
        ::ParOptVec* upper = nullptr;
        ::ParOptScalar* dense = nullptr;
        interior_point.getOptimizedPoint(
            &variables, &dense, &sparse_lower, &lower, &upper);
        mma.setMultipliers(dense, sparse_lower, lower, upper);
        if (mma.initializeSubProblem(variables) != 0) {
            throw std::runtime_error(
                "ParOpt could not update the MMA subproblem.");
        }
        interior_point.resetDesignAndBounds();

        result.iterations = iteration + 1;
        mma.computeKKTError(
            &result.l1, &result.linfinity, &result.infeasibility);
        if (result.infeasibility < infeasibility_tolerance
            && (result.l1 < l1_tolerance
                || result.linfinity < linfinity_tolerance)) {
            result.converged = true;
            break;
        }
    }
    return result;
}

} // namespace

Optimizer::Optimizer(const OptimizerSettings& settings,
                     ForwardSolver& solver,
                     LevelSet& geometry,
                     const LogFunction& log)
    : settings(settings),
      solver(solver),
      geometry(geometry),
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
        int completed_iteration = 0;
        if (initial_mma_evaluation) {
            initial_mma_evaluation = false;
        }
        else {
            completed_iteration = optimizer.iteration.fetch_add(1) + 1;
        }

        ParOptScalar* x;
        variables->getArray(&x);
        const double bound = ParOptRealPart(x[active_design_size]);
        *objective_value = bound;
        optimizer.mma_bound.store(bound * objective_scale);
        if (completed_iteration != 0) {
            optimizer.log(LogLevel::Message,
                "Completed MMA iteration "
                    + std::to_string(completed_iteration)
                    + ": pass/stop = " + std::to_string(objective.pass)
                    + " / " + std::to_string(objective.stop)
                    + ", raw/normalized bound = "
                    + std::to_string(bound * objective_scale)
                    + " / " + std::to_string(bound) + ".");
        }

        int constraint = 0;
        if (objective.has_pass) {
            constraints[constraint++] =
                bound - objective.pass / objective_scale;
        }
        if (objective.has_stop) {
            constraints[constraint++] =
                bound - objective.stop / objective_scale;
        }
        optimizer.check_cancelled();
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
        optimizer.check_cancelled();
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
        optimizer.check_cancelled();
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
        const auto restore_previous_design = [&]() {
            optimizer.geometry.design = previous_design;
            optimizer.geometry.enforceDesignConstraints();
            if (!optimizer.solver.assembleSolutionSpace()
                || !optimizer.solver.solve()) {
                optimizer.log(LogLevel::Error,
                    "Could not restore the last completed design after a failed candidate.");
            }
        };
        for (int design = 0; design < active_design_size; ++design) {
            const double value = ParOptRealPart(x[design]);
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "ParOpt proposed a non-finite design variable.");
            }
        }
        for (int design = 0; design < active_design_size; ++design) {
            const double value = ParOptRealPart(x[design]);
            optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]] =
                    std::clamp(value, 0.0, 1.0);
        }
        optimizer.geometry.enforceDesignConstraints();

        ObjectiveEvaluation next_objective;
        try {
            if (!optimizer.solver.assembleSolutionSpace()
                || !optimizer.solver.solve()) {
                throw std::runtime_error(
                    "The forward analysis failed inside ParOpt.");
            }
            if (!optimizer.evaluateObjectives(next_objective)) {
                throw std::runtime_error(
                    "The FFT objective could not be evaluated.");
            }
        }
        catch (...) {
            restore_previous_design();
            throw;
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
    bool initial_mma_evaluation = true;
    bool gradients_ready = false;
};

void Optimizer::prepare_run()
{
    clear_requests();
    run_prepared.store(true);
    status.store(OptimizerStatus::Working);
}

void Optimizer::run()
{
    if (!run_prepared.exchange(false)) {
        clear_requests();
    }
    iteration.store(0);
    pass_objective.store(0.0);
    stop_objective.store(0.0);
    mma_bound.store(0.0);
    status.store(OptimizerStatus::Working);

    try {
        if (cancel_requested.load()) {
            throw OptimizationCancelled{};
        }
        if (!solver.setMesh()
            || !solver.assembleSolutionSpace()
            || !solver.solve()) {
            status.store(
                solver.get_status() == SolverStatus::Diverged
                    ? OptimizerStatus::Diverged
                    : OptimizerStatus::Error);
        }
        else {
            optimize();
        }
    }
    catch (const OptimizationCancelled&) {
        status.store(OptimizerStatus::Cancelled);
        log(LogLevel::Warning,
            "Optimization cancelled after the latest completed design.");
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
    clear_requests();
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

    objective.pass_spectrum_derivative.assign(bin_count, {0.0, 0.0});
    objective.stop_spectrum_derivative.assign(bin_count, {0.0, 0.0});
    if (settings.objectiveMode == ObjectiveMode::freeform) {
        const FreeformObjective& freeform = settings.freeformObjective;
        if (freeform.frequencyHz.size() != freeform.targetTransmission.size()) {
            log(LogLevel::Error,
                "The freeform objective frequency and target arrays must match.");
            return false;
        }

        constexpr double target_floor = 1.0e-6;
        int objective_bins = 0;
        for (std::size_t bin = 0; bin < bin_count; ++bin) {
            if (response.valid[bin] == 0 || freeform.frequencyHz.empty()) {
                continue;
            }
            const auto found = std::lower_bound(
                freeform.frequencyHz.begin(),
                freeform.frequencyHz.end(),
                response.frequency[bin]);
            std::size_t sample = found == freeform.frequencyHz.end()
                ? freeform.frequencyHz.size() - 1
                : static_cast<std::size_t>(found - freeform.frequencyHz.begin());
            if (sample > 0
                && std::abs(freeform.frequencyHz[sample - 1] - response.frequency[bin])
                    < std::abs(freeform.frequencyHz[sample] - response.frequency[bin])) {
                --sample;
            }
            const double bin_width = bin_count > 1
                ? (bin + 1 < bin_count
                    ? response.frequency[bin + 1] - response.frequency[bin]
                    : response.frequency[bin] - response.frequency[bin - 1])
                : 0.0;
            if (std::abs(freeform.frequencyHz[sample] - response.frequency[bin])
                    > 0.5 * std::abs(bin_width) + 1.0e-9) {
                continue;
            }

            const double target = freeform.targetTransmission[sample];
            const double reference_amplitude = std::abs(response.reference[bin]);
            const double outlet_amplitude = std::abs(response.outlet[bin]);
            if (!std::isfinite(target) || target <= 0.0
                || !std::isfinite(reference_amplitude)
                || !std::isfinite(outlet_amplitude)
                || reference_amplitude <= 0.0) {
                log(LogLevel::Error,
                    "A freeform objective bin contains a non-finite response or target.");
                return false;
            }

            const double denominator = std::max(target, target_floor);
            const double transmission = outlet_amplitude / reference_amplitude;
            const double relative_error = (transmission - target) / denominator;
            objective.pass += relative_error * relative_error;
            const double derivative_scale = outlet_amplitude
                    > std::numeric_limits<double>::epsilon()
                        * std::max(1.0, reference_amplitude)
                ? 2.0 * (transmission - target)
                    / (denominator * denominator
                       * reference_amplitude * outlet_amplitude)
                : 0.0;
            objective.pass_spectrum_derivative[bin]
                = derivative_scale * response.outlet[bin];
            ++objective_bins;
        }
        if (objective_bins == 0) {
            log(LogLevel::Error,
                "The freeform objective needs at least one excited FFT bin.");
            return false;
        }
        objective.pass /= objective_bins;
        for (std::complex<double>& derivative
                : objective.pass_spectrum_derivative) {
            derivative /= objective_bins;
        }
        objective.has_pass = true;
        return true;
    }

    std::vector<const FrequencyBand*> bands;
    bands.reserve(settings.frequencyBands.size());
    bool pass_configured = false;
    bool stop_configured = false;
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
        pass_configured |= band.type == FrequencyBandType::pass;
        stop_configured |= band.type == FrequencyBandType::stop;
        bands.push_back(&band);
    }
    std::sort(bands.begin(), bands.end(),
        [](const FrequencyBand* first, const FrequencyBand* second) {
            if (first->startHz == second->startHz) {
                return first->endHz < second->endHz;
            }
            return first->startHz < second->startHz;
        });

    // TODO: Perhaps allow overlapping bands to add when we add net square error for free form drawing of filters
    for (std::size_t band = 1; band < bands.size(); ++band) {
        if (bands[band]->startHz < bands[band - 1]->endHz) {
            log(LogLevel::Error, "Frequency bands cannot overlap.");
            return false;
        }
    }

    int pass_bins = 0;
    int stop_bins = 0;
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
            ++pass_bins;
        }
        else {
            objective.stop += error;
            objective.has_stop = true;
            objective.stop_spectrum_derivative[bin] = derivative;
            ++stop_bins;
        }
    }

    if ((!pass_configured && !stop_configured)
        || (pass_configured && pass_bins == 0)
        || (stop_configured && stop_bins == 0)) {
        log(LogLevel::Error,
            "Every configured objective group needs at least one valid FFT bin.");
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
    mma_bound.store(std::max(objective.pass, objective.stop));
    log(LogLevel::Message,
        "Initial pass/stop objectives: "
            + std::to_string(objective.pass) + " / "
            + std::to_string(objective.stop) + ".");

    try {
        check_cancelled();
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
        option_error |= options->setOption("output_file", "");
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
        option_error |= options->setOption(
            "mma_use_constraint_linearization", 0);
        option_error |= options->setOption("mma_l1_tol", 1.0e-5);
        option_error |= options->setOption("mma_linfty_tol", 1.0e-5);
        option_error |= options->setOption("mma_infeas_tol", 1.0e-6);
        option_error |= options->setOption("max_major_iters", 100);
        option_error |= options->setOption("abs_res_tol", 1.0e-10);
        option_error |= options->setOption("use_diag_hessian", 1);
        option_error |= options->setOption(
            "starting_point_strategy", "affine_step");
        option_error |= options->setOption(
            "barrier_strategy", "mehrotra");
        option_error |= options->setOption("use_line_search", 0);
        if (option_error != 0) {
            throw std::runtime_error(
                "ParOpt rejected one or more MMA settings.");
        }

        std::unique_ptr<EpigraphMMA, decltype(release)> mma(
            new EpigraphMMA(
                problem.get(), options.get(),
                geometry.activeDesignDofs.Size()),
            release);
        mma->incref();
        std::unique_ptr<ParOptInteriorPoint, decltype(release)> interior_point(
            new ParOptInteriorPoint(mma.get(), options.get()), release);
        interior_point->incref();
        log(LogLevel::Message,
            "Starting ParOpt MMA with "
                + std::to_string(geometry.activeDesignDofs.Size())
                + " active design variables and "
                + std::to_string(settings.maxIterations)
                + " requested iterations.");
        const auto paropt_started_at = std::chrono::steady_clock::now();
        const MMAResult mma_result = runMMA(
            *mma, *interior_point, *options);
        performance_data.paroptSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - paropt_started_at).count();
        check_cancelled();

        if (cancel_requested.load()) {
            status.store(OptimizerStatus::Cancelled);
            log(LogLevel::Warning,
                "Optimization cancelled after the latest completed design.");
            return false;
        }
        ParOptVec* optimized_design = nullptr;
        mma->getOptimizedPoint(&optimized_design);
        if (optimized_design == nullptr || !problem->matches(optimized_design)) {
            throw std::runtime_error(
                "ParOpt returned a design that has not completed its forward analysis.");
        }

        iteration.store(mma_result.iterations);
        status.store(mma_result.converged
            ? OptimizerStatus::Converged
            : OptimizerStatus::MaximumIterations);
        log(LogLevel::Message,
            std::string(mma_result.converged
                ? "ParOpt converged after "
                : "ParOpt reached the requested limit of ")
                + std::to_string(mma_result.iterations)
                + " MMA iterations; KKT l1/linfinity/infeasibility = "
                + std::to_string(mma_result.l1) + " / "
                + std::to_string(mma_result.linfinity) + " / "
                + std::to_string(mma_result.infeasibility) + ".");
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
    const OptimizerStatus current_status = status.load();
    if (current_status != OptimizerStatus::Working) {
        return;
    }
    cancel_requested.store(true);
}

void Optimizer::check_cancelled() const
{
    if (cancel_requested.load()) {
        throw OptimizationCancelled{};
    }
}

void Optimizer::clear_requests()
{
    cancel_requested.store(false);
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
