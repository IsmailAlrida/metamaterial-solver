#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "mpi.h"

#include "optimizer.hpp"
#include "solver.hpp"

namespace {

struct ObjectivePoint {
    int iteration;
    double pass;
    double stop;
    double elapsedSeconds;
};

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
    bool gate_passed,
    const App::SolverSettings& solver_settings,
    const App::OptimizerSettings& optimizer_settings,
    const App::Optimizer& optimizer,
    const App::Solver& solver,
    const App::LevelSet& geometry,
    const std::vector<ObjectivePoint>& history,
    double total_wall_seconds)
{
    if (!geometry.phi || geometry.phi->FESpace() == nullptr) {
        return false;
    }
    const mfem::FiniteElementSpace& fes = *geometry.phi->FESpace();
    const mfem::Mesh& mesh = *fes.GetMesh();
    if (mesh.Dimension() != 2 || geometry.design.Size() != fes.GetTrueVSize()) {
        return false;
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << std::setprecision(17);

    const App::FrequencyResponse& response = solver.frequencyResponse();
    const double initial_worst = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : std::max(history.front().pass, history.front().stop);
    const double final_worst = std::max(
        optimizer.get_pass_objective(), optimizer.get_stop_objective());
    const double initial_forward_pair_seconds = history.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : history.front().elapsedSeconds;
    const double average_iteration_seconds = history.size() < 2
        ? std::numeric_limits<double>::quiet_NaN()
        : (history.back().elapsedSeconds - history.front().elapsedSeconds)
            / std::max(1, history.back().iteration);

    output << "{\n"
           << "  \"schema_version\": 2,\n"
           << "  \"mode\": \"" << mode << "\",\n"
           << "  \"gate_passed\": " << (gate_passed ? "true" : "false") << ",\n"
           << "  \"optimizer_status\": \""
           << status_name(optimizer.get_status()) << "\",\n"
           << "  \"settings\": {\n"
           << "    \"nx\": " << solver_settings.nx << ",\n"
           << "    \"ny\": " << solver_settings.ny << ",\n"
           << "    \"inlet_length_m\": " << solver_settings.inletLength << ",\n"
           << "    \"design_length_m\": " << solver_settings.designLength << ",\n"
           << "    \"outlet_length_m\": " << solver_settings.outletLength << ",\n"
           << "    \"height_m\": " << solver_settings.sy << ",\n"
           << "    \"duration_s\": " << solver_settings.duration << ",\n"
           << "    \"dt_s\": " << solver_settings.dt << ",\n"
           << "    \"filter_radius_m\": " << optimizer_settings.filterRadius << ",\n"
           << "    \"maximum_iterations\": " << optimizer_settings.maxIterations << "\n"
           << "  },\n"
           << "  \"runtime\": {\n"
           << "    \"optimizer_wall_seconds\": ";
    write_number(output, total_wall_seconds);
    output << ",\n    \"initial_reference_and_design_seconds\": ";
    write_number(output, initial_forward_pair_seconds);
    output << ",\n    \"average_iteration_seconds\": ";
    write_number(output, average_iteration_seconds);
    output << ",\n    \"paper_wall_seconds\": null,\n"
           << "    \"paper_runtime_note\": \"Not reported in the paper.\"\n"
           << "  },\n"
           << "  \"paper_reference\": {\n"
           << "    \"case\": \"Low-pass filter, b = 1e-2\",\n"
           << "    \"figure\": \"6(a,d)\",\n"
           << "    \"page\": 13,\n"
           << "    \"settings\": {\n"
           << "      \"nx\": 250,\n"
           << "      \"ny\": 50,\n"
           << "      \"duration_s\": 0.02,\n"
           << "      \"dt_s\": 0.00002,\n"
           << "      \"maximum_iterations\": 400\n"
           << "    },\n"
           << "    \"objectives\": {\n"
           << "      \"final_pass\": 5.06428,\n"
           << "      \"final_stop\": 4.99466\n"
           << "    },\n"
           << "    \"optimizer_wall_seconds\": null\n"
           << "  },\n"
           << "  \"objectives\": {\n"
           << "    \"initial_worst\": ";
    write_number(output, initial_worst);
    output << ",\n    \"final_pass\": ";
    write_number(output, optimizer.get_pass_objective());
    output << ",\n    \"final_stop\": ";
    write_number(output, optimizer.get_stop_objective());
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
        write_number(output, valid
            ? std::abs(response.outlet[bin]) / std::abs(response.reference[bin])
            : std::numeric_limits<double>::quiet_NaN());
        output << "}" << (bin + 1 == response.frequency.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

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
               << ", \"phi\": " << (*geometry.phi)[dof]
               << ", \"active\": " << (active[dof] ? "true" : "false")
               << ", \"solid\": "
               << ((*geometry.phi)[dof] >= 0.0 ? "true" : "false")
               << "}" << (vertex + 1 == mesh.GetNV() ? "\n" : ",\n");
    }
    output << "    ]\n"
           << "  }\n"
           << "}\n";
    return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv)
{
    const bool paper_mode = argc > 1 && std::string(argv[1]) == "--paper";
    if (argc > 1 && !paper_mode) {
        std::cerr << "Usage: paper_optimizer_miniapp [--paper]\n";
        return 1;
    }

    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided)
            != MPI_SUCCESS) {
        std::cerr << "The miniapp could not initialize MPI.\n";
        return 1;
    }
    if (provided < MPI_THREAD_SERIALIZED) {
        std::cerr << "The miniapp needs MPI_THREAD_SERIALIZED.\n";
        MPI_Finalize();
        return 1;
    }

    int exit_code = 0;
    {
        App::SolverSettings solver_settings;
        App::OptimizerSettings optimizer_settings;
        if (!paper_mode) {
            solver_settings.nx = 100;
            solver_settings.ny = 20;
            solver_settings.duration = 0.008;
            solver_settings.dt = 0.0001;
            optimizer_settings.maxIterations = 20;
        }

        App::LevelSet geometry;
        App::SolverResult result;
        App::Optimizer* optimizer = nullptr;
        std::vector<ObjectivePoint> history;
        auto started_at = std::chrono::steady_clock::now();
        App::LogFunction log = [&](App::LogLevel level, std::string message) {
            if (level == App::LogLevel::Error) {
                std::cerr << message << '\n';
            }
            else {
                std::cout << message << '\n';
            }
            if (optimizer != nullptr
                && (message.rfind("Initial pass/stop objectives:", 0) == 0
                    || message.rfind("Completed optimization iteration ", 0) == 0)) {
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
            solver_settings, optimizer_settings, geometry, result, log);
        App::Optimizer optimizer_instance(
            optimizer_settings, solver, geometry, result, log);
        optimizer = &optimizer_instance;
        started_at = std::chrono::steady_clock::now();
        optimizer->run();
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
        const bool gate_passed =
            optimizer->get_status() == App::OptimizerStatus::MaximumIterations
            && optimizer->get_iteration() == optimizer_settings.maxIterations
            && optimizer->is_exportable()
            && bounds_hold
            && std::isfinite(initial_worst)
            && std::isfinite(final_worst)
            && final_worst < initial_worst;

        const char* mode = paper_mode ? "paper" : "quick";
        const std::filesystem::path output_path =
            std::filesystem::path("test-results")
            / "paper-optimizer" / (std::string(mode) + ".json");
        if (!write_report_data(
                output_path,
                mode,
                gate_passed,
                solver_settings,
                optimizer_settings,
                *optimizer,
                solver,
                geometry,
                history,
                total_wall_seconds)) {
            std::cerr << "Could not write the optimizer report data.\n";
            exit_code = 1;
        }
        else {
            std::cout << "Wrote " << std::filesystem::absolute(output_path)
                      << '\n';
        }
        if (!gate_passed) {
            std::cerr << "The optimizer did not improve the app-like objective.\n";
            exit_code = 1;
        }
    }

    MPI_Finalize();
    return exit_code;
}
