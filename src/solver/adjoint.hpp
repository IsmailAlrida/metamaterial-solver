#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "fftw3.h"

#include "newmark.hpp"

namespace App::detail {

inline bool forwardWindowedSignal(
    const std::vector<double>& signal,
    const std::vector<double>& window,
    std::vector<std::complex<double>>& spectrum)
{
    if (signal.empty() || signal.size() != window.size()) {
        return false;
    }
    std::vector<double> windowed(signal.size());
    for (std::size_t sample = 0; sample < signal.size(); ++sample) {
        windowed[sample] = signal[sample] * window[sample];
    }

    fftw_complex* output = fftw_alloc_complex(signal.size() / 2 + 1);
    if (output == nullptr) {
        return false;
    }
    fftw_plan plan = fftw_plan_dft_r2c_1d(
        static_cast<int>(signal.size()), windowed.data(), output, FFTW_ESTIMATE);
    if (plan == nullptr) {
        fftw_free(output);
        return false;
    }
    fftw_execute(plan);
    spectrum.resize(signal.size() / 2 + 1);
    for (std::size_t bin = 0; bin < spectrum.size(); ++bin) {
        spectrum[bin] = {output[bin][0], output[bin][1]};
    }
    fftw_destroy_plan(plan);
    fftw_free(output);
    return true;
}

inline bool inverseOutletDerivative(
    const std::vector<std::complex<double>>& spectrum,
    int time_steps,
    const std::vector<double>& window,
    std::vector<double>& outlet_derivative)
{
    if (spectrum.empty()) {
        outlet_derivative.clear();
        return true;
    }
    fftw_complex* fft_derivative = fftw_alloc_complex(spectrum.size());
    if (fft_derivative == nullptr) {
        return false;
    }
    const bool has_nyquist_bin = time_steps % 2 == 0;
    for (int bin = 0; bin < static_cast<int>(spectrum.size()); ++bin) {
        const bool single_sided = bin == 0
            || (has_nyquist_bin
                && bin == static_cast<int>(spectrum.size()) - 1);
        const double scale = single_sided ? 1.0 : 0.5;
        fft_derivative[bin][0] = scale * spectrum[bin].real();
        fft_derivative[bin][1] = single_sided
            ? 0.0 : scale * spectrum[bin].imag();
    }

    outlet_derivative.resize(time_steps);
    fftw_plan plan = fftw_plan_dft_c2r_1d(
        time_steps,
        fft_derivative,
        outlet_derivative.data(),
        FFTW_ESTIMATE);
    if (plan == nullptr) {
        fftw_free(fft_derivative);
        return false;
    }
    fftw_execute(plan);
    fftw_destroy_plan(plan);
    fftw_free(fft_derivative);
    for (int sample = 0; sample < time_steps; ++sample) {
        outlet_derivative[sample] *= window[sample];
    }
    return true;
}

inline bool runNewmarkAdjoint(
    const SolverSettings& settings,
    const mfem::Operator& M,
    const mfem::Operator& C,
    const mfem::Operator& effective_transpose,
    const mfem::Operator& initial_transpose,
    const mfem::Array<int>& essential_dofs,
    const mfem::Vector& outlet_functional,
    const std::vector<double>& outlet_derivative,
    const LinearSolve& solve_effective_transpose,
    const LinearSolve& solve_initial_transpose,
    const GlobalNorm& norm,
    std::vector<mfem::Vector>& adjoint,
    mfem::Vector& initial_adjoint,
    SolverPerformance& performance,
    double& elapsed_seconds,
    const LogFunction& log,
    const char* objective_name)
{
    const auto started_at = std::chrono::steady_clock::now();
    const int time_steps = static_cast<int>(outlet_derivative.size());
    const int state_size = M.Height();
    const double beta = settings.newmarkBeta;
    const double gamma = settings.newmarkGamma;
    const double a_1 = 1.0 - gamma / beta;
    const double a_2 =
        (1.0 - gamma / (2.0 * beta)) * settings.dt;
    const double a_3 = gamma / (beta * settings.dt);
    const double a_4 = 1.0 / (beta * settings.dt);
    const double a_5 = 1.0 / (2.0 * beta) - 1.0;
    const double a_6 = 1.0 / (beta * settings.dt * settings.dt);

    adjoint.resize(time_steps + 1);
    for (mfem::Vector& state : adjoint) {
        state.SetSize(state_size);
    }
    adjoint[0] = 0.0;

    mfem::Vector bar_v(state_size);
    mfem::Vector bar_v_dot(state_size);
    mfem::Vector bar_v_ddot(state_size);
    mfem::Vector previous_bar_v(state_size);
    mfem::Vector previous_bar_v_dot(state_size);
    mfem::Vector previous_bar_v_ddot(state_size);
    mfem::Vector M_transpose_adjoint(state_size);
    mfem::Vector C_transpose_adjoint(state_size);
    mfem::Vector residual(state_size);
    bar_v = 0.0;
    bar_v_dot = 0.0;
    bar_v_ddot = 0.0;

    for (int n = time_steps; n >= 1; --n) {
        if (n < time_steps) {
            bar_v.Add(outlet_derivative[n], outlet_functional);
        }

        previous_bar_v = 0.0;
        previous_bar_v.Add(-a_3, bar_v_dot);
        previous_bar_v.Add(-a_6, bar_v_ddot);
        previous_bar_v_dot = 0.0;
        previous_bar_v_dot.Add(a_1, bar_v_dot);
        previous_bar_v_dot.Add(-a_4, bar_v_ddot);
        previous_bar_v_ddot = 0.0;
        previous_bar_v_ddot.Add(a_2, bar_v_dot);
        previous_bar_v_ddot.Add(-a_5, bar_v_ddot);

        bar_v.Add(a_3, bar_v_dot);
        bar_v.Add(a_6, bar_v_ddot);
        bar_v.SetSubVector(essential_dofs, 0.0);
        if (n == time_steps) {
            adjoint[n] = 0.0;
        }
        else {
            adjoint[n] = adjoint[n + 1];
        }

        const LinearSolveResult solve =
            solve_effective_transpose(bar_v, adjoint[n]);
        performance.adjointFgmresIterations += solve.iterations;
        ++performance.adjointFgmresSolves;
        performance.maximumAdjointFgmresIterations = std::max(
            performance.maximumAdjointFgmresIterations, solve.iterations);
        effective_transpose.Mult(adjoint[n], residual);
        residual -= bar_v;
        const double relative_residual =
            norm(residual) / std::max(1.0, norm(bar_v));
        if (!solve.converged || !std::isfinite(relative_residual)
            || relative_residual > 1.0e-9) {
            std::ostringstream message;
            message << "The " << objective_name
                    << " Newmark adjoint residual gate failed at time step "
                    << n << ": physical=" << std::scientific
                    << relative_residual << ", solver=" << solve.residual
                    << ", iterations=" << solve.iterations
                    << " (limit 1.000000e-09).";
            log(LogLevel::Error, message.str());
            return false;
        }
        adjoint[n].SetSubVector(essential_dofs, 0.0);

        M.MultTranspose(adjoint[n], M_transpose_adjoint);
        C.MultTranspose(adjoint[n], C_transpose_adjoint);
        previous_bar_v.Add(a_6, M_transpose_adjoint);
        previous_bar_v.Add(a_3, C_transpose_adjoint);
        previous_bar_v_dot.Add(a_4, M_transpose_adjoint);
        previous_bar_v_dot.Add(-a_1, C_transpose_adjoint);
        previous_bar_v_ddot.Add(a_5, M_transpose_adjoint);
        previous_bar_v_ddot.Add(-a_2, C_transpose_adjoint);

        bar_v = previous_bar_v;
        bar_v_dot = previous_bar_v_dot;
        bar_v_ddot = previous_bar_v_ddot;
    }

    initial_adjoint.SetSize(state_size);
    initial_adjoint = 0.0;
    bar_v_ddot.SetSubVector(essential_dofs, 0.0);
    const LinearSolveResult initial =
        solve_initial_transpose(bar_v_ddot, initial_adjoint);
    performance.adjointFgmresIterations += initial.iterations;
    ++performance.adjointFgmresSolves;
    performance.maximumAdjointFgmresIterations = std::max(
        performance.maximumAdjointFgmresIterations, initial.iterations);
    initial_transpose.Mult(initial_adjoint, residual);
    residual -= bar_v_ddot;
    const double initial_residual =
        norm(residual) / std::max(1.0, norm(bar_v_ddot));
    if (!initial.converged || !std::isfinite(initial_residual)
        || initial_residual > 1.0e-9) {
        std::ostringstream message;
        message << "The " << objective_name
                << " initial-acceleration adjoint residual gate failed: "
                << std::scientific << initial_residual
                << " (limit 1.000000e-09).";
        log(LogLevel::Error, message.str());
        return false;
    }
    initial_adjoint.SetSubVector(essential_dofs, 0.0);
    elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started_at).count();
    return true;
}

} // namespace App::detail
