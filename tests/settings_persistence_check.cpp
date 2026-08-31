#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

#include "settings.hpp"

int main()
{
    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / ("metamaterial-settings-check-" + std::to_string(unique));
    const std::filesystem::path path = directory / "settings.json";

    App::AppSettings written;
    written.solverSettings.nx = 137;
    written.solverSettings.inletLength = 0.07;
    auto& material = std::get<App::VibroacousticSettings>(
        written.solverSettings.physics);
    material.rho_s = 1340.0f;
    material.zeta = 0.02f;
    material.f1 = 60.0f;
    material.f2 = 600.0f;
    written.optSettings.frequencyBands = {
        {App::FrequencyBandType::stop, 60.0, 600.0, 1.0e-2},
        {App::FrequencyBandType::pass, 600.0, 4000.0, 1.0}
    };

    std::string error;
    assert(App::saveAppSettings(written, path, &error));

    App::AppSettings loaded;
    assert(App::loadAppSettings(loaded, path, &error));
    assert(loaded.solverSettings.nx == 137);
    assert(loaded.solverSettings.inletLength == 0.07);
    const auto& loaded_material = std::get<App::VibroacousticSettings>(
        loaded.solverSettings.physics);
    assert(loaded_material.rho_s == 1340.0f);
    assert(loaded_material.zeta == 0.02f);
    assert(loaded_material.f1 == 60.0f);
    assert(loaded_material.f2 == 600.0f);
    assert(loaded.optSettings.frequencyBands.size() == 2);
    assert(loaded.optSettings.frequencyBands.front().startHz == 60.0);

    std::filesystem::remove_all(directory);
}
