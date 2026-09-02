#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if METAMATERIAL_USE_MPI
#include "mpi.h"
#endif

#include "adjoint.hpp"
#include "integrators.hpp"
#include "solver.hpp"

namespace {

struct Summary {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
};

struct Options {
    std::optional<std::string> write_rank_reference;
    std::optional<std::string> compare_rank_reference;
    bool mumps = false;
};

void pass(Summary& summary, const std::string& name,
          const std::string& detail = {})
{
    ++summary.passed;
    std::cout << "[PASS] " << name;
    if (!detail.empty()) {
        std::cout << ": " << detail;
    }
    std::cout << '\n';
}

void fail(Summary& summary, const std::string& name,
          const std::string& detail)
{
    ++summary.failed;
    std::cerr << "[FAIL] " << name << ": " << detail << '\n';
}

void skip(Summary& summary, const std::string& name,
          const std::string& detail)
{
    ++summary.skipped;
    std::cout << "[SKIP] " << name << ": " << detail << '\n';
}

bool close(double left, double right, double relative = 1.0e-8,
           double absolute = 1.0e-12)
{
    return std::abs(left - right)
        <= absolute + relative * std::max(std::abs(left), std::abs(right));
}

bool finite_signal(const App::SignalTD& signal)
{
    return signal.size == static_cast<int>(signal.amplitude.size())
        && signal.amplitude.size() == signal.time.size()
        && std::all_of(
            signal.amplitude.begin(), signal.amplitude.end(),
            [](double value) { return std::isfinite(value); });
}

bool response_shape_valid(const App::FrequencyResponse& response)
{
    return !response.frequency.empty()
        && response.outlet.size() == response.frequency.size()
        && response.reference.size() == response.frequency.size()
        && response.valid.size() == response.frequency.size();
}

bool power_shape_valid(const App::SignalFFT& response)
{
    const std::size_t size = static_cast<std::size_t>(response.size);
    return response.size > 0
        && response.reflectedPower.size() == size
        && response.transmittedPower.size() == size
        && response.unaccountedPower.size() == size
        && response.powerBalanceValid.size() == size;
}

bool run_forward(App::Solver& solver)
{
    // Keep the application contract sacred even when setMesh() reuses its cache.
    if (!solver.setMesh()) {
        std::cerr << "[DIAGNOSTIC] forward solve failed during setMesh()\n";
        return false;
    }
    if (!solver.assembleSolutionSpace()) {
        std::cerr << "[DIAGNOSTIC] forward solve failed during "
                     "assembleSolutionSpace()\n";
        return false;
    }
    if (!solver.solve()) {
        std::cerr << "[DIAGNOSTIC] forward solve failed during solve()\n";
        return false;
    }
    return true;
}

bool check_cut_volume_integration(std::string& reason)
{
    mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
        1, 1, mfem::Element::QUADRILATERAL, true, 1.0, 1.0);
    mfem::H1_FECollection collection(1, 2);
    mfem::FiniteElementSpace space(&mesh, &collection);
    mfem::GridFunction phi(&space);
    mfem::FunctionCoefficient level_set([](const mfem::Vector& x) {
        return x[0] - 0.5;
    });
    phi.ProjectCoefficient(level_set);

