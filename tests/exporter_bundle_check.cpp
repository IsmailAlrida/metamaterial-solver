#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "exporter.hpp"
#include "zip.h"

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / ("metamaterial-exporter-check-" + std::to_string(unique));

    try {
        std::filesystem::create_directories(directory);

        App::AppSettings settings;
        App::SolverResult result;
        mfem::Mesh mesh = mfem::Mesh::MakeCartesian2D(
            2, 1, mfem::Element::QUADRILATERAL, true, 0.5, 0.1);
        mfem::H1_FECollection collection(1, mesh.Dimension());
        mfem::FiniteElementSpace space(&mesh, &collection);
        App::LevelSet geometry(space);
        for (int dof = 0; dof < geometry.design.Size(); ++dof) {
            geometry.design[dof] = dof % 2 == 0 ? 0.25 : 0.75;
        }
        geometry.phi->SetFromTrueDofs(geometry.design);

        auto signal = std::make_shared<App::SignalTD>();
        signal->size = 2;
        signal->time = {0.0, 0.001};
        signal->amplitude = {0.25, -0.5};
        result.inletPressure = signal;
        result.outletPressure = signal;
        result.referenceOutletPressure = signal;

        auto response = std::make_shared<App::SignalFFT>();
        response->size = 2;
        response->frequency = {0.0f, 1000.0f};
        response->valid = {1, 1};
        response->referenceAmplitude = {1.0f, 1.0f};
        response->amplitude = {1.0f, 0.1f};
        response->transmission = {1.0f, 0.1f};
        response->attenuationDB = {0.0f, -20.0f};
        response->phase = {0.0f, 0.5f};
        result.materialImpulseResponse = response;
        result.success = 1;
        result.stateSize = 8;
        result.displacementSize = 4;
        result.pressureSize = 4;
        result.pressureOffset = 4;
        result.timeSteps = 1;
        result.dt = 0.001;
        result.solidInfillFraction.store(0.375, std::memory_order_release);
        result.residualNorms.push_back({1.0e-12, 2.0e-12, 3.0e-12});
        result.U.emplace_back(8);
        result.U.back() = 123.0;

        std::vector<std::string> logs;
        App::LogFunction log = [&](App::LogLevel, std::string message) {
            logs.push_back(std::move(message));
        };
        App::Exporter exporter(settings, result, geometry, log);

        require(exporter.exportRunData(
                    directory, App::OptimizerStatus::Paused,
                    7, 1.5, 2.5, 2.5),
                "First bundle export failed.");
        std::vector<std::filesystem::path> archives;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".zip") {
                archives.push_back(entry.path());
            }
        }
        require(archives.size() == 1, "First export did not create one ZIP.");
        const std::filesystem::path first_archive = archives.front();
        const auto first_size = std::filesystem::file_size(first_archive);

        require(exporter.exportRunData(
                    directory, App::OptimizerStatus::Paused,
                    7, 1.5, 2.5, 2.5),
                "Second bundle export failed.");
        archives.clear();
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            require(entry.path().extension() != ".tmp",
                    "A temporary archive was left behind.");
            if (entry.path().extension() == ".zip") {
                archives.push_back(entry.path());
            }
        }
        require(archives.size() == 2, "Collision-safe export overwrote a ZIP.");
        require(std::filesystem::file_size(first_archive) == first_size,
                "The first ZIP changed during the second export.");

        zip_t* archive = zip_open(
            archives.back().string().c_str(), 0, 'r');
        require(archive != nullptr, "Could not reopen the exported ZIP.");
        std::set<std::string> names;
        std::string json;
        const ssize_t count = zip_entries_total(archive);
        for (ssize_t index = 0; index < count; ++index) {
            require(zip_entry_openbyindex(archive, index) == 0,
                    "Could not open a ZIP entry.");
            const std::string name = zip_entry_name(archive);
            names.insert(name);
            if (name == "response.json") {
                void* data = nullptr;
                size_t size = 0;
                require(zip_entry_read(archive, &data, &size) >= 0,
                        "Could not read response.json.");
                json.assign(static_cast<const char*>(data), size);
                std::free(data);
            }
            require(zip_entry_close(archive) == 0,
                    "Could not close a ZIP entry.");
        }
        zip_close(archive);

        require(names == std::set<std::string>{
                    "Metamaterial.m",
                    "metamaterial_response.py",
                    "response.json"},
                "The bundle does not contain exactly the expected files.");
        require(json.find("\"frequency_bands\"") != std::string::npos
                && json.find("\"frequency_hz\":1000") != std::string::npos
                && json.find("\"attenuation_db\":-20") != std::string::npos
                && json.find("\"reference_outlet\"") != std::string::npos
                && json.find("\"equilibrium\":") != std::string::npos
                && json.find("\"solid_infill_fraction\":0.375")
                    != std::string::npos
                && json.find("\"active_design_mask\"") != std::string::npos,
                "response.json is missing required run data.");
        require(json.find("\"U\"") == std::string::npos,
                "The full Newmark history was serialized.");

        std::filesystem::remove_all(directory);
        std::cout << "exporter bundle check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << "exporter bundle check failed: "
                  << error.what() << '\n';
        return 1;
    }
}
