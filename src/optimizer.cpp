#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IpIpoptApplication.hpp"
#include "IpTNLP.hpp"
#if METAMATERIAL_USE_MPI && !defined(_WIN32)
#include <mpi.h>
#endif

namespace App {

namespace {

struct OptimizationCancelled {
};

const char* ipopt_status_name(Ipopt::ApplicationReturnStatus status)
{
    switch (status) {
        case Ipopt::Solve_Succeeded: return "solve succeeded";
        case Ipopt::Solved_To_Acceptable_Level: return "acceptable solution";
        case Ipopt::Infeasible_Problem_Detected: return "infeasible problem";
        case Ipopt::Search_Direction_Becomes_Too_Small:
            return "search direction became too small";
        case Ipopt::Diverging_Iterates: return "diverging iterates";
        case Ipopt::User_Requested_Stop: return "user requested stop";
        case Ipopt::Feasible_Point_Found: return "feasible point found";
        case Ipopt::Maximum_Iterations_Exceeded: return "maximum iterations";
        case Ipopt::Restoration_Failed: return "restoration failed";
        case Ipopt::Error_In_Step_Computation: return "step computation failed";
        case Ipopt::Maximum_CpuTime_Exceeded: return "maximum CPU time";
        case Ipopt::Maximum_WallTime_Exceeded: return "maximum wall time";
        case Ipopt::Not_Enough_Degrees_Of_Freedom:
            return "not enough degrees of freedom";
        case Ipopt::Invalid_Problem_Definition:
            return "invalid problem definition";
        case Ipopt::Invalid_Option: return "invalid option";
        case Ipopt::Invalid_Number_Detected: return "invalid number detected";
        case Ipopt::Unrecoverable_Exception: return "unrecoverable exception";
        case Ipopt::NonIpopt_Exception_Thrown: return "non-Ipopt exception";
        case Ipopt::Insufficient_Memory: return "insufficient memory";
        case Ipopt::Internal_Error: return "internal error";
    }
    return "unknown status";
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

// Ipopt requires a TNLP callback object. Keep it private to App::Optimizer so
// the application still has one optimizer type and one ownership path.
class Optimizer::Problem final : public Ipopt::TNLP {
public:
    Problem(Optimizer& optimizer, ObjectiveEvaluation initial_objective)
        : optimizer(optimizer),
          evaluated_objective(std::move(initial_objective))
    {
        active_design_size = optimizer.geometry.activeDesignDofs.Size();
        constraint_size = (evaluated_objective.has_pass ? 1 : 0)
            + (evaluated_objective.has_stop ? 1 : 0);
        if (active_design_size == 0 || constraint_size == 0) {
            throw std::runtime_error(
                "Ipopt requires active design variables and at least one objective group.");
        }

        std::vector<unsigned char> active_dofs_seen(
            optimizer.geometry.design.Size(), 0);
        for (int design = 0; design < active_design_size; ++design) {
            const int dof = optimizer.geometry.activeDesignDofs[design];
            if (dof < 0 || dof >= optimizer.geometry.design.Size()
                || active_dofs_seen[dof] != 0) {
                throw std::runtime_error(
                    "The active design DOFs must be unique valid LevelSet entries.");
            }
            const double value = optimizer.geometry.design[dof];
            if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
                throw std::runtime_error(
                    "Every active design value must be finite and lie in [0,1].");
            }
            active_dofs_seen[dof] = 1;
        }

        const double initial_worst = std::max(
            evaluated_objective.has_pass ? evaluated_objective.pass : 0.0,
            evaluated_objective.has_stop ? evaluated_objective.stop : 0.0);
        objective_scale = std::max(1.0, initial_worst);
        const double scaled_worst = initial_worst / objective_scale;
        initial_bound = scaled_worst
            + std::max(1.0e-8, 1.0e-4 * (1.0 + scaled_worst));

        evaluated_design.resize(active_design_size);
        current_iterate.resize(active_design_size + 1);
        final_design.resize(active_design_size + 1);
        for (int design = 0; design < active_design_size; ++design) {
            evaluated_design[design] = optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]];
        }
        accepted_design = evaluated_design;
        accepted_objective = evaluated_objective;
        accepted_bound = initial_worst;
    }

