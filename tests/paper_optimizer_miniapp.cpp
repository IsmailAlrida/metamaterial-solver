#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <Eigen/Dense>
#if METAMATERIAL_USE_MPI
#include "mpi.h"
#endif
#include "zip.h"

#include "optimizer.hpp"
#include "solver.hpp"

namespace {

volatile std::sig_atomic_t interrupt_requested = 0;

void handle_interrupt(int)
{
    interrupt_requested = 1;
}

struct ObjectivePoint {
    int iteration;
    double pass;
    double stop;
    double elapsedSeconds;
};

struct TransferFunctionFit {
    int order = 0;
    double frequencyScaleHz = 0.0;
    double rmseDb = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> numerator;
    std::vector<double> denominator;
    std::vector<double> frequency;
    std::vector<double> transmissionDb;
};

TransferFunctionFit fit_transfer_function(
    const App::FrequencyResponse& response,
    const std::vector<App::FrequencyBand>& bands)
{
    TransferFunctionFit fit;
    if (bands.empty()) {
        return fit;
    }

    double minimum_frequency = bands.front().startHz;
    double maximum_frequency = bands.front().endHz;
    for (const App::FrequencyBand& band : bands) {
        minimum_frequency = std::min(minimum_frequency, band.startHz);
        maximum_frequency = std::max(maximum_frequency, band.endHz);
    }

    std::vector<std::size_t> samples;
    for (std::size_t bin = 0; bin < response.frequency.size(); ++bin) {
        if (bin < response.valid.size()
            && bin < response.outlet.size()
            && bin < response.reference.size()
            && response.valid[bin]
            && response.frequency[bin] >= minimum_frequency
            && response.frequency[bin] <= maximum_frequency
            && std::abs(response.reference[bin]) > 0.0) {
            samples.push_back(bin);
        }
    }
    if (samples.size() < 3 || maximum_frequency <= 0.0) {
        return fit;
    }

    fit.order = std::min(4, static_cast<int>((samples.size() - 1) / 2));
    fit.frequencyScaleHz = maximum_frequency;
    const int unknowns = 2 * fit.order + 1;
    Eigen::MatrixXd matrix(2 * samples.size(), unknowns);
    Eigen::VectorXd right_hand_side(2 * samples.size());

    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
        const std::size_t bin = samples[sample];
        const std::complex<double> transfer =
            response.outlet[bin] / response.reference[bin];
        const std::complex<double> q(
            0.0, response.frequency[bin] / fit.frequencyScaleHz);
        std::vector<std::complex<double>> powers(fit.order + 1, 1.0);
        for (int power = 1; power <= fit.order; ++power) {
            powers[power] = powers[power - 1] * q;
        }

        for (int power = 0; power <= fit.order; ++power) {
            matrix(2 * sample, power) = powers[power].real();
            matrix(2 * sample + 1, power) = powers[power].imag();
        }
        for (int power = 1; power <= fit.order; ++power) {
            const std::complex<double> coefficient = -transfer * powers[power];
            const int column = fit.order + power;
            matrix(2 * sample, column) = coefficient.real();
            matrix(2 * sample + 1, column) = coefficient.imag();
        }
        right_hand_side[2 * sample] = transfer.real();
        right_hand_side[2 * sample + 1] = transfer.imag();
    }

    Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(matrix);
    const Eigen::VectorXd coefficients = qr.solve(right_hand_side);
    if (qr.rank() < unknowns || !coefficients.allFinite()) {
        return {};
    }

    fit.numerator.resize(fit.order + 1);
    fit.denominator.resize(fit.order + 1, 1.0);
    for (int power = 0; power <= fit.order; ++power) {
        fit.numerator[power] = coefficients[power];
    }
    for (int power = 1; power <= fit.order; ++power) {
        fit.denominator[power] = coefficients[fit.order + power];
    }

