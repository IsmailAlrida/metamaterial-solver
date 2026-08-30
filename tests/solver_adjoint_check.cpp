#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "solver.hpp"

int main()
{
    mfem::Device device(METAMATERIAL_USE_CUDA ? "cuda" : "cpu");
    device.Print();

    App::SolverSettings solver_settings;
    solver_settings.nx = 20;
    solver_settings.ny = 4;
    solver_settings.duration = 0.002;
    solver_settings.dt = 0.0001;

    App::OptimizerSettings optimizer_settings;
    optimizer_settings.maxIterations = 1;

    App::LevelSet geometry;
    App::SolverResult result;
    App::LogFunction log = [](App::LogLevel level, std::string message) {
        if (level == App::LogLevel::Error) {
            std::cerr << message << '\n';
        }
    };
    App::Solver solver(
        solver_settings, optimizer_settings, geometry, result, log);

    if (!solver.setMesh()
        || !solver.assembleSolutionSpace()
        || !solver.solve()) {
        std::cerr << "The coarse baseline solve failed.\n";
        return 1;
    }

    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        geometry.design[geometry.activeDesignDofs[i]] =
            0.5 + 0.25 * std::sin(0.7 * i);
    }
    geometry.enforceDesignConstraints();
    if (!solver.assembleSolutionSpace() || !solver.solve()) {
        std::cerr << "The coarse interior-design solve failed.\n";
        return 1;
    }

    const App::FrequencyResponse& response = solver.frequencyResponse();
    if (response.frequency.size() < 2
        || response.outlet.size() != response.frequency.size()
        || response.reference.size() != response.frequency.size()
        || response.valid.size() != response.frequency.size()) {
        std::cerr << "The coarse solve did not produce an FFT.\n";
        return 1;
    }

    std::size_t pass_bin = response.frequency.size();
    std::size_t stop_bin = response.frequency.size();
    for (std::size_t bin = 0; bin < response.frequency.size(); ++bin) {
        if (response.valid[bin] == 0) {
            continue;
        }
        if (pass_bin == response.frequency.size()
            && response.frequency[bin] >= 1000.0
            && response.frequency[bin] <= 2500.0) {
            pass_bin = bin;
        }
        if (stop_bin == response.frequency.size()
            && response.frequency[bin] > 2500.0
            && response.frequency[bin] <= 4000.0) {
            stop_bin = bin;
        }
    }
    if (pass_bin == response.frequency.size()
        || stop_bin == response.frequency.size()) {
        std::cerr << "The coarse solve has no valid pass/stop FFT bins.\n";
        return 1;
    }

    auto spectrum_derivative = [&response](std::size_t bin, double target) {
        std::vector<std::complex<double>> derivative(
            response.outlet.size(), {0.0, 0.0});
        const double reference = std::abs(response.reference[bin]);
        const double outlet = std::abs(response.outlet[bin]);
        const double transmission = outlet / reference;
        if (outlet > std::numeric_limits<double>::epsilon()
                * std::max(1.0, reference)) {
            derivative[bin] = 2.0 * (transmission - target)
                / (target * target * reference * outlet)
                * response.outlet[bin];
        }
        return derivative;
    };

    mfem::Vector pass_gradient;
    mfem::Vector stop_gradient;
    if (!solver.differentiateFrequencyResponses(
            spectrum_derivative(pass_bin, 1.0),
            spectrum_derivative(stop_bin, 1.0e-2),
            pass_gradient,
            stop_gradient)) {
        std::cerr << "A coarse pass/stop discrete adjoint failed.\n";
        return 1;
    }

    auto rank_dofs = [&geometry](const mfem::Vector& gradient) {
        std::vector<std::pair<double, int>> ranked;
        for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
            const int dof = geometry.activeDesignDofs[i];
            ranked.emplace_back(std::abs(gradient[dof]), dof);
        }
        std::sort(ranked.begin(), ranked.end(), std::greater<>());
        return ranked;
    };
    const auto pass_dofs = rank_dofs(pass_gradient);
    const auto stop_dofs = rank_dofs(stop_gradient);
    if (pass_dofs.size() < 3 || stop_dofs.size() < 3
        || pass_dofs[2].first <= 1.0e-12
        || stop_dofs[2].first <= 1.0e-12) {
        std::cerr << "The coarse design produced no measurable gradient.\n";
        return 1;
    }

    const double step = optimizer_settings.cutDerivativeRelativeStep;
    auto check_gradient = [&](const char* name,
                              std::size_t bin,
                              double target,
                              const mfem::Vector& gradient,
                              int design_dof) {
        const double original_design = geometry.design[design_dof];
        auto objective_at = [&](double value) {
            geometry.design[design_dof] = value;
            geometry.enforceDesignConstraints();
            if (!solver.assembleSolutionSpace() || !solver.solve()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const App::FrequencyResponse& current = solver.frequencyResponse();
            const double transmission = std::abs(current.outlet[bin])
                / std::abs(current.reference[bin]);
            const double error = (transmission - target) / target;
            return error * error;
        };
        const double plus = objective_at(original_design + step);
        const double minus = objective_at(original_design - step);
        geometry.design[design_dof] = original_design;
        geometry.enforceDesignConstraints();

        const double finite_difference = (plus - minus) / (2.0 * step);
        const double denominator = std::max({
            1.0e-12,
            std::abs(finite_difference),
            std::abs(gradient[design_dof])});
        const double relative_error = std::abs(
            finite_difference - gradient[design_dof]) / denominator;
        if (!std::isfinite(relative_error) || relative_error > 1.0e-3) {
            std::cerr << name << " gradient at DOF " << design_dof
                      << " has relative error " << relative_error << ".\n";
            return false;
        }
        return true;
    };

    for (int candidate = 0; candidate < 3; ++candidate) {
        if (!check_gradient(
                "Pass", pass_bin, 1.0,
                pass_gradient, pass_dofs[candidate].second)
            || !check_gradient(
                "Stop", stop_bin, 1.0e-2,
                stop_gradient, stop_dofs[candidate].second)) {
            return 1;
        }
    }

    std::cout << "solver adjoint check passed\n";
    return 0;
}