    auto assemble_cut = [&](bool positive, double& measure) {
        mfem::ConstantCoefficient one(1.0);
        mfem::BilinearForm form(&space);
        auto* integrator = new App::ImplicitDomainIntegrator(
            std::make_unique<mfem::MassIntegrator>(one),
            phi, 4, 1, 0.0, positive);
        form.AddDomainIntegrator(integrator);
        form.Assemble();
        form.Finalize();
        measure = integrator->GetCutMeasure();
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
    auto positive = assemble_cut(true, positive_measure);
    auto negative = assemble_cut(false, negative_measure);
    std::unique_ptr<mfem::SparseMatrix> partition(
        mfem::Add(1.0, *positive, 1.0, *negative));
    partition->Add(-1.0, *full);
    const double tolerance = 1.0e-10 * std::max(1.0, full->MaxNorm());
    if (partition->MaxNorm() > tolerance
        || !close(positive_measure, 0.5, 0.0, 1.0e-12)
        || !close(negative_measure, 0.5, 0.0, 1.0e-12)) {
        std::ostringstream message;
        message << "cut partition: matrix residual=" << partition->MaxNorm()
                << " (limit " << tolerance << "), positive/negative measure="
                << positive_measure << "/" << negative_measure
                << " (expected 0.5/0.5, absolute tolerance 1e-12)";
        reason = message.str();
        return false;
    }

    phi = 1.0;
    positive = assemble_cut(true, positive_measure);
    negative = assemble_cut(false, negative_measure);
    positive->Add(-1.0, *full);
    if (positive->MaxNorm() > tolerance
        || negative->MaxNorm() > tolerance
        || !close(positive_measure, 1.0, 0.0, 1.0e-12)
        || !close(negative_measure, 0.0, 0.0, 1.0e-12)) {
        std::ostringstream message;
        message << "uncut reduction: positive/full matrix residual="
                << positive->MaxNorm() << ", negative matrix norm="
                << negative->MaxNorm() << " (limit " << tolerance
                << "), positive/negative measure=" << positive_measure
                << "/" << negative_measure << " (expected 1/0)";
        reason = message.str();
        return false;
    }
    return true;
}

bool check_fft_convention(std::string& reason)
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
        reason = "FFTW rejected a simple sinusoid";
        return false;
    }
    const double measured_amplitude =
        2.0 * std::abs(spectrum[frequency_bin]) / sample_count;
    const double measured_phase = std::arg(
        spectrum[frequency_bin] / std::polar(1.0, phase));
    if (!close(measured_amplitude, amplitude, 0.0, 1.0e-12)
        || !close(measured_phase, 0.0, 0.0, 1.0e-12)) {
        std::ostringstream message;
        message << "FFT bin " << frequency_bin << ": amplitude="
                << measured_amplitude << " (expected " << amplitude
                << "), phase error=" << measured_phase
                << " rad (absolute tolerance 1e-12)";
        reason = message.str();
        return false;
    }
    return true;
}

bool check_clamp_scope(std::string& reason)
{
    constexpr int nx = 4;
    mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
        nx, 2, mfem::Element::QUADRILATERAL, true, 1.0, 1.0);
    mfem::H1_FECollection collection(1, 2);
    mfem::FiniteElementSpace displacement(
        &mesh, &collection, 2, mfem::Ordering::byVDIM);
    mfem::Array<int> marker(mesh.bdr_attributes.Max());
    marker = 0;
    marker[0] = 1; // Cartesian bottom.
    marker[2] = 1; // Cartesian top.
    mfem::Array<int> essential;
    displacement.GetEssentialTrueDofs(marker, essential);

    // Two boundaries, nx + 1 nodes each, and two displacement components.
    if (essential.Size() != 4 * (nx + 1)
        || essential.Size() >= displacement.GetTrueVSize()) {
        std::ostringstream message;
        message << "essential displacement DOFs=" << essential.Size()
                << " (expected " << 4 * (nx + 1)
                << "), total true DOFs=" << displacement.GetTrueVSize();
        reason = message.str();
        return false;
    }
    return true;
}

struct Objectives {
    double pass = 0.0;
    double stop = 0.0;
    int pass_bins = 0;
    int stop_bins = 0;
};

Objectives paper_objectives(const App::FrequencyResponse& response)
{
    Objectives result;
    for (std::size_t bin = 0; bin < response.frequency.size(); ++bin) {
        if (response.valid[bin] == 0) {
            continue;
        }
        const double frequency = response.frequency[bin];
        const double transmission =
            std::abs(response.outlet[bin]) / std::abs(response.reference[bin]);
        if (frequency >= 1000.0 && frequency <= 2500.0) {
            const double error = transmission - 1.0;
            result.pass += error * error;
            ++result.pass_bins;
        }
        else if (frequency > 2500.0 && frequency <= 4000.0) {
            const double error = (transmission - 1.0e-2) / 1.0e-2;
            result.stop += error * error;
            ++result.stop_bins;
        }
    }
    return result;
}