    auto evaluate = [](const std::vector<double>& polynomial,
                       std::complex<double> q) {
        std::complex<double> value = 0.0;
        for (auto coefficient = polynomial.rbegin();
             coefficient != polynomial.rend(); ++coefficient) {
            value = value * q + *coefficient;
        }
        return value;
    };

    double squared_error = 0.0;
    for (std::size_t bin : samples) {
        const std::complex<double> q(
            0.0, response.frequency[bin] / fit.frequencyScaleHz);
        const std::complex<double> denominator = evaluate(fit.denominator, q);
        if (std::abs(denominator) < 1.0e-12) {
            return {};
        }
        const std::complex<double> fitted = evaluate(fit.numerator, q) / denominator;
        const double measured_db = 20.0 * std::log10(std::max(
            std::abs(response.outlet[bin] / response.reference[bin]), 1.0e-12));
        const double fitted_db =
            20.0 * std::log10(std::max(std::abs(fitted), 1.0e-12));
        if (!std::isfinite(measured_db) || !std::isfinite(fitted_db)) {
            return {};
        }
        squared_error += (fitted_db - measured_db) * (fitted_db - measured_db);
        fit.frequency.push_back(response.frequency[bin]);
        fit.transmissionDb.push_back(fitted_db);
    }
    fit.rmseDb = std::sqrt(squared_error / samples.size());
    if (!std::isfinite(fit.rmseDb)) {
        return {};
    }
    return fit;
}

const char* status_name(App::OptimizerStatus status)
{
    switch (status) {
    case App::OptimizerStatus::Idle: return "idle";
    case App::OptimizerStatus::Working: return "working";
    case App::OptimizerStatus::Converged: return "converged";
    case App::OptimizerStatus::MaximumIterations: return "maximum_iterations";
    case App::OptimizerStatus::Diverged: return "diverged";
    case App::OptimizerStatus::Cancelled: return "cancelled";
    case App::OptimizerStatus::Error: return "error";
    }
    return "unknown";
}

void write_number(std::ostream& output, double value)
{
    if (std::isfinite(value)) {
        output << value;
    }
    else {
        output << "null";
    }
}

