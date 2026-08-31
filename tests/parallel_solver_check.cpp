#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "mpi.h"

#include "global_types.hpp"
#include "logging.hpp"
#include "solver.hpp"

namespace {

bool finite_signal(const App::SignalTD& signal)
{
    return std::all_of(
        signal.amplitude.begin(), signal.amplitude.end(),
        [](double value) { return std::isfinite(value); });
}

bool matching_signal(
    const App::SignalTD& first,
    const App::SignalTD& second)
{
    if (first.amplitude.size() != second.amplitude.size()) {
        return false;
    }
    for (std::size_t sample = 0; sample < first.amplitude.size(); ++sample) {
        const double scale = std::max({
            1.0,
            std::abs(first.amplitude[sample]),
            std::abs(second.amplitude[sample])});
        if (std::abs(first.amplitude[sample] - second.amplitude[sample])
            > 1.0e-8 * scale) {
            return false;
        }
    }
    return true;
}

bool matching_response(
    const App::FrequencyResponse& first,
    const App::FrequencyResponse& second)
{
    if (first.frequency.size() != second.frequency.size()
        || first.outlet.size() != second.outlet.size()
        || first.reference.size() != second.reference.size()
        || first.valid != second.valid) {
        return false;
    }
    auto close = [](const auto& left, const auto& right) {
        const double scale = std::max({
            1.0, std::abs(left), std::abs(right)});
        return std::abs(left - right) <= 1.0e-8 * scale;
    };
    for (std::size_t bin = 0; bin < first.frequency.size(); ++bin) {
        if (!close(first.frequency[bin], second.frequency[bin])
            || !close(first.outlet[bin], second.outlet[bin])
            || !close(first.reference[bin], second.reference[bin])) {
            return false;
        }
    }
    return true;
}

bool matching_vector(const mfem::Vector& first, const mfem::Vector& second)
{
    if (first.Size() != second.Size()) {
        return false;
    }
    for (int i = 0; i < first.Size(); ++i) {
        const double scale = std::max({
            1.0, std::abs(first[i]), std::abs(second[i])});
        if (std::abs(first[i] - second[i]) > 1.0e-7 * scale) {
            return false;
        }
    }
    return true;
}

std::vector<std::complex<double>> response_derivative(
    const App::FrequencyResponse& response)
{
    std::vector<std::complex<double>> derivative(
        response.outlet.size(), {0.0, 0.0});
    for (std::size_t bin = 1; bin < response.outlet.size(); ++bin) {
        const double reference = std::abs(response.reference[bin]);
        const double outlet = std::abs(response.outlet[bin]);
        if (response.valid[bin] != 0
            && reference > 0.0
            && outlet > std::numeric_limits<double>::epsilon()) {
            const double target = 0.5;
            const double transmission = outlet / reference;
            derivative[bin] = 2.0 * (transmission - target)
                / (target * target * reference * outlet)
                * response.outlet[bin];
            break;
        }
    }
    return derivative;
}

} // namespace

int main(int argc, char** argv)
{
    int provided = 0;
    mfem::Mpi::Init(argc, argv, MPI_THREAD_SERIALIZED, &provided);
    mfem::Hypre::Init();
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    App::SolverSettings settings;
    settings.nx = 20;
    settings.ny = 4;
    settings.duration = 1.0e-4;
    settings.dt = 2.0e-5;
    App::LevelSet level_set;
    App::SolverResult result;
    const App::LogFunction log = [rank](
        App::LogLevel, std::string message) {
        if (rank == 0) {
            std::cout << message << '\n';
        }
    };
    App::Solver solver(
        settings, level_set, result, log);

    int success = provided >= MPI_THREAD_SERIALIZED ? 1 : 0;
    if (!success) {
        if (rank == 0) {
            std::cerr << "MPI_THREAD_SERIALIZED is unavailable.\n";
        }
    }
    else if (rank != 0) {
        solver.parallelWorkerLoop();
    }
    else {
        std::cout << "[1/3] Running parallel FGMRES forward/adjoint...\n";
        settings.linearSolveMethod = App::LinearSolveMethod::fgmres;
        bool fgmres_forward = solver.setMesh()
            && solver.assembleSolutionSpace()
            && solver.solve();
        const auto fgmres_signal = result.outletPressure;
        const double fgmres_infill = result.solidInfillFraction.load(
            std::memory_order_acquire);
        const App::FrequencyResponse fgmres_response =
            solver.frequencyResponse();
        const auto derivative = response_derivative(fgmres_response);
        mfem::Vector fgmres_gradient;
        mfem::Vector unused_gradient;
        const bool derivative_ready = std::any_of(
                derivative.begin(), derivative.end(),
                [](const std::complex<double>& value) {
                    return value != std::complex<double>{};
                });
        bool fgmres_adjoint = fgmres_forward && derivative_ready;
        if (fgmres_adjoint) {
            fgmres_adjoint = solver.differentiateFrequencyResponses(
                derivative,
                {},
                fgmres_gradient,
                unused_gradient);
        }
        fgmres_forward = fgmres_forward && fgmres_signal
            && finite_signal(*fgmres_signal)
            && result.U.size()
                == static_cast<std::size_t>(result.timeSteps + 1);
        std::cout << "      FGMRES forward: "
                  << (fgmres_forward ? "pass" : "fail")
                  << ", adjoint: "
                  << (fgmres_adjoint ? "pass" : "fail") << ".\n";

        std::cout << "[2/3] Running parallel MUMPS forward/adjoint...\n";
        settings.linearSolveMethod = App::LinearSolveMethod::mumps;
        bool mumps_forward = solver.setMesh()
            && solver.assembleSolutionSpace()
            && solver.solve();
        const auto mumps_signal = result.outletPressure;
        const double mumps_infill = result.solidInfillFraction.load(
            std::memory_order_acquire);
        const App::FrequencyResponse mumps_response =
            solver.frequencyResponse();
        mfem::Vector mumps_gradient;
        bool mumps_adjoint = mumps_forward && derivative_ready;
        if (mumps_adjoint) {
            mumps_adjoint = solver.differentiateFrequencyResponses(
                derivative,
                {},
                mumps_gradient,
                unused_gradient);
        }
        mumps_forward = mumps_forward && mumps_signal
            && finite_signal(*mumps_signal)
            && solver.performance().mumpsEffectiveFactorizationSeconds > 0.0
            && solver.performance().mumpsSolveSeconds > 0.0;
        const bool mumps_parity = fgmres_forward && fgmres_adjoint
            && mumps_forward && mumps_adjoint
            && std::abs(fgmres_infill - mumps_infill) <= 1.0e-10
            && matching_signal(*fgmres_signal, *mumps_signal)
            && matching_response(fgmres_response, mumps_response)
            && matching_vector(fgmres_gradient, mumps_gradient);
        std::cout << "      MUMPS forward: "
                  << (mumps_forward ? "pass" : "fail")
                  << ", adjoint: "
                  << (mumps_adjoint ? "pass" : "fail")
                  << ", FGMRES parity: "
                  << (mumps_parity ? "pass" : "fail") << ".\n";

        success = fgmres_forward && fgmres_adjoint
            && mumps_forward && mumps_adjoint && mumps_parity;
        std::cout << "[3/3] Parallel Solver check "
                  << (success ? "passed" : "failed") << ".\n";
        solver.shutdownParallelWorkers();
    }

    MPI_Bcast(&success, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    return success ? 0 : 1;
}