bool check_paper_defaults(std::string& reason)
{
    const App::SolverSettings settings;
    const auto* physics = std::get_if<App::VibroacousticSettings>(
        &settings.physics);
    if (physics == nullptr) {
        reason = "the default physics is not vibroacoustics";
        return false;
    }
    const auto close_float = [](float left, float right) {
        return close(left, right, 1.0e-6, 1.0e-12);
    };
    const bool matches = settings.nx == 250 && settings.ny == 50
        && settings.nz == 0
        && close(settings.inletLength, 0.1)
        && close(settings.designLength, 0.3)
        && close(settings.outletLength, 0.1)
        && close(settings.sy, 0.1)
        && close(settings.duration, 0.02)
        && close(settings.dt, 2.0e-5)
        && close(settings.newmarkBeta, 0.25)
        && close(settings.newmarkGamma, 0.5)
        && settings.sourceSeed == 1337
        && settings.initialPatternX == 7
        && settings.initialPatternY == 7
        && close(settings.initialPatternLx, 0.1)
        && close(settings.initialPatternLy, 0.1)
        && close(settings.initialPatternBias, 0.1)
        && close(settings.initialPatternThreshold, 0.01)
        && close_float(settings.filterRadius, 0.008f)
        && close_float(physics->rho_s, 1000.0f)
        && close_float(physics->rho_a, 1.21f)
        && close_float(physics->c_a, 343.0f)
        && close_float(physics->youngs_modulus, 50.0e6f)
        && close_float(physics->poisson_ratio, 0.4f)
        && close_float(physics->zeta, 0.1f)
        && close_float(physics->f1, 1600.0f)
        && close_float(physics->f2, 2200.0f)
        && close_float(physics->epsilon, 1.0e-8f);
    if (!matches) {
        reason = "SolverSettings no longer matches the published paper setup";
    }
    return matches;
}

bool check_initial_state(
    const App::LevelSet& geometry,
    const App::SolverResult& result,
    const App::FrequencyResponse& response,
    Objectives& objectives,
    std::string& reason)
{
    if (geometry.design.Size() == 0
        || geometry.phi.Size() != geometry.design.Size()
        || geometry.activeDesignDofs.Size() == 0
        || geometry.activeDesignDofs.Size() == geometry.design.Size()) {
        reason = "the design, filtered level set, or active region is malformed";
        return false;
    }

    mfem::Array<int> active(geometry.design.Size());
    active = 0;
    bool has_air = false;
    bool has_solid = false;
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        const int dof = geometry.activeDesignDofs[i];
        active[dof] = 1;
        has_air = has_air || geometry.phi[dof] < 0.0;
        has_solid = has_solid || geometry.phi[dof] > 0.0;
    }
    for (int dof = 0; dof < geometry.design.Size(); ++dof) {
        if (active[dof] == 0 && geometry.design[dof] != 0.0) {
            reason = "the initial design modifies a fixed inlet/outlet DOF";
            return false;
        }
    }
    const double infill = result.solidInfillFraction.load(
        std::memory_order_acquire);
    if (!has_air || !has_solid || !(infill > 0.0 && infill < 1.0)) {
        reason = "the paper cosine guess does not contain both physical phases";
        return false;
    }
    if (result.success != 1
        || result.stateSize != result.displacementSize + result.pressureSize
        || result.pressureOffset != result.displacementSize
        || result.U.size() != static_cast<std::size_t>(result.timeSteps + 1)
        || result.residualNorms.size() != result.U.size()) {
        reason = "the Newmark history does not have its documented block layout";
        return false;
    }
    for (const mfem::Vector& state : result.U) {
        if (state.Size() != 3 * result.stateSize
            || state.CheckFinite() != 0) {
            reason = "the Newmark state history is incomplete or non-finite";
            return false;
        }
    }

    if (!response_shape_valid(response)) {
        reason = "the initial solve published inconsistent FFT arrays";
        return false;
    }
    objectives = paper_objectives(response);
    if (objectives.pass_bins == 0 || objectives.stop_bins == 0
        || !std::isfinite(objectives.pass)
        || !std::isfinite(objectives.stop)) {
        reason = "the initial solve has no valid paper pass/stop FFT bins";
        return false;
    }
    return true;
}