bool write_report_data(
    const std::filesystem::path& path,
    const char* mode,
    bool high_pass,
    bool gate_passed,
    const App::SolverSettings& solver_settings,
    const App::OptimizerSettings& optimizer_settings,
    const App::Optimizer& optimizer,
    const App::Solver& solver,
    const App::LevelSet& geometry,
    const std::vector<ObjectivePoint>& history,
    const std::string& failure_message,
    double total_wall_seconds)
{
    const double length = solver_settings.inletLength
        + solver_settings.designLength + solver_settings.outletLength;
    mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
        solver_settings.nx, solver_settings.ny,
        mfem::Element::QUADRILATERAL, true,
        length, solver_settings.sy);
    mfem::H1_FECollection collection(1, mesh.Dimension());
    mfem::FiniteElementSpace fes(&mesh, &collection);
    if (geometry.design.Size() != fes.GetTrueVSize()
        || geometry.phi.Size() != fes.GetTrueVSize()) {
        return false;
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << std::setprecision(17);

    const App::FrequencyResponse& response = solver.frequencyResponse();
    const TransferFunctionFit transfer_fit = fit_transfer_function(
        response, optimizer_settings.frequencyBands);
    const App::SolverPerformance& solver_performance = solver.performance();
    const App::OptimizerPerformance& optimizer_performance =
        optimizer.performance();
    const double initial_worst = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : std::max(history.front().pass, history.front().stop);
    const double final_pass = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : optimizer.get_pass_objective();
    const double final_stop = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : optimizer.get_stop_objective();
    const double final_worst = std::max(final_pass, final_stop);
    const double initial_forward_pair_seconds = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : history.front().elapsedSeconds;
    const double average_iteration_seconds = history.size() < 2
        ? std::numeric_limits<double>::quiet_NaN()
        : (history.back().elapsedSeconds - history.front().elapsedSeconds)
            / std::max(1, history.back().iteration);
    const int report_time_steps = solver_settings.duration > 0.0
            && solver_settings.dt > 0.0
        ? std::max(1, static_cast<int>(std::llround(
            solver_settings.duration / solver_settings.dt)))
        : 1;

    const char* paper_case = high_pass
        ? "High-pass filter, b = 1e-3, r1 = r2 = 7"
        : "Low-pass filter, b = 1e-2";
    const char* paper_figure = high_pass ? "11(a,d)" : "6(a,d)";
    const int paper_page = high_pass ? 18 : 13;
    const double paper_final_pass = high_pass ? 13.7815 : 5.06428;
    const double paper_final_stop = high_pass ? 11.6695 : 4.99466;
    const bool has_paper_reference =
        std::string(mode).rfind("high-pass-20db", 0) != 0;

    output << "{\n"
           << "  \"schema_version\": 5,\n"
           << "  \"mode\": \"" << mode << "\",\n"
           << "  \"gate_passed\": " << (gate_passed ? "true" : "false") << ",\n"
           << "  \"optimizer_status\": \""
           << status_name(optimizer.get_status()) << "\",\n"
           << "  \"failure_message\": " << std::quoted(failure_message) << ",\n"
           << "  \"settings\": {\n"
           << "    \"nx\": " << solver_settings.nx << ",\n"
           << "    \"ny\": " << solver_settings.ny << ",\n"
           << "    \"inlet_length_m\": " << solver_settings.inletLength << ",\n"
           << "    \"design_length_m\": " << solver_settings.designLength << ",\n"
           << "    \"outlet_length_m\": " << solver_settings.outletLength << ",\n"
           << "    \"height_m\": " << solver_settings.sy << ",\n"
           << "    \"duration_s\": " << solver_settings.duration << ",\n"
           << "    \"dt_s\": " << solver_settings.dt << ",\n"
           << "    \"linear_solver\": \""
           << (solver_settings.linearSolveMethod
                    == App::LinearSolveMethod::mumps ? "mumps" : "fgmres")
           << "\",\n"
           << "    \"mpi_ranks\": " << solver_performance.mpiRanks << ",\n"
           << "    \"filter_radius_m\": " << solver_settings.filterRadius << ",\n"
           << "    \"maximum_iterations\": " << optimizer_settings.maxIterations << "\n"
           << "  },\n"
           << "  \"runtime\": {\n"
           << "    \"optimizer_wall_seconds\": ";
    write_number(output, total_wall_seconds);
    output << ",\n    \"initial_reference_and_design_seconds\": ";
    write_number(output, initial_forward_pair_seconds);
    output << ",\n    \"average_iteration_seconds\": ";
    write_number(output, average_iteration_seconds);
    output << ",\n    \"solver_phases\": {\n"
           << "      \"mesh_setup_seconds\": "
           << solver_performance.meshSetupSeconds << ",\n"
           << "      \"level_set_smoothing_seconds\": "
           << solver_performance.levelSetSmoothingSeconds << ",\n"
           << "      \"assembly_seconds\": "
           << solver_performance.assemblySeconds << ",\n"
           << "      \"reference_transient_seconds\": "
           << solver_performance.referenceTransientSeconds << ",\n"
           << "      \"designed_transient_seconds\": "
           << solver_performance.designedTransientSeconds << ",\n"
           << "      \"designed_time_step_seconds\": "
           << solver_performance.designedTransientSeconds
                / report_time_steps
           << ",\n"
           << "      \"fourier_seconds\": "
           << solver_performance.fourierSeconds << ",\n"
           << "      \"pass_adjoint_seconds\": "
           << solver_performance.passAdjointSeconds << ",\n"
           << "      \"stop_adjoint_seconds\": "
           << solver_performance.stopAdjointSeconds << ",\n"
           << "      \"cut_differentiation_seconds\": "
           << solver_performance.cutDifferentiationSeconds << ",\n"
           << "      \"filter_adjoint_seconds\": "
           << solver_performance.filterAdjointSeconds << ",\n"
           << "      \"mumps_initial_factorization_seconds\": "
           << solver_performance.mumpsInitialFactorizationSeconds << ",\n"
           << "      \"mumps_effective_factorization_seconds\": "
           << solver_performance.mumpsEffectiveFactorizationSeconds << ",\n"
           << "      \"mumps_solve_seconds\": "
           << solver_performance.mumpsSolveSeconds << ",\n"
           << "      \"maximum_forward_residual\": "
           << solver_performance.maximumForwardResidual << ",\n"
           << "      \"forward_fgmres_solves\": "
           << solver_performance.forwardFgmresSolves << ",\n"
           << "      \"forward_fgmres_iterations\": "
           << solver_performance.forwardFgmresIterations << ",\n"
           << "      \"average_forward_fgmres_iterations\": "
           << static_cast<double>(solver_performance.forwardFgmresIterations)
                / std::max(1, solver_performance.forwardFgmresSolves)
           << ",\n"
           << "      \"maximum_forward_fgmres_iterations\": "
           << solver_performance.maximumForwardFgmresIterations << ",\n"
           << "      \"adjoint_fgmres_solves\": "
           << solver_performance.adjointFgmresSolves << ",\n"
           << "      \"adjoint_fgmres_iterations\": "
           << solver_performance.adjointFgmresIterations << ",\n"
           << "      \"average_adjoint_fgmres_iterations\": "
           << static_cast<double>(solver_performance.adjointFgmresIterations)
                / std::max(1, solver_performance.adjointFgmresSolves)
           << ",\n"
           << "      \"maximum_adjoint_fgmres_iterations\": "
           << solver_performance.maximumAdjointFgmresIterations << ",\n"
           << "      \"cut_elements\": "
           << solver_performance.cutElements << ",\n"
           << "      \"differentiated_local_dofs\": "
           << solver_performance.differentiatedDofs << "\n"
           << "    },\n"
           << "    \"optimizer_phases\": {\n"
           << "      \"optimizer_seconds\": "
           << optimizer_performance.optimizerSeconds << ",\n"
           << "      \"forward_callback_seconds\": "
           << optimizer_performance.forwardCallbackSeconds << ",\n"
           << "      \"gradient_callback_seconds\": "
           << optimizer_performance.gradientCallbackSeconds << ",\n"
           << "      \"forward_callbacks\": "
           << optimizer_performance.forwardCallbacks << ",\n"
           << "      \"gradient_callbacks\": "
           << optimizer_performance.gradientCallbacks << ",\n"
           << "      \"optimizer_bookkeeping_seconds\": "
           << std::max(0.0,
                optimizer_performance.optimizerSeconds
                    - optimizer_performance.forwardCallbackSeconds
                    - optimizer_performance.gradientCallbackSeconds)
           << "\n"
           << "    },\n"
           << "    \"paper_wall_seconds\": null,\n"
           << "    \"paper_runtime_note\": \"Not reported in the paper.\"\n"
           << "  },\n";
    if (has_paper_reference) {
        output << "  \"paper_reference\": {\n"
               << "    \"case\": " << std::quoted(paper_case) << ",\n"
               << "    \"figure\": " << std::quoted(paper_figure) << ",\n"
               << "    \"page\": " << paper_page << ",\n"
               << "    \"settings\": {\n"
               << "      \"nx\": 250,\n"
               << "      \"ny\": 50,\n"
               << "      \"duration_s\": 0.02,\n"
               << "      \"dt_s\": 0.00002,\n"
               << "      \"maximum_iterations\": 400\n"
               << "    },\n"
               << "    \"objectives\": {\n"
               << "      \"final_pass\": " << paper_final_pass << ",\n"
               << "      \"final_stop\": " << paper_final_stop << "\n"
               << "    },\n"
               << "    \"optimizer_wall_seconds\": null\n"
               << "  },\n";
    }
    else {
        output << "  \"paper_reference\": null,\n";
    }
    output << "  \"objectives\": {\n"
           << "    \"initial_worst\": ";
    write_number(output, initial_worst);
    output << ",\n    \"final_pass\": ";
    write_number(output, final_pass);
    output << ",\n    \"final_stop\": ";
    write_number(output, final_stop);
    output << ",\n    \"final_worst\": ";
    write_number(output, final_worst);
    output << "\n  },\n";

    output << "  \"history\": [\n";
    for (std::size_t i = 0; i < history.size(); ++i) {
        const ObjectivePoint& point = history[i];
        output << "    {\"iteration\": " << point.iteration
               << ", \"pass\": ";
        write_number(output, point.pass);
        output << ", \"stop\": ";
        write_number(output, point.stop);
        output << ", \"elapsed_seconds\": ";
        write_number(output, point.elapsedSeconds);
        output << "}" << (i + 1 == history.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"bands\": [\n";
    for (std::size_t i = 0; i < optimizer_settings.frequencyBands.size(); ++i) {
        const App::FrequencyBand& band = optimizer_settings.frequencyBands[i];
        output << "    {\"type\": \""
               << (band.type == App::FrequencyBandType::pass ? "pass" : "stop")
               << "\", \"start_hz\": " << band.startHz
               << ", \"end_hz\": " << band.endHz
               << ", \"target_transmission\": " << band.targetTransmission
               << "}" << (i + 1 == optimizer_settings.frequencyBands.size()
                    ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"frequency_response\": [\n";
    for (std::size_t bin = 0; bin < response.frequency.size(); ++bin) {
        const bool valid = bin < response.valid.size()
            && bin < response.outlet.size()
            && bin < response.reference.size()
            && response.valid[bin] != 0;
        output << "    {\"frequency_hz\": " << response.frequency[bin]
               << ", \"valid\": " << (valid ? "true" : "false")
               << ", \"transmission\": ";
        const std::complex<double> transfer = valid
            ? response.outlet[bin] / response.reference[bin]
            : std::complex<double>(
                std::numeric_limits<double>::quiet_NaN(), 0.0);
        write_number(output, std::abs(transfer));
        output << ", \"phase_rad\": ";
        write_number(output, valid
            ? std::arg(transfer)
            : std::numeric_limits<double>::quiet_NaN());
        output << "}" << (bin + 1 == response.frequency.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"transfer_function_fit\": ";
    if (transfer_fit.order == 0) {
        output << "null,\n";
    }
    else {
        output << "{\n"
               << "    \"model\": \"continuous rational frequency-domain least squares\",\n"
               << "    \"order\": " << transfer_fit.order << ",\n"
               << "    \"frequency_scale_hz\": "
               << transfer_fit.frequencyScaleHz << ",\n"
               << "    \"rmse_db\": " << transfer_fit.rmseDb << ",\n"
               << "    \"numerator\": [";
        for (std::size_t i = 0; i < transfer_fit.numerator.size(); ++i) {
            output << transfer_fit.numerator[i]
                   << (i + 1 == transfer_fit.numerator.size() ? "" : ", ");
        }
        output << "],\n    \"denominator\": [";
        for (std::size_t i = 0; i < transfer_fit.denominator.size(); ++i) {
            output << transfer_fit.denominator[i]
                   << (i + 1 == transfer_fit.denominator.size() ? "" : ", ");
        }
        output << "],\n    \"samples\": [\n";
        for (std::size_t i = 0; i < transfer_fit.frequency.size(); ++i) {
            output << "      {\"frequency_hz\": " << transfer_fit.frequency[i]
                   << ", \"transmission_db\": "
                   << transfer_fit.transmissionDb[i] << "}"
                   << (i + 1 == transfer_fit.frequency.size() ? "\n" : ",\n");
        }
        output << "    ]\n  },\n";
    }

    std::vector<unsigned char> active(fes.GetTrueVSize(), 0);
    for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
        active[geometry.activeDesignDofs[i]] = 1;
    }
    output << "  \"geometry\": {\n"
           << "    \"positive_phi_is_solid\": true,\n"
           << "    \"points\": [\n";
    mfem::Array<int> vertex_dofs;
    for (int vertex = 0; vertex < mesh.GetNV(); ++vertex) {
        fes.GetVertexDofs(vertex, vertex_dofs);
        if (vertex_dofs.Size() != 1) {
            return false;
        }
        const int encoded_dof = vertex_dofs[0];
        const int dof = encoded_dof >= 0 ? encoded_dof : -1 - encoded_dof;
        const double* position = mesh.GetVertex(vertex);
        output << "      {\"x\": " << position[0]
               << ", \"y\": " << position[1]
               << ", \"design\": " << geometry.design[dof]
               << ", \"phi\": " << geometry.phi[dof]
               << ", \"active\": " << (active[dof] ? "true" : "false")
               << ", \"solid\": "
               << (geometry.phi[dof] >= 0.0 ? "true" : "false")
               << "}" << (vertex + 1 == mesh.GetNV() ? "\n" : ",\n");
    }
    output << "    ]\n"
           << "  }\n"
           << "}\n";
    return static_cast<bool>(output);
}

bool write_response_bundle(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& response_path)
{
    zip_t* archive = zip_open(
        archive_path.string().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (archive == nullptr) {
        return false;
    }

    const auto add_file = [&](const char* name,
                              const std::filesystem::path& source) {
        if (zip_entry_open(archive, name) != 0) {
            return false;
        }
        const bool written =
            zip_entry_fwrite(archive, source.string().c_str()) == 0;
        return zip_entry_close(archive) == 0 && written;
    };
    const bool written = add_file("response.json", response_path)
        && add_file("metamaterial_response.py",
                    "scripts/metamaterial_response.py")
        && add_file("Metamaterial.m", "scripts/Metamaterial.m");
    zip_close(archive);
    return written;
}

} // namespace

int main(int argc, char** argv)
{
    bool paper_mode = false;
    bool high_pass = false;
    bool high_pass_20db = false;
    bool desperado = false;
    bool use_mumps = false;
    int requested_iterations = 0;
    for (int argument = 1; argument < argc; ++argument) {
        const std::string option = argv[argument];
        if (option == "--paper") {
            paper_mode = true;
        }
        else if (option == "--high-pass") {
            paper_mode = true;
            high_pass = true;
        }
        else if (option == "--high-pass-20db") {
            paper_mode = true;
            high_pass_20db = true;
        }
        else if (option == "--desperado") {
            paper_mode = true;
            desperado = true;
        }
        else if (option == "--mumps") {
            use_mumps = true;
        }
        else if (option == "--iterations" && argument + 1 < argc) {
            try {
                requested_iterations = std::stoi(argv[++argument]);
            }
            catch (...) {
                requested_iterations = 0;
            }
            if (requested_iterations <= 0) {
                std::cerr << "--iterations requires a positive integer.\n";
                return 1;
            }
        }
        else {
            std::cerr
                << "Usage: paper_optimizer_miniapp "
                   "[--paper|--high-pass|--high-pass-20db|--desperado] "
                   "[--iterations N] [--mumps]\n";
            return 1;
        }
    }
#if !METAMATERIAL_USE_MPI
    if (use_mumps) {
        std::cerr << "--mumps requires the parallel-cpu build.\n";
        return 1;
    }
#endif

    int rank = 0;
#if METAMATERIAL_USE_MPI
    int provided = 0;
    mfem::Mpi::Init(argc, argv, MPI_THREAD_SERIALIZED, &provided);
    mfem::Hypre::Init();
    if (provided < MPI_THREAD_SERIALIZED) {
        std::cerr << "The miniapp needs MPI_THREAD_SERIALIZED.\n";
        return 1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
    std::signal(SIGINT, handle_interrupt);

    int exit_code = 0;
    {
        mfem::Device device(METAMATERIAL_USE_CUDA ? "cuda" : "cpu");
        device.Print();

        App::SolverSettings solver_settings;
        App::OptimizerSettings optimizer_settings;
        solver_settings.linearSolveMethod = use_mumps
            ? App::LinearSolveMethod::mumps
            : App::LinearSolveMethod::fgmres;
        if (desperado) {
            auto& physics = std::get<App::VibroacousticSettings>(
                solver_settings.physics);
            physics.rho_s = 1340.0f;
            physics.poisson_ratio = 0.36f;
            physics.youngs_modulus = 2.5e9f;
            solver_settings.nx = 100;
            solver_settings.inletLength = 0.07;
            solver_settings.designLength = 0.20;
            solver_settings.outletLength = 0.07;
            optimizer_settings.frequencyMin = 60.0f;
            optimizer_settings.frequencyMax = 4000.0f;
            optimizer_settings.maxIterations = 150;
            optimizer_settings.frequencyBands = {
                {App::FrequencyBandType::stop, 60.0, 600.0, 1.0e-2},
                {App::FrequencyBandType::pass, 600.0, 4000.0, 1.0}
            };
        }
        else if (high_pass_20db) {
            solver_settings.duration = 0.05;
            optimizer_settings.frequencyMin = 60.0f;
            optimizer_settings.frequencyMax = 4000.0f;
            optimizer_settings.frequencyBands = {
                {App::FrequencyBandType::stop, 60.0, 1000.0, 0.1},
                {App::FrequencyBandType::pass, 1000.0, 4000.0, 1.0}
            };
        }
        else if (high_pass) {
            solver_settings.initialPatternX = 7;
            solver_settings.initialPatternY = 7;
            optimizer_settings.frequencyMin = 1000.0f;
            optimizer_settings.frequencyMax = 4000.0f;
            optimizer_settings.maxIterations = 400;
            optimizer_settings.frequencyBands = {
                {App::FrequencyBandType::stop, 1000.0,
                    std::nextafter(2500.0, 0.0), 1.0e-3},
                {App::FrequencyBandType::pass, 2500.0, 4000.0, 1.0}
            };
        }
        if (!paper_mode) {
            solver_settings.nx = 100;
            solver_settings.ny = 20;
            solver_settings.duration = 0.008;
            solver_settings.dt = 0.0001;
            optimizer_settings.maxIterations = 20;
        }
        if (requested_iterations > 0) {
            optimizer_settings.maxIterations = requested_iterations;
        }

        App::LevelSet geometry;
        App::SolverResult result;
        App::Optimizer* optimizer = nullptr;
        std::vector<ObjectivePoint> history;
        std::string failure_message;
        auto started_at = std::chrono::steady_clock::now();
        App::LogFunction log = [&](App::LogLevel level, std::string message) {
            if (level == App::LogLevel::Error) {
                if (failure_message.empty()) {
                    failure_message = message;
                }
                std::cerr << message << '\n';
            }
            else {
                std::cout << message << '\n';
            }
            if (optimizer != nullptr
                && (message.rfind("Initial pass/stop objectives:", 0) == 0
                    || message.rfind("Completed optimizer iteration ", 0) == 0)) {
                const ObjectivePoint point{
                    optimizer->get_iteration(),
                    optimizer->get_pass_objective(),
                    optimizer->get_stop_objective(),
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - started_at).count()};
                if (!history.empty() && history.back().iteration == point.iteration) {
                    history.back() = point;
                }
                else {
                    history.push_back(point);
                }
            }
        };

        App::Solver solver(
            solver_settings,
            geometry,
            result,
            log);
        if (rank != 0) {
            solver.parallelWorkerLoop();
        }
        else {
            try {
            App::Optimizer optimizer_instance(
                optimizer_settings, solver, geometry, log);
            optimizer = &optimizer_instance;
            started_at = std::chrono::steady_clock::now();
            std::atomic<bool> optimizer_finished{false};
            std::thread interrupt_watcher([&] {
                while (!optimizer_finished.load(std::memory_order_relaxed)) {
                    if (interrupt_requested != 0) {
                        optimizer->request_cancel();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
            try {
                optimizer->run();
            }
            catch (...) {
                optimizer_finished.store(true, std::memory_order_relaxed);
                interrupt_watcher.join();
                throw;
            }
            optimizer_finished.store(true, std::memory_order_relaxed);
            interrupt_watcher.join();
            const double total_wall_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started_at).count();

            bool bounds_hold = true;
            for (int i = 0; i < geometry.activeDesignDofs.Size(); ++i) {
                const double value = geometry.design[geometry.activeDesignDofs[i]];
                bounds_hold = bounds_hold
                    && std::isfinite(value) && value >= 0.0 && value <= 1.0;
            }
            const double initial_worst = history.empty()
                ? std::numeric_limits<double>::quiet_NaN()
                : std::max(history.front().pass, history.front().stop);
            const double final_worst = std::max(
                optimizer->get_pass_objective(), optimizer->get_stop_objective());
            const bool optimization_completed =
                optimizer->get_status() == App::OptimizerStatus::Converged
                || (optimizer->get_status()
                        == App::OptimizerStatus::MaximumIterations
                    && optimizer->get_iteration()
                        == optimizer_settings.maxIterations);
            const bool gate_passed =
                optimization_completed
                && optimizer->is_exportable()
                && bounds_hold
                && std::isfinite(initial_worst)
                && std::isfinite(final_worst)
                && final_worst < initial_worst;

            const std::string mode = std::string(
                desperado ? "desperado"
                    : high_pass_20db ? "high-pass-20db"
                    : high_pass ? "high-pass"
                    : paper_mode ? "paper" : "quick")
                + (use_mumps ? "-mumps" : "");
            const std::filesystem::path output_path =
                std::filesystem::path("test-results")
                / "paper-optimizer" / (mode + ".json");
            if (!write_report_data(
                    output_path,
                    mode.c_str(),
                    high_pass,
                    gate_passed,
                    solver_settings,
                    optimizer_settings,
                    *optimizer,
                    solver,
                    geometry,
                    history,
                    failure_message,
                    total_wall_seconds)) {
                std::cerr << "Could not write the optimizer report data.\n";
                exit_code = 1;
            }
            else {
                std::cout << "Wrote " << std::filesystem::absolute(output_path)
                          << '\n';
                const std::filesystem::path archive_path =
                    output_path.parent_path() / (mode + "-bundle.zip");
                if (!write_response_bundle(archive_path, output_path)) {
                    std::cerr << "Could not write the response ZIP bundle.\n";
                    exit_code = 1;
                }
                else {
                    std::cout << "Wrote "
                              << std::filesystem::absolute(archive_path) << '\n';
                }
            }
            if (interrupt_requested != 0
                && optimizer->get_status() == App::OptimizerStatus::Cancelled) {
                std::cerr << "Optimization interrupted after the latest completed design.\n";
                exit_code = 130;
            }
            else if (!gate_passed) {
                std::cerr << "The optimizer did not improve the app-like objective.\n";
                exit_code = 1;
            }
            }
            catch (const std::exception& error) {
                std::cerr << "Optimizer miniapp failed: "
                          << error.what() << '\n';
                exit_code = 1;
            }
            catch (...) {
                std::cerr << "Optimizer miniapp failed with an unknown error.\n";
                exit_code = 1;
            }
#if METAMATERIAL_USE_MPI
            solver.shutdownParallelWorkers();
#endif
        }
    }

    return exit_code;
}