    bool get_nlp_info(
        Ipopt::Index& n,
        Ipopt::Index& m,
        Ipopt::Index& nnz_jac_g,
        Ipopt::Index& nnz_h_lag,
        IndexStyleEnum& index_style) override
    {
        n = active_design_size + 1;
        m = constraint_size;
        nnz_jac_g = n * m;
        nnz_h_lag = 0;
        index_style = C_STYLE;
        return true;
    }

    bool get_bounds_info(
        Ipopt::Index n,
        Ipopt::Number* x_l,
        Ipopt::Number* x_u,
        Ipopt::Index m,
        Ipopt::Number* g_l,
        Ipopt::Number* g_u) override
    {
        if (n != active_design_size + 1 || m != constraint_size) {
            return fail("Ipopt requested inconsistent problem bounds.");
        }
        for (int design = 0; design < active_design_size; ++design) {
            x_l[design] = 0.0;
            x_u[design] = 1.0;
        }
        x_l[active_design_size] = 0.0;
        x_u[active_design_size] = 2.0e19;
        for (int constraint = 0; constraint < constraint_size; ++constraint) {
            g_l[constraint] = 0.0;
            g_u[constraint] = 2.0e19;
        }
        return true;
    }

    bool get_starting_point(
        Ipopt::Index n,
        bool init_x,
        Ipopt::Number* x,
        bool init_z,
        Ipopt::Number*,
        Ipopt::Number*,
        Ipopt::Index m,
        bool init_lambda,
        Ipopt::Number*) override
    {
        if (n != active_design_size + 1 || m != constraint_size
            || !init_x || init_z || init_lambda) {
            return fail("Ipopt requested an unsupported starting point.");
        }
        std::copy(evaluated_design.begin(), evaluated_design.end(), x);
        x[active_design_size] = initial_bound;
        return true;
    }

    bool eval_f(
        Ipopt::Index,
        const Ipopt::Number* x,
        bool,
        Ipopt::Number& objective_value) override
    {
        objective_value = x[active_design_size];
        return std::isfinite(objective_value)
            || fail("Ipopt proposed a non-finite epigraph value.");
    }

    bool eval_grad_f(
        Ipopt::Index n,
        const Ipopt::Number*,
        bool,
        Ipopt::Number* gradient) override
    {
        std::fill_n(gradient, n, 0.0);
        gradient[active_design_size] = 1.0;
        return true;
    }

    bool eval_g(
        Ipopt::Index,
        const Ipopt::Number* x,
        bool,
        Ipopt::Index,
        Ipopt::Number* constraints) override
    {
        if (!ensure_values(x)) {
            return false;
        }

        int constraint = 0;
        const double bound = x[active_design_size];
        if (evaluated_objective.has_pass) {
            constraints[constraint++] = bound
                - evaluated_objective.pass / objective_scale;
        }
        if (evaluated_objective.has_stop) {
            constraints[constraint++] = bound
                - evaluated_objective.stop / objective_scale;
        }
        return true;
    }

    bool eval_jac_g(
        Ipopt::Index n,
        const Ipopt::Number* x,
        bool,
        Ipopt::Index m,
        Ipopt::Index,
        Ipopt::Index* rows,
        Ipopt::Index* columns,
        Ipopt::Number* values) override
    {
        if (values == nullptr) {
            int entry = 0;
            for (int constraint = 0; constraint < m; ++constraint) {
                for (int variable = 0; variable < n; ++variable) {
                    rows[entry] = constraint;
                    columns[entry] = variable;
                    ++entry;
                }
            }
            return true;
        }

        if (!ensure_values(x) || !ensure_gradients()) {
            return false;
        }
        int entry = 0;
        if (evaluated_objective.has_pass) {
            fill_constraint_gradient(pass_gradient, values, entry);
        }
        if (evaluated_objective.has_stop) {
            fill_constraint_gradient(stop_gradient, values, entry);
        }
        return true;
    }