bool check_empty_duct(
    App::Solver& solver,
    App::LevelSet& geometry,
    App::SolverResult& result,
    std::string& reason)
{
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        geometry.design[geometry.activeDesignDofs[i]] = 0.0;
    }
    geometry.enforceDesignConstraints();
    if (!run_forward(solver)) {
        reason = "the all-air forward solve failed";
        return false;
    }
    if (result.solidInfillFraction.load(std::memory_order_acquire) > 1.0e-10) {
        reason = "all-air solid infill=" + std::to_string(
            result.solidInfillFraction.load(std::memory_order_acquire))
            + " (limit 1e-10)";
        return false;
    }
    const App::FrequencyResponse& response = solver.frequencyResponse();
    if (!response_shape_valid(response)) {
        reason = "the all-air solve published inconsistent FFT arrays";
        return false;
    }
    int valid_bins = 0;
    for (std::size_t bin = 0; bin < response.valid.size(); ++bin) {
        if (response.valid[bin] == 0) {
            continue;
        }
        ++valid_bins;
        const double transmission =
            std::abs(response.outlet[bin] / response.reference[bin]);
        if (!std::isfinite(transmission)
            || !close(transmission, 1.0, 1.0e-8, 1.0e-10)) {
            std::ostringstream message;
            message << "all-air transmission at bin " << bin << " ("
                    << response.frequency[bin] << " Hz)=" << transmission
                    << " (expected 1, relative tolerance 1e-8, absolute 1e-10)";
            reason = message.str();
            return false;
        }
    }
    if (valid_bins == 0) {
        reason = "the all-air response has no excited FFT bins";
        return false;
    }

#if METAMATERIAL_USE_MPI
    return true;
#else
    const auto power = std::atomic_load(&result.materialImpulseResponse);
    if (!power || !power_shape_valid(*power)) {
        reason = "the all-air solve did not publish its port power balance";
        return false;
    }
    int power_bins = 0;
    for (std::size_t bin = 0; bin < power->powerBalanceValid.size(); ++bin) {
        if (power->powerBalanceValid[bin] == 0) {
            continue;
        }
        ++power_bins;
        if (!std::isfinite(power->reflectedPower[bin])
            || !std::isfinite(power->transmittedPower[bin])
            || !std::isfinite(power->unaccountedPower[bin])
            || power->reflectedPower[bin] > 0.05f
            || std::abs(power->transmittedPower[bin] - 1.0f) > 0.05f
            || power->unaccountedPower[bin] < -0.05f) {
            reason = "the all-air plane-wave port balance is not approximately lossless";
            return false;
        }
    }
    if (power_bins == 0) {
        reason = "the all-air solve has no valid plane-wave power bins";
        return false;
    }
    return true;
#endif
}

bool check_solid_slab(
    App::Solver& solver,
    App::LevelSet& geometry,
    App::SolverResult& result,
    std::string& reason)
{
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        geometry.design[geometry.activeDesignDofs[i]] = 1.0;
    }
    geometry.enforceDesignConstraints();
    if (!run_forward(solver)) {
        reason = "the prescribed full-height solid slab solve failed";
        return false;
    }
    const double infill = result.solidInfillFraction.load(
        std::memory_order_acquire);
    // Interface DOFs shared with the fixed-air inlet/outlet are deliberately
    // excluded from the design.  On this coarse mesh their transition cells
    // prevent an all-one active design from reaching 90% geometric infill.
    if (!std::isfinite(infill) || infill < 0.5) {
        reason = "the prescribed slab was not assembled predominantly as solid"
            " (infill=" + std::to_string(infill) + ")";
        return false;
    }

    const auto outlet = std::atomic_load(&result.outletPressure);
    if (!outlet || !finite_signal(*outlet)) {
        reason = "the slab did not publish a finite far-side pressure signal";
        return false;
    }
    const double maximum_outlet = std::accumulate(
        outlet->amplitude.begin(), outlet->amplitude.end(), 0.0,
        [](double maximum, double value) {
            return std::max(maximum, std::abs(value));
        });
    if (!(maximum_outlet > std::numeric_limits<double>::min())) {
        reason = "the slab is numerically impenetrable at the outlet";
        return false;
    }

