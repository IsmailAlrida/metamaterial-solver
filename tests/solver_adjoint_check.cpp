#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "adjoint.hpp"
#include "integrators.hpp"
#include "solver.hpp"

namespace {

bool check_cut_volume_integration()
{
    mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
        1, 1, mfem::Element::QUADRILATERAL, true, 1.0, 1.0);
    mfem::H1_FECollection collection(1, 2);
    mfem::FiniteElementSpace space(&mesh, &collection);
    mfem::GridFunction phi(&space);
    mfem::FunctionCoefficient cut([](const mfem::Vector& x) {
        return x[0] - 0.5;
    });
    phi.ProjectCoefficient(cut);

    auto assemble = [&](bool positive, double* cut_measure = nullptr) {
        mfem::ConstantCoefficient one(1.0);
        mfem::BilinearForm form(&space);
        auto* integrator = new App::ImplicitDomainIntegrator(
            std::make_unique<mfem::MassIntegrator>(one),
            phi, 4, 1, 0.0, positive);
        form.AddDomainIntegrator(integrator);
        form.Assemble();
        form.Finalize();
        if (cut_measure != nullptr) {
            *cut_measure = integrator->GetCutMeasure();
        }
        return std::unique_ptr<mfem::SparseMatrix>(form.LoseMat());
    };
    auto assemble_full = [&] {
        mfem::ConstantCoefficient one(1.0);
        mfem::BilinearForm form(&space);
        form.AddDomainIntegrator(new mfem::MassIntegrator(one));
        form.Assemble();
        form.Finalize();
        return std::unique_ptr<mfem::SparseMatrix>(form.LoseMat());
    };

    const auto full = assemble_full();
    double positive_measure = 0.0;
    double negative_measure = 0.0;
    auto positive = assemble(true, &positive_measure);
    auto negative = assemble(false, &negative_measure);
    std::unique_ptr<mfem::SparseMatrix> complement(
        mfem::Add(1.0, *positive, 1.0, *negative));
    complement->Add(-1.0, *full);
    const double tolerance = 1.0e-10 * std::max(1.0, full->MaxNorm());
    if (complement->MaxNorm() > tolerance
        || std::abs(positive_measure - 0.5) > 1.0e-12
        || std::abs(negative_measure - 0.5) > 1.0e-12) {
        return false;
    }

    phi = 1.0;
    positive = assemble(true, &positive_measure);
    negative = assemble(false, &negative_measure);
    positive->Add(-1.0, *full);
    return positive->MaxNorm() <= tolerance
        && negative->MaxNorm() <= tolerance
        && std::abs(positive_measure - 1.0) <= 1.0e-12
        && std::abs(negative_measure) <= 1.0e-12;
}

bool check_fft_convention()
{
    constexpr int sample_count = 32;
    constexpr int frequency_bin = 3;
    constexpr double amplitude = 2.5;
    constexpr double phase = 0.37;
    const double pi = std::acos(-1.0);
    std::vector<double> signal(sample_count);
    std::vector<double> window(sample_count, 1.0);
    for (int sample = 0; sample < sample_count; ++sample) {
        signal[sample] = amplitude * std::cos(
            2.0 * pi * frequency_bin * sample / sample_count + phase);
    }
    std::vector<std::complex<double>> spectrum;
    if (!App::detail::forwardWindowedSignal(signal, window, spectrum)) {
        return false;
    }
    const double measured_amplitude =
        2.0 * std::abs(spectrum[frequency_bin]) / sample_count;
    const double measured_phase = std::arg(
        spectrum[frequency_bin] / std::polar(1.0, phase));
    return std::abs(measured_amplitude - amplitude) <= 1.0e-12
        && std::abs(measured_phase) <= 1.0e-12;
}

} // namespace

int main()
{
    mfem::Device device(METAMATERIAL_USE_CUDA ? "cuda" : "cpu");
    device.Print();

    if (!check_cut_volume_integration()) {
        std::cerr << "Algoim cut-volume integration failed its complement check.\n";
        return 1;
    }
    if (!check_fft_convention()) {
        std::cerr << "The FFT amplitude/phase convention is inconsistent.\n";
        return 1;
    }

    App::SolverSettings solver_settings;
    solver_settings.nx = 20;
    solver_settings.ny = 4;
    solver_settings.duration = 0.002;
    solver_settings.dt = 0.0001;

    App::LevelSet geometry;
    App::SolverResult result;
    App::LogFunction log = [](App::LogLevel level, std::string message) {
        if (level == App::LogLevel::Error) {
            std::cerr << message << '\n';
        }
    };
    App::Solver solver(
        solver_settings, geometry, result, log);

    if (!solver.setMesh()
        || !solver.assembleSolutionSpace()
        || !solver.solve()) {
        std::cerr << "The coarse baseline solve failed.\n";
        return 1;
    }

    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        geometry.design[geometry.activeDesignDofs[i]] = 0.0;
    }
    geometry.enforceDesignConstraints();
    if (!solver.assembleSolutionSpace() || !solver.solve()) {
        std::cerr << "The coarse empty-duct solve failed.\n";
        return 1;
    }
    if (result.solidInfillFraction.load(std::memory_order_acquire)
            > 1.0e-10) {
        std::cerr << "The empty duct reports nonzero solid infill.\n";
        return 1;
    }
    int valid_empty_bins = 0;
    const App::FrequencyResponse& empty_response = solver.frequencyResponse();
    for (std::size_t bin = 0; bin < empty_response.valid.size(); ++bin) {
        if (empty_response.valid[bin] == 0) {
            continue;
        }
        ++valid_empty_bins;
        const double transmission = std::abs(
            empty_response.outlet[bin] / empty_response.reference[bin]);
        if (!std::isfinite(transmission)
            || std::abs(transmission - 1.0) > 1.0e-8) {
            std::cerr << "The empty duct does not reproduce its reference FFT.\n";
            return 1;
        }
    }
    if (valid_empty_bins == 0) {
        std::cerr << "The empty duct has no valid FFT bins.\n";
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

    const double step = solver_settings.cutDerivativeRelativeStep;
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