    bool eval_h(
        Ipopt::Index,
        const Ipopt::Number*,
        bool,
        Ipopt::Number,
        Ipopt::Index,
        const Ipopt::Number*,
        bool,
        Ipopt::Index nonzeros,
        Ipopt::Index*,
        Ipopt::Index*,
        Ipopt::Number*) override
    {
        return nonzeros == 0;
    }

    Ipopt::Index get_number_of_nonlinear_variables() override
    {
        return active_design_size;
    }

    bool get_list_of_nonlinear_variables(
        Ipopt::Index nonlinear_variables,
        Ipopt::Index* positions) override
    {
        if (nonlinear_variables != active_design_size) {
            return fail("Ipopt requested an inconsistent nonlinear variable list.");
        }
        for (int design = 0; design < active_design_size; ++design) {
            positions[design] = design;
        }
        return true;
    }

    bool intermediate_callback(
        Ipopt::AlgorithmMode,
        Ipopt::Index current_iteration,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Number,
        Ipopt::Index,
        const Ipopt::IpoptData* data,
        Ipopt::IpoptCalculatedQuantities* quantities) override
    {
        if (optimizer.cancel_requested.load()) {
            cancellation_observed = true;
            return false;
        }

        if (!get_curr_iterate(
                data, quantities, false,
                static_cast<Ipopt::Index>(current_iterate.size()),
                current_iterate.data(),
                nullptr, nullptr, 0, nullptr, nullptr)) {
            return fail("Ipopt could not expose its accepted iterate.");
        }
        return accept(
            current_iterate.data(), static_cast<int>(current_iteration));
    }

    void finalize_solution(
        Ipopt::SolverReturn,
        Ipopt::Index n,
        const Ipopt::Number* x,
        const Ipopt::Number*,
        const Ipopt::Number*,
        Ipopt::Index,
        const Ipopt::Number*,
        const Ipopt::Number*,
        Ipopt::Number,
        const Ipopt::IpoptData*,
        Ipopt::IpoptCalculatedQuantities*) override
    {
        if (x != nullptr && n == active_design_size + 1) {
            std::copy_n(x, n, final_design.begin());
            final_design_ready = true;
        }
    }

    bool accept_final()
    {
        if (!final_design_ready) {
            return false;
        }
        const bool final_design_changed = !matches(final_design.data());
        if (!accept(final_design.data(), optimizer.iteration.load())) {
            return false;
        }
        if (final_design_changed) {
            optimizer.log(LogLevel::Message,
                "Completed optimizer iteration "
                    + std::to_string(optimizer.iteration.load())
                    + ": pass/stop/bound = "
                    + std::to_string(accepted_objective.pass) + " / "
                    + std::to_string(accepted_objective.stop) + " / "
                    + std::to_string(accepted_bound) + ".");
        }
        return true;
    }