#if !METAMATERIAL_USE_MPI
    double maximum_displacement = 0.0;
    for (const mfem::Vector& state : result.U) {
        for (int dof = 0; dof < result.displacementSize; ++dof) {
            maximum_displacement = std::max(
                maximum_displacement, std::abs(state[dof]));
        }
    }
    if (!(maximum_displacement > std::numeric_limits<double>::min())) {
        reason = "pressure does not excite any structural displacement in the slab";
        return false;
    }
#endif

#if METAMATERIAL_USE_MPI
    return true;
#else
    const auto power = std::atomic_load(&result.materialImpulseResponse);
    if (!power || !power_shape_valid(*power)) {
        reason = "the slab solve did not publish its port power balance";
        return false;
    }
    int valid_bins = 0;
    bool has_transmitted_power = false;
    for (std::size_t bin = 0; bin < power->powerBalanceValid.size(); ++bin) {
        if (power->powerBalanceValid[bin] == 0) {
            continue;
        }
        ++valid_bins;
        const double reflected = power->reflectedPower[bin];
        const double transmitted = power->transmittedPower[bin];
        const double unaccounted = power->unaccountedPower[bin];
        if (!std::isfinite(reflected) || !std::isfinite(transmitted)
            || !std::isfinite(unaccounted)
            || reflected < 0.0 || transmitted < 0.0
            || unaccounted < -0.05) {
            reason = "the slab creates or reports non-finite plane-wave power";
            return false;
        }
        has_transmitted_power = has_transmitted_power || transmitted > 0.0;
    }
    if (valid_bins == 0 || !has_transmitted_power) {
        reason = "the slab has no valid nonzero far-side power bin";
        return false;
    }
    return true;
#endif
}

std::vector<std::complex<double>> objective_derivative(
    const App::FrequencyResponse& response,
    std::size_t bin,
    double target)
{
    std::vector<std::complex<double>> derivative(
        response.outlet.size(), {0.0, 0.0});
    const double reference = std::abs(response.reference[bin]);
    const double outlet = std::abs(response.outlet[bin]);
    if (outlet > std::numeric_limits<double>::epsilon()
            * std::max(1.0, reference)) {
        const double transmission = outlet / reference;
        derivative[bin] = 2.0 * (transmission - target)
            / (target * target * reference * outlet)
            * response.outlet[bin];
    }
    return derivative;
}

bool prepare_adjoint_design(
    App::Solver& solver,
    App::LevelSet& geometry,
    std::size_t& pass_bin,
    std::size_t& stop_bin,
    mfem::Vector& pass_gradient,
    mfem::Vector& stop_gradient,
    std::string& reason)
{
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        geometry.design[geometry.activeDesignDofs[i]] =
            0.5 + 0.25 * std::sin(0.7 * i);
    }
    geometry.enforceDesignConstraints();
    if (!run_forward(solver)) {
        reason = "the interior adjoint design failed its forward solve";
        return false;
    }

    const App::FrequencyResponse& response = solver.frequencyResponse();
    if (!response_shape_valid(response)) {
        reason = "the adjoint design published inconsistent FFT arrays";
        return false;
    }
    pass_bin = response.frequency.size();
    stop_bin = response.frequency.size();
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
        reason = "the adjoint design has no valid pass/stop bins";
        return false;
    }
    if (!solver.differentiateFrequencyResponses(
            objective_derivative(response, pass_bin, 1.0),
            objective_derivative(response, stop_bin, 1.0e-2),
            pass_gradient,
            stop_gradient)) {
        reason = "the discrete FFT/Newmark/cut/filter adjoint failed";
        return false;
    }
    return true;
}

int strongest_active_dof(
    const App::LevelSet& geometry,
    const mfem::Vector& gradient)
{
    int strongest = -1;
    double magnitude = 0.0;
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        const int dof = geometry.activeDesignDofs[i];
        if (std::abs(gradient[dof]) > magnitude) {
            strongest = dof;
            magnitude = std::abs(gradient[dof]);
        }
    }
    return magnitude > 1.0e-12 ? strongest : -1;
}

