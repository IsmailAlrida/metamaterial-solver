#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "solver.hpp"

namespace App::detail {

struct LinearSolveResult {
    bool converged = true;
    int iterations = 0;
    double residual = 0.0;
};

using LinearSolve = std::function<LinearSolveResult(
    const mfem::Vector&, mfem::Vector&)>;
using GlobalNorm = std::function<double(const mfem::Vector&)>;
using GlobalDot = std::function<double(
    const mfem::Vector&, const mfem::Vector&)>;

inline bool runNewmark(
    const SolverSettings& settings,
    const VibroacousticSettings& physics,
    const mfem::Operator& M,
    const mfem::Operator& C,
    const mfem::Operator& K,
    const mfem::Operator& initial_matrix,
    const mfem::Operator& effective_matrix,
    const mfem::Array<int>& essential_dofs,
    const mfem::Vector& inlet_load,
    const mfem::Vector& outlet_functional,
    const std::vector<double>& source_pressure_derivative,
    const LinearSolve& solve_initial,
    const LinearSolve& solve_effective,
    const GlobalNorm& norm,
    const GlobalDot& dot,
    bool retain_history,
    std::vector<mfem::Vector>& history,
    std::vector<NewmarkResidualNorms>& residual_norms,
    std::vector<double>& measured_outlet,
    SolverPerformance& performance,
    const LogFunction& log,
    const char* analysis_name)
{
    const double beta = settings.newmarkBeta;
    const double gamma = settings.newmarkGamma;
    const double a_1 = 1.0 - gamma / beta;
    const double a_2 =
        (1.0 - gamma / (2.0 * beta)) * settings.dt;
    const double a_3 = gamma / (beta * settings.dt);
    const double a_4 = 1.0 / (beta * settings.dt);
    const double a_5 = 1.0 / (2.0 * beta) - 1.0;
    const double a_6 = 1.0 / (beta * settings.dt * settings.dt);
    const int state_size = M.Height();
    const int time_steps = static_cast<int>(source_pressure_derivative.size()) - 1;
    const double load_scale = 2.0 / (physics.rho_a * physics.c_a);

    mfem::Vector v(state_size);
    mfem::Vector v_dot(state_size);
    mfem::Vector v_ddot(state_size);
    mfem::Vector h(state_size);
    v = 0.0;
    v_dot = 0.0;
    v_ddot = 0.0;
    h = 0.0;
    h.Add(load_scale * source_pressure_derivative[0], inlet_load);
    h.SetSubVector(essential_dofs, 0.0);

    const LinearSolveResult initial = solve_initial(h, v_ddot);
    performance.forwardFgmresIterations += initial.iterations;
    ++performance.forwardFgmresSolves;
    performance.maximumForwardFgmresIterations = std::max(
        performance.maximumForwardFgmresIterations, initial.iterations);
    if (!initial.converged) {
        std::ostringstream message;
        message << "The initial-acceleration solve did not converge: "
                << initial.iterations << " iterations, relative residual "
                << std::scientific << initial.residual << ".";
        log(LogLevel::Error, message.str());
        return false;
    }
    v_ddot.SetSubVector(essential_dofs, 0.0);

    mfem::Vector residual(state_size);
    initial_matrix.Mult(v_ddot, residual);
    residual -= h;
    const double initial_residual = norm(residual) / std::max(1.0, norm(h));
    if (!std::isfinite(initial_residual) || initial_residual > 1.0e-9) {
        std::ostringstream message;
        message << "The initial-acceleration residual gate failed: "
                << std::scientific << initial_residual
                << " (limit 1.000000e-09).";
        log(LogLevel::Error, message.str());
        return false;
    }

    if (retain_history) {
        history.resize(time_steps + 1);
        residual_norms.resize(time_steps + 1);
        for (mfem::Vector& state : history) {
            state.SetSize(3 * state_size);
        }
        history[0].SetVector(v, 0);
        history[0].SetVector(v_dot, state_size);
        history[0].SetVector(v_ddot, 2 * state_size);
        residual_norms[0] = {initial_residual, 0.0, 0.0};
    }

    measured_outlet.clear();
    measured_outlet.reserve(time_steps);
    // Paper Eq. (29) transforms U^0, ..., U^(N-1).
    measured_outlet.push_back(dot(outlet_functional, v));
    mfem::Vector h_hat(state_size);
    mfem::Vector x_M(state_size);
    mfem::Vector x_C(state_size);
    mfem::Vector y_M(state_size);
    mfem::Vector y_C(state_size);
    mfem::Vector v_n(state_size);
    mfem::Vector v_dot_n(state_size);
    mfem::Vector v_ddot_n(state_size);
    mfem::Vector delta_v(state_size);
    mfem::Vector K_v(state_size);
    mfem::Vector R_1(state_size);
    mfem::Vector R_2(state_size);
    mfem::Vector R_3(state_size);
    double maximum_linear_residual = initial_residual;
    double maximum_equilibrium_residual = initial_residual;
    double maximum_velocity_residual = 0.0;
    double maximum_acceleration_residual = 0.0;
    double maximum_solver_residual = initial.residual;
    int maximum_iterations = initial.iterations;

    for (int n = 1; n <= time_steps; ++n) {
        h = 0.0;
        h.Add(load_scale * source_pressure_derivative[n], inlet_load);

        x_M = 0.0;
        x_M.Add(a_4, v_dot);
        x_M.Add(a_5, v_ddot);
        x_M.Add(a_6, v);
        M.Mult(x_M, y_M);

        x_C = 0.0;
        x_C.Add(-a_1, v_dot);
        x_C.Add(-a_2, v_ddot);
        x_C.Add(a_3, v);
        C.Mult(x_C, y_C);

        h_hat = h;
        h_hat += y_M;
        h_hat += y_C;
        h_hat.SetSubVector(essential_dofs, 0.0);

        v_n = v;
        const LinearSolveResult step = solve_effective(h_hat, v_n);
        performance.forwardFgmresIterations += step.iterations;
        ++performance.forwardFgmresSolves;
        performance.maximumForwardFgmresIterations = std::max(
            performance.maximumForwardFgmresIterations, step.iterations);
        if (!step.converged) {
            std::ostringstream message;
            message << "The Newmark linear solve failed at time step " << n
                    << ": " << step.iterations
                    << " iterations, relative residual " << std::scientific
                    << step.residual << ".";
            log(LogLevel::Error, message.str());
            return false;
        }
        v_n.SetSubVector(essential_dofs, 0.0);

        effective_matrix.Mult(v_n, residual);
        residual -= h_hat;
        const double linear_residual =
            norm(residual) / std::max(1.0, norm(h_hat));

        delta_v = v_n;
        delta_v -= v;
        v_dot_n = 0.0;
        v_dot_n.Add(a_1, v_dot);
        v_dot_n.Add(a_2, v_ddot);
        v_dot_n.Add(a_3, delta_v);
        v_ddot_n = 0.0;
        v_ddot_n.Add(-a_4, v_dot);
        v_ddot_n.Add(-a_5, v_ddot);
        v_ddot_n.Add(a_6, delta_v);
        v_dot_n.SetSubVector(essential_dofs, 0.0);
        v_ddot_n.SetSubVector(essential_dofs, 0.0);

        if (n < time_steps) {
            measured_outlet.push_back(dot(outlet_functional, v_n));
        }

        M.Mult(v_ddot_n, y_M);
        C.Mult(v_dot_n, y_C);
        K.Mult(v_n, K_v);
        R_1 = y_M;
        R_1 += y_C;
        R_1 += K_v;
        R_1 -= h;
        R_1.SetSubVector(essential_dofs, 0.0);

        R_2 = v_dot_n;
        R_2.Add(-a_1, v_dot);
        R_2.Add(-a_2, v_ddot);
        R_2.Add(-a_3, delta_v);
        R_3 = v_ddot_n;
        R_3.Add(a_4, v_dot);
        R_3.Add(a_5, v_ddot);
        R_3.Add(-a_6, delta_v);

        const double R_1_norm = norm(R_1) / std::max({
            1.0, norm(h), norm(y_M) + norm(y_C) + norm(K_v)});
        const double R_2_norm = norm(R_2) / std::max({
            1.0,
            norm(v_dot_n),
            std::abs(a_1) * norm(v_dot)
                + std::abs(a_2) * norm(v_ddot)
                + std::abs(a_3) * norm(delta_v)});
        const double R_3_norm = norm(R_3) / std::max({
            1.0,
            norm(v_ddot_n),
            std::abs(a_4) * norm(v_dot)
                + std::abs(a_5) * norm(v_ddot)
                + std::abs(a_6) * norm(delta_v)});
        if (!std::isfinite(linear_residual)
            || !std::isfinite(R_1_norm)
            || !std::isfinite(R_2_norm)
            || !std::isfinite(R_3_norm)
            || linear_residual > 1.0e-9
            || R_1_norm > 1.0e-9
            || R_2_norm > 1.0e-9
            || R_3_norm > 1.0e-9) {
            std::ostringstream message;
            message << "The Newmark residual gate failed at time step " << n
                    << ": linear=" << std::scientific << linear_residual
                    << ", equilibrium=" << R_1_norm
                    << ", velocity=" << R_2_norm
                    << ", acceleration=" << R_3_norm
                    << " (limit 1.000000e-09).";
            log(LogLevel::Error, message.str());
            return false;
        }

        maximum_linear_residual = std::max(
            maximum_linear_residual, linear_residual);
        maximum_equilibrium_residual = std::max(
            maximum_equilibrium_residual, R_1_norm);
        maximum_velocity_residual = std::max(
            maximum_velocity_residual, R_2_norm);
        maximum_acceleration_residual = std::max(
            maximum_acceleration_residual, R_3_norm);
        maximum_solver_residual = std::max(
            maximum_solver_residual, step.residual);
        maximum_iterations = std::max(maximum_iterations, step.iterations);

        if (retain_history) {
            history[n].SetVector(v_n, 0);
            history[n].SetVector(v_dot_n, state_size);
            history[n].SetVector(v_ddot_n, 2 * state_size);
            residual_norms[n] = {R_1_norm, R_2_norm, R_3_norm};
        }
        v = v_n;
        v_dot = v_dot_n;
        v_ddot = v_ddot_n;
    }

    std::ostringstream summary;
    summary << "Completed the " << analysis_name
            << " transient: maximum linear/equilibrium/velocity/acceleration "
            << "residuals = " << std::scientific
            << maximum_linear_residual << " / "
            << maximum_equilibrium_residual << " / "
            << maximum_velocity_residual << " / "
            << maximum_acceleration_residual
            << ", maximum solver residual = " << maximum_solver_residual
            << ", maximum iterations = " << maximum_iterations << ".";
    log(LogLevel::Message, summary.str());
    performance.maximumForwardResidual = maximum_linear_residual;
    return true;
}

} // namespace App::detail