    bool check_derivatives(double relative_step, double tolerance)
    {
        if (!std::isfinite(relative_step) || relative_step <= 0.0
            || !std::isfinite(tolerance) || tolerance <= 0.0) {
            return fail("The derivative-check step and tolerance must be positive.");
        }

        const int variables = active_design_size + 1;
        std::vector<Ipopt::Number> point(variables);
        if (!get_starting_point(
                variables, true, point.data(), false, nullptr, nullptr,
                constraint_size, false, nullptr)) {
            return false;
        }
        const std::vector<Ipopt::Number> original = point;
        std::vector<Ipopt::Number> constraints(constraint_size);
        std::vector<Ipopt::Number> upper_constraints(constraint_size);
        std::vector<Ipopt::Number> lower_constraints(constraint_size);
        std::vector<Ipopt::Number> jacobian(
            static_cast<std::size_t>(variables) * constraint_size);

        if (!eval_g(variables, point.data(), true, constraint_size,
                    constraints.data())
            || !eval_jac_g(
                variables, point.data(), false, constraint_size,
                static_cast<Ipopt::Index>(jacobian.size()),
                nullptr, nullptr, jacobian.data())) {
            return false;
        }

        const int cached_forwards = optimizer.performance_data.forwardCallbacks;
        const int cached_gradients = optimizer.performance_data.gradientCallbacks;
        if (!eval_g(variables, point.data(), false, constraint_size,
                    constraints.data())
            || !eval_jac_g(
                variables, point.data(), false, constraint_size,
                static_cast<Ipopt::Index>(jacobian.size()),
                nullptr, nullptr, jacobian.data())
            || optimizer.performance_data.forwardCallbacks != cached_forwards
            || optimizer.performance_data.gradientCallbacks != cached_gradients) {
            return fail(
                "Repeated TNLP callbacks recomputed unchanged physics.");
        }

        const double original_bound = point[active_design_size];
        const double bound_step = relative_step
            * std::max(1.0, std::abs(original_bound));
        point[active_design_size] = original_bound + bound_step;
        if (!eval_g(variables, point.data(), true, constraint_size,
                    upper_constraints.data())) {
            return false;
        }
        point[active_design_size] = std::max(0.0, original_bound - bound_step);
        if (!eval_g(variables, point.data(), true, constraint_size,
                    lower_constraints.data())) {
            return false;
        }
        const double bound_denominator = original_bound + bound_step
            - point[active_design_size];
        for (int constraint = 0; constraint < constraint_size; ++constraint) {
            const double finite_difference =
                (upper_constraints[constraint] - lower_constraints[constraint])
                / bound_denominator;
            const double analytic = jacobian[
                constraint * variables + active_design_size];
            if (std::abs(finite_difference - analytic)
                > tolerance * std::max({1.0, std::abs(finite_difference),
                                        std::abs(analytic)})) {
                return fail(
                    "The epigraph-column derivative check failed for constraint "
                    + std::to_string(constraint) + ": analytic="
                    + std::to_string(analytic) + ", finite difference="
                    + std::to_string(finite_difference) + ".");
            }
        }
        if (optimizer.performance_data.forwardCallbacks != cached_forwards
            || optimizer.performance_data.gradientCallbacks != cached_gradients) {
            return fail("Changing only the epigraph variable recomputed physics.");
        }
        point[active_design_size] = original_bound;

        bool derivatives_match = true;
        for (int design = 0; design < active_design_size && derivatives_match;
             ++design) {
            const double value = original[design];
            const double step = relative_step * std::max(1.0, std::abs(value));
            const double upper = std::min(1.0, value + step);
            const double lower = std::max(0.0, value - step);
            if (upper == lower) {
                derivatives_match = false;
                fail("A design variable has no finite-difference interval.");
                break;
            }

            point[design] = upper;
            if (!eval_g(variables, point.data(), true, constraint_size,
                        upper_constraints.data())) {
                derivatives_match = false;
                break;
            }
            point[design] = lower;
            if (!eval_g(variables, point.data(), true, constraint_size,
                        lower_constraints.data())) {
                derivatives_match = false;
                break;
            }
            point[design] = value;

            for (int constraint = 0;
                 constraint < constraint_size; ++constraint) {
                const double finite_difference =
                    (upper_constraints[constraint]
                     - lower_constraints[constraint]) / (upper - lower);
                const double analytic = jacobian[
                    constraint * variables + design];
                if (std::abs(finite_difference - analytic)
                    > tolerance * std::max({1.0, std::abs(finite_difference),
                                            std::abs(analytic)})) {
                    derivatives_match = false;
                    fail(
                        "The design-column derivative check failed for variable "
                        + std::to_string(design) + ", constraint "
                        + std::to_string(constraint) + ": analytic="
                        + std::to_string(analytic) + ", finite difference="
                        + std::to_string(finite_difference) + ".");
                    break;
                }
            }
        }

        if (!ensure_values(original.data())) {
            return false;
        }
        return derivatives_match;
    }