bool check_one_gradient(
    const char* name,
    App::Solver& solver,
    App::LevelSet& geometry,
    std::size_t bin,
    double target,
    const mfem::Vector& gradient,
    int design_dof,
    double step,
    std::string& reason)
{
    const double original = geometry.design[design_dof];
    auto objective_at = [&](double value) {
        geometry.design[design_dof] = value;
        geometry.enforceDesignConstraints();
        if (!run_forward(solver)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const App::FrequencyResponse& response = solver.frequencyResponse();
        const double transmission =
            std::abs(response.outlet[bin]) / std::abs(response.reference[bin]);
        const double error = (transmission - target) / target;
        return error * error;
    };

    const double plus = objective_at(original + step);
    const double minus = objective_at(original - step);
    geometry.design[design_dof] = original;
    geometry.enforceDesignConstraints();
    const double finite_difference = (plus - minus) / (2.0 * step);
    const double denominator = std::max({
        1.0e-12, std::abs(finite_difference),
        std::abs(gradient[design_dof])});
    const double relative_error =
        std::abs(finite_difference - gradient[design_dof]) / denominator;
    if (!std::isfinite(relative_error) || relative_error > 1.0e-3) {
        std::ostringstream message;
        message << name << " gradient at DOF " << design_dof
                << " has relative error " << std::scientific
                << relative_error << " (limit 1e-3)";
        reason = message.str();
        return false;
    }
    return true;
}

bool check_adjoint_finite_difference(
    App::Solver& solver,
    App::SolverSettings& settings,
    App::LevelSet& geometry,
    mfem::Vector& pass_gradient,
    mfem::Vector& stop_gradient,
    std::string& reason)
{
    std::size_t pass_bin = 0;
    std::size_t stop_bin = 0;
    if (!prepare_adjoint_design(
            solver, geometry, pass_bin, stop_bin,
            pass_gradient, stop_gradient, reason)) {
        return false;
    }
    const int pass_dof = strongest_active_dof(geometry, pass_gradient);
    const int stop_dof = strongest_active_dof(geometry, stop_gradient);
    if (pass_dof < 0 || stop_dof < 0) {
        reason = "the coarse design produced no measurable design gradient";
        return false;
    }
    const double step = settings.cutDerivativeRelativeStep;
    if (!check_one_gradient(
            "pass", solver, geometry, pass_bin, 1.0,
            pass_gradient, pass_dof, step, reason)
        || !check_one_gradient(
            "stop", solver, geometry, stop_bin, 1.0e-2,
            stop_gradient, stop_dof, step, reason)) {
        return false;
    }

    // Restore the exact state whose adjoint is retained for rank comparison.
    return run_forward(solver)
        && solver.differentiateFrequencyResponses(
            objective_derivative(solver.frequencyResponse(), pass_bin, 1.0),
            objective_derivative(solver.frequencyResponse(), stop_bin, 1.0e-2),
            pass_gradient,
            stop_gradient);
}

std::vector<double> rank_signature(
    const App::Solver& solver,
    const App::LevelSet& geometry,
    const App::SolverResult& result,
    const mfem::Vector& pass_gradient,
    const mfem::Vector& stop_gradient)
{
    std::vector<double> signature;
    signature.push_back(result.solidInfillFraction.load(
        std::memory_order_acquire));
    const auto outlet = std::atomic_load(&result.outletPressure);
    const auto reference = std::atomic_load(&result.referenceOutletPressure);
    if (outlet) {
        signature.insert(signature.end(),
                         outlet->amplitude.begin(), outlet->amplitude.end());
    }
    if (reference) {
        signature.insert(signature.end(),
                         reference->amplitude.begin(), reference->amplitude.end());
    }
    const App::FrequencyResponse& response = solver.frequencyResponse();
    for (std::size_t bin = 0; bin < response.frequency.size(); ++bin) {
        signature.push_back(response.frequency[bin]);
        signature.push_back(response.outlet[bin].real());
        signature.push_back(response.outlet[bin].imag());
        signature.push_back(response.reference[bin].real());
        signature.push_back(response.reference[bin].imag());
        signature.push_back(response.valid[bin]);
    }
    signature.insert(signature.end(), geometry.phi.GetData(),
                     geometry.phi.GetData() + geometry.phi.Size());
    signature.insert(signature.end(), pass_gradient.GetData(),
                     pass_gradient.GetData() + pass_gradient.Size());
    signature.insert(signature.end(), stop_gradient.GetData(),
                     stop_gradient.GetData() + stop_gradient.Size());
    return signature;
}

bool write_signature(
    const std::string& path,
    const std::vector<double>& signature,
    std::string& reason)
{
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        reason = "could not create rank reference " + path;
        return false;
    }
    output << "SOLVER_TEST_SUITE_RANK_SIGNATURE_V1\n"
           << signature.size() << '\n'
           << std::setprecision(17);
    for (double value : signature) {
        output << value << '\n';
    }
    if (!output) {
        reason = "could not finish writing rank reference " + path;
        return false;
    }
    return true;
}

bool compare_signature(
    const std::string& path,
    const std::vector<double>& actual,
    std::string& reason)
{
    std::ifstream input(path);
    std::string header;
    std::size_t count = 0;
    if (!(input >> header >> count)
        || header != "SOLVER_TEST_SUITE_RANK_SIGNATURE_V1"
        || count != actual.size()) {
        reason = "rank reference is missing or has an incompatible shape";
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        double expected = 0.0;
        if (!(input >> expected)
            || !close(actual[i], expected, 1.0e-6, 1.0e-11)) {
            std::ostringstream message;
            message << "rank signature differs at value " << i
                    << ": expected " << std::setprecision(17) << expected
                    << ", got " << actual[i];
            reason = message.str();
            return false;
        }
    }
    return true;
}

bool parse_options(int argc, char** argv, Options& options, std::string& reason)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--write-rank-reference"
            || argument == "--compare-rank-reference") {
            if (++i >= argc) {
                reason = argument + " requires a file path";
                return false;
            }
            if (argument == "--write-rank-reference") {
                options.write_rank_reference = argv[i];
            }
            else {
                options.compare_rank_reference = argv[i];
            }
        }
        else if (argument == "--mumps") {
            options.mumps = true;
        }
        else {
            reason = "unknown option: " + argument;
            return false;
        }
    }
    if (options.write_rank_reference && options.compare_rank_reference) {
        reason = "choose either write or compare rank reference, not both";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string option_error;
    if (!parse_options(argc, argv, options, option_error)) {
        std::cerr << option_error << '\n';
        return 2;
    }

    int rank = 0;
    int success = 1;
#if METAMATERIAL_USE_MPI
    int provided = 0;
    mfem::Mpi::Init(argc, argv, MPI_THREAD_SERIALIZED, &provided);
    mfem::Hypre::Init();
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    success = provided >= MPI_THREAD_SERIALIZED ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &success, 1, MPI_INT, MPI_MIN,
                  MPI_COMM_WORLD);
    if (!success) {
        if (rank == 0) {
            std::cerr << "MPI_THREAD_SERIALIZED is unavailable.\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
        return 1;
    }
#endif

    mfem::Device device(METAMATERIAL_USE_CUDA ? "cuda" : "cpu");
    if (rank == 0) {
        device.Print();
    }

    App::SolverSettings settings;
    settings.nx = 20;
    settings.ny = 4;
    settings.duration = 0.002;
    settings.dt = 0.0001;
#if METAMATERIAL_USE_MPI
    settings.linearSolveMethod = options.mumps
        ? App::LinearSolveMethod::mumps
        : App::LinearSolveMethod::fgmres;
#else
    if (options.mumps) {
        std::cerr << "--mumps requires a parallel-cpu build.\n";
        return 2;
    }
#endif
    App::LevelSet geometry;
    App::SolverResult result;
    const App::LogFunction log = [rank](
        App::LogLevel level, std::string message) {
        if (rank == 0 && level != App::LogLevel::Message) {
            std::cerr << message << '\n';
        }
    };
    App::Solver solver(settings, geometry, result, log);

#if METAMATERIAL_USE_MPI
    if (rank != 0) {
        if (success) {
            solver.parallelWorkerLoop();
        }
    }
    else
#endif
    {
        Summary summary;
        try {
#if METAMATERIAL_USE_MPI
            pass(summary, "MPI thread contract");
#endif

            std::string reason;
            if (check_cut_volume_integration(reason)) {
                pass(summary, "Algoim cut-domain partition");
            }
            else {
                fail(summary, "Algoim cut-domain partition", reason);
            }

            reason.clear();
            if (check_fft_convention(reason)) {
                pass(summary, "FFTW amplitude and phase convention");
            }
            else {
                fail(summary, "FFTW amplitude and phase convention", reason);
            }

            reason.clear();
            if (check_clamp_scope(reason)) {
                pass(summary, "Top/bottom essential-DOF scope");
            }
            else {
                fail(summary, "Top/bottom essential-DOF scope", reason);
            }

            reason.clear();
            if (check_paper_defaults(reason)) {
                pass(summary, "Paper solver defaults");
            }
            else {
                fail(summary, "Paper solver defaults", reason);
            }

            Objectives objectives;
            if (!run_forward(solver)) {
                fail(summary, "Paper initial state", "the initial forward solve failed");
            }
            else {
                reason.clear();
                if (check_initial_state(
                        geometry, result, solver.frequencyResponse(),
                        objectives, reason)) {
                    std::ostringstream detail;
                    detail << "coarse pass/stop proxy = "
                           << std::setprecision(8) << objectives.pass
                           << " / " << objectives.stop;
                    pass(summary, "Paper initial state", detail.str());
                }
                else {
                    fail(summary, "Paper initial state", reason);
                }
            }

            reason.clear();
            if (check_empty_duct(solver, geometry, result, reason)) {
                pass(summary, "Empty-duct reference"
#if !METAMATERIAL_USE_MPI
                    " and port power"
#endif
                );
            }
            else {
                fail(summary, "Empty-duct reference and port power", reason);
            }

            reason.clear();
            if (check_solid_slab(solver, geometry, result, reason)) {
                pass(summary, "Prescribed slab coupling"
#if !METAMATERIAL_USE_MPI
                    " and port power"
#endif
                );
            }
            else {
                fail(summary, "Prescribed slab coupling and port power", reason);
            }
            skip(summary, "Prescribed slab interface-work balance",
                 "the public SolverResult does not publish interface work or structural energy");
#if METAMATERIAL_USE_MPI
            skip(summary, "Distributed plane-wave port balance",
                 "the parallel solver does not publish the serial port-power diagnostic yet");
            skip(summary, "Global slab displacement norm",
                 "SolverResult retains rank-local states and publishes no reduced displacement norm");
#endif

            mfem::Vector pass_gradient;
            mfem::Vector stop_gradient;
            reason.clear();
            if (check_adjoint_finite_difference(
                    solver, settings, geometry,
                    pass_gradient, stop_gradient, reason)) {
                pass(summary, "Discrete adjoint finite differences");
            }
            else {
                fail(summary, "Discrete adjoint finite differences", reason);
            }

            if (pass_gradient.Size() == geometry.design.Size()
                && stop_gradient.Size() == geometry.design.Size()) {
                const std::vector<double> signature = rank_signature(
                    solver, geometry, result, pass_gradient, stop_gradient);
                reason.clear();
                if (options.write_rank_reference) {
                    if (write_signature(
                            *options.write_rank_reference, signature, reason)) {
                        pass(summary, "Rank-invariance reference write",
                             *options.write_rank_reference);
                    }
                    else {
                        fail(summary, "Rank-invariance reference write", reason);
                    }
                }
                else if (options.compare_rank_reference) {
                    if (compare_signature(
                            *options.compare_rank_reference, signature, reason)) {
                        pass(summary, "Rank invariance",
                             *options.compare_rank_reference);
                    }
                    else {
                        fail(summary, "Rank invariance", reason);
                    }
                }
                else {
                    skip(summary, "Rank invariance",
                         "write with --write-rank-reference FILE, then compare another mpiexec run with --compare-rank-reference FILE");
                }
            }
            else {
                fail(summary, "Rank invariance",
                     "no complete adjoint signature was available");
            }
        }
        catch (const std::exception& error) {
            fail(summary, "Unhandled test exception", error.what());
        }
        catch (...) {
            fail(summary, "Unhandled test exception", "unknown exception");
        }

        success = summary.failed == 0 ? 1 : 0;
        std::cout << "SolverTestSuite: " << summary.passed << " passed, "
                  << summary.failed << " failed, " << summary.skipped
                  << " skipped.\n";
#if METAMATERIAL_USE_MPI
        solver.shutdownParallelWorkers();
#endif
    }

#if METAMATERIAL_USE_MPI
    MPI_Bcast(&success, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    return success ? 0 : 1;
}