    bool restore_accepted()
    {
        if (accepted_design.empty()) {
            return false;
        }
        if (evaluated_design != accepted_design) {
            apply_design(accepted_design.data());
            try {
                if (!optimizer.solver.assembleSolutionSpace()
                    || !optimizer.solver.solve()) {
                    return fail("Could not restore the last accepted design.");
                }
            }
            catch (const std::exception& error) {
                return fail(std::string("Could not restore the last accepted design: ")
                    + error.what());
            }
            catch (...) {
                return fail("Could not restore the last accepted design.");
            }
            evaluated_design = accepted_design;
            evaluated_objective = accepted_objective;
            gradients_ready = false;
        }
        publish(accepted_bound, optimizer.iteration.load(), false);
        return true;
    }

    bool cancelled() const { return cancellation_observed; }
    const std::string& error() const { return callback_error; }

private:
    bool ensure_values(const Ipopt::Number* variables)
    {
        if (matches(variables)) {
            return true;
        }
        for (int design = 0; design < active_design_size; ++design) {
            if (!std::isfinite(variables[design])) {
                return fail("Ipopt proposed a non-finite design variable.");
            }
        }

        const auto started_at = std::chrono::steady_clock::now();
        ObjectiveEvaluation next_objective;
        try {
            apply_design(variables);
            ++optimizer.performance_data.forwardCallbacks;
            if (!optimizer.solver.assembleSolutionSpace()
                || !optimizer.solver.solve()) {
                throw std::runtime_error(
                    "The forward analysis failed inside Ipopt.");
            }
            if (!optimizer.evaluateObjectives(next_objective)) {
                throw std::runtime_error(
                    "The FFT objective could not be evaluated.");
            }
        }
        catch (const std::exception& error) {
            callback_error = error.what();
            return restore_after_callback_failure();
        }
        catch (...) {
            callback_error = "The Ipopt forward callback failed with an unknown error.";
            return restore_after_callback_failure();
        }

        evaluated_objective = std::move(next_objective);
        for (int design = 0; design < active_design_size; ++design) {
            evaluated_design[design] = optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]];
        }
        gradients_ready = false;
        optimizer.performance_data.forwardCallbackSeconds +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
        return true;
    }

    bool ensure_gradients()
    {
        if (gradients_ready) {
            return true;
        }
        const auto started_at = std::chrono::steady_clock::now();
        static const std::vector<std::complex<double>> no_derivative;
        try {
            ++optimizer.performance_data.gradientCallbacks;
            if (!optimizer.solver.differentiateFrequencyResponses(
                    evaluated_objective.has_pass
                        ? evaluated_objective.pass_spectrum_derivative : no_derivative,
                    evaluated_objective.has_stop
                        ? evaluated_objective.stop_spectrum_derivative : no_derivative,
                    pass_gradient,
                    stop_gradient)) {
                return fail("The pass/stop discrete adjoint failed.");
            }
        }
        catch (const std::exception& error) {
            return fail(std::string("The pass/stop discrete adjoint failed: ")
                + error.what());
        }
        catch (...) {
            return fail("The pass/stop discrete adjoint failed.");
        }

        for (int design = 0; design < active_design_size; ++design) {
            const int dof = optimizer.geometry.activeDesignDofs[design];
            if ((evaluated_objective.has_pass
                    && (dof >= pass_gradient.Size()
                        || !std::isfinite(pass_gradient[dof])))
                || (evaluated_objective.has_stop
                    && (dof >= stop_gradient.Size()
                        || !std::isfinite(stop_gradient[dof])))) {
                return fail(
                    "The discrete adjoint produced a non-finite design gradient.");
            }
        }
        optimizer.performance_data.gradientCallbackSeconds +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();
        gradients_ready = true;
        return true;
    }

    bool restore_after_callback_failure()
    {
        try {
            apply_design(accepted_design.data());
            if (!optimizer.solver.assembleSolutionSpace()
                || !optimizer.solver.solve()) {
                callback_error +=
                    " The last completed design could not be restored.";
                return false;
            }
        }
        catch (...) {
            callback_error += " The last completed design could not be restored.";
            return false;
        }
        evaluated_design = accepted_design;
        evaluated_objective = accepted_objective;
        gradients_ready = false;
        return false;
    }

    bool accept(const Ipopt::Number* variables, int current_iteration)
    {
        if (!ensure_values(variables)) {
            return false;
        }
        accepted_design = evaluated_design;
        accepted_objective = evaluated_objective;
        accepted_bound = variables[active_design_size] * objective_scale;
        publish(accepted_bound, current_iteration, true);
        return true;
    }

    void publish(double bound, int current_iteration, bool write_log)
    {
        optimizer.iteration.store(current_iteration);
        optimizer.pass_objective.store(accepted_objective.pass);
        optimizer.stop_objective.store(accepted_objective.stop);
        optimizer.epigraph_bound.store(bound);
        if (write_log && current_iteration > published_iteration) {
            published_iteration = current_iteration;
            optimizer.log(LogLevel::Message,
                "Completed optimizer iteration "
                    + std::to_string(current_iteration)
                    + ": pass/stop/bound = "
                    + std::to_string(accepted_objective.pass) + " / "
                    + std::to_string(accepted_objective.stop) + " / "
                    + std::to_string(bound) + ".");
        }
    }

    void fill_constraint_gradient(
        const mfem::Vector& design_gradient,
        Ipopt::Number* values,
        int& entry) const
    {
        for (int design = 0; design < active_design_size; ++design) {
            values[entry++] = -design_gradient[
                optimizer.geometry.activeDesignDofs[design]] / objective_scale;
        }
        values[entry++] = 1.0;
    }

    bool matches(const Ipopt::Number* variables) const
    {
        for (int design = 0; design < active_design_size; ++design) {
            if (variables[design] != evaluated_design[design]) {
                return false;
            }
        }
        return true;
    }

    void apply_design(const Ipopt::Number* variables)
    {
        for (int design = 0; design < active_design_size; ++design) {
            optimizer.geometry.design[
                optimizer.geometry.activeDesignDofs[design]] =
                    std::clamp(static_cast<double>(variables[design]), 0.0, 1.0);
        }
        optimizer.geometry.enforceDesignConstraints();
    }

    bool fail(std::string message)
    {
        if (callback_error.empty()) {
            callback_error = std::move(message);
        }
        return false;
    }

    Optimizer& optimizer;
    ObjectiveEvaluation evaluated_objective;
    ObjectiveEvaluation accepted_objective;
    mfem::Vector pass_gradient;
    mfem::Vector stop_gradient;
    std::vector<double> evaluated_design;
    std::vector<double> accepted_design;
    std::vector<Ipopt::Number> current_iterate;
    std::vector<double> final_design;
    std::string callback_error;
    double objective_scale = 1.0;
    double initial_bound = 1.0;
    double accepted_bound = 0.0;
    int active_design_size = 0;
    int constraint_size = 0;
    int published_iteration = 0;
    bool gradients_ready = false;
    bool cancellation_observed = false;
    bool final_design_ready = false;
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
    epigraph_bound.store(0.0);
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
    for (std::size_t bin = 0; bin < bin_count; ++bin) {
        if (!std::isfinite(response.frequency[bin])
            || (bin > 0
                && response.frequency[bin] <= response.frequency[bin - 1])) {
            log(LogLevel::Error,
                "The solver frequency bins must be finite and increasing.");
            return false;
        }
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
        for (std::size_t sample = 0;
             sample < freeform.frequencyHz.size(); ++sample) {
            if (!std::isfinite(freeform.frequencyHz[sample])
                || !std::isfinite(freeform.targetTransmission[sample])
                || freeform.targetTransmission[sample] <= 0.0
                || (sample > 0
                    && freeform.frequencyHz[sample]
                        <= freeform.frequencyHz[sample - 1])) {
                log(LogLevel::Error,
                    "Freeform objective samples must be finite, positive, and increasing.");
                return false;
            }
        }

        constexpr double target_floor = 1.0e-6;
        int objective_bins = 0;
        for (std::size_t bin = 0; bin < bin_count; ++bin) {
            if (response.valid[bin] == 0 || freeform.frequencyHz.empty()) {
                continue;
            }
            const auto found = std::lower_bound(
                freeform.frequencyHz.begin(), freeform.frequencyHz.end(),
                response.frequency[bin]);
            std::size_t sample = found == freeform.frequencyHz.end()
                ? freeform.frequencyHz.size() - 1
                : static_cast<std::size_t>(found - freeform.frequencyHz.begin());
            if (sample > 0
                && std::abs(freeform.frequencyHz[sample - 1]
                            - response.frequency[bin])
                    < std::abs(freeform.frequencyHz[sample]
                               - response.frequency[bin])) {
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
            if (!std::isfinite(objective.pass)
                || !std::isfinite(
                    objective.pass_spectrum_derivative[bin].real())
                || !std::isfinite(
                    objective.pass_spectrum_derivative[bin].imag())) {
                log(LogLevel::Error,
                    "A freeform objective bin overflowed its value or derivative.");
                return false;
            }
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
        if (!std::isfinite(error)
            || !std::isfinite(derivative.real())
            || !std::isfinite(derivative.imag())) {
            log(LogLevel::Error,
                "A frequency-band objective bin overflowed its value or derivative.");
            return false;
        }

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
        if (!std::isfinite(objective.pass)
            || !std::isfinite(objective.stop)) {
            log(LogLevel::Error,
                "The frequency-band objective overflowed while accumulating bins.");
            return false;
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
        || !std::isfinite(settings.convergenceTolerance)
        || !std::isfinite(settings.acceptableTolerance)
        || settings.convergenceTolerance <= 0.0
        || settings.acceptableTolerance < settings.convergenceTolerance) {
        log(LogLevel::Error, "The optimizer settings are invalid.");
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
    epigraph_bound.store(std::max(objective.pass, objective.stop));
    log(LogLevel::Message,
        "Initial pass/stop objectives: "
            + std::to_string(objective.pass) + " / "
            + std::to_string(objective.stop) + ".");

    try {
        check_cancelled();
        Ipopt::SmartPtr<Problem> problem =
            new Problem(*this, std::move(objective));
        Ipopt::SmartPtr<Ipopt::IpoptApplication> application =
            IpoptApplicationFactory();
        if (!Ipopt::IsValid(application)) {
            throw std::runtime_error("Could not create the Ipopt application.");
        }

        bool options_valid = true;
        options_valid &= application->Options()->SetStringValue(
            "hessian_approximation", "limited-memory");
        options_valid &= application->Options()->SetStringValue(
            "grad_f_constant", "yes");
        options_valid &= application->Options()->SetNumericValue(
            "bound_relax_factor", 0.0);
        options_valid &= application->Options()->SetStringValue(
            "honor_original_bounds", "yes");
        options_valid &= application->Options()->SetIntegerValue(
            "max_iter", settings.maxIterations);
        options_valid &= application->Options()->SetNumericValue(
            "tol", settings.convergenceTolerance);
        options_valid &= application->Options()->SetNumericValue(
            "acceptable_tol", settings.acceptableTolerance);
        options_valid &= application->Options()->SetIntegerValue(
            "print_level", 0);
        options_valid &= application->Options()->SetStringValue("sb", "yes");
#if METAMATERIAL_USE_MPI && !defined(_WIN32)
        options_valid &= application->Options()->SetIntegerValue(
            "mumps_mpi_communicator",
            static_cast<Ipopt::Index>(MPI_Comm_c2f(MPI_COMM_SELF)));
#endif
        if (!options_valid) {
            throw std::runtime_error("Ipopt rejected an optimizer option.");
        }

        const Ipopt::ApplicationReturnStatus initialize_status =
            application->Initialize("");
        if (initialize_status != Ipopt::Solve_Succeeded) {
            throw std::runtime_error(
                std::string("Ipopt initialization failed: ")
                + ipopt_status_name(initialize_status) + ".");
        }

        log(LogLevel::Message,
            "Starting Ipopt with "
                + std::to_string(geometry.activeDesignDofs.Size())
                + " active design variables and "
                + std::to_string(settings.maxIterations)
                + " requested iterations.");
        const auto started_at = std::chrono::steady_clock::now();
        Ipopt::SmartPtr<Ipopt::TNLP> nlp = problem;
        const Ipopt::ApplicationReturnStatus solve_status =
            application->OptimizeTNLP(nlp);
        performance_data.optimizerSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at).count();

        if (cancel_requested.load() || problem->cancelled()) {
            if (!problem->restore_accepted()) {
                status.store(OptimizerStatus::Error);
                log(LogLevel::Error,
                    problem->error().empty()
                        ? "Could not restore the last accepted design after cancellation."
                        : problem->error());
                return false;
            }
            status.store(OptimizerStatus::Cancelled);
            log(LogLevel::Warning,
                "Optimization cancelled after the latest accepted design.");
            return false;
        }

        const bool completed = solve_status == Ipopt::Solve_Succeeded
            || solve_status == Ipopt::Solved_To_Acceptable_Level
            || solve_status == Ipopt::Maximum_Iterations_Exceeded;
        if (completed && problem->accept_final()) {
            status.store(solve_status == Ipopt::Maximum_Iterations_Exceeded
                ? OptimizerStatus::MaximumIterations
                : OptimizerStatus::Converged);
            log(LogLevel::Message,
                std::string("Ipopt finished with ")
                    + ipopt_status_name(solve_status) + " after "
                    + std::to_string(iteration.load()) + " iterations.");
            return true;
        }

        problem->restore_accepted();
        status.store(solve_status == Ipopt::Diverging_Iterates
            ? OptimizerStatus::Diverged
            : OptimizerStatus::Error);
        std::string message = std::string("Ipopt optimization failed: ")
            + ipopt_status_name(solve_status) + ".";
        if (!problem->error().empty()) {
            message += " " + problem->error();
        }
        log(LogLevel::Error, std::move(message));
        return false;
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
            "Ipopt optimization failed: " + std::string(error.what()));
        return false;
    }
}

bool Optimizer::check_derivatives(double relative_step, double tolerance)
{
    if (status.load() == OptimizerStatus::Working) {
        log(LogLevel::Error,
            "The optimizer derivative check cannot run during optimization.");
        return false;
    }
    performance_data = {};
    try {
        if (!solver.setMesh()
            || !solver.assembleSolutionSpace()
            || !solver.solve()) {
            log(LogLevel::Error,
                "The derivative check could not complete its initial forward solve.");
            return false;
        }

        ObjectiveEvaluation objective;
        if (!evaluateObjectives(objective)) {
            return false;
        }
        Ipopt::SmartPtr<Problem> problem =
            new Problem(*this, std::move(objective));
        if (!problem->check_derivatives(relative_step, tolerance)) {
            log(LogLevel::Error,
                problem->error().empty()
                    ? "The optimizer derivative check failed."
                    : problem->error());
            return false;
        }
        log(LogLevel::Message, "The optimizer derivative check passed.");
        return true;
    }
    catch (const std::exception& error) {
        log(LogLevel::Error,
            "The optimizer derivative check failed: "
                + std::string(error.what()));
        return false;
    }
    catch (...) {
        log(LogLevel::Error,
            "The optimizer derivative check failed with an unknown error.");
        return false;
    }
}

void Optimizer::request_cancel()
{
    if (status.load() == OptimizerStatus::Working) {
        cancel_requested.store(true);
    }
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

double Optimizer::get_epigraph_bound() const
{
    return epigraph_bound.load();
}

const OptimizerPerformance& Optimizer::performance() const
{
    return performance_data;
}

} // namespace App
