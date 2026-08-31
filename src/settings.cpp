#include "settings.hpp"

#include "fem/picojson.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

bool fail(std::string* error, std::string message)
{
    if (error) {
        *error = std::move(message);
    }
    return false;
}

template <typename number_t>
bool readNumber(const picojson::object& object,
                const std::string& key,
                number_t& destination,
                std::string* error)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    if (!found->second.is<double>()) {
        return fail(error, "Expected a number for " + key);
    }
    const double value = found->second.get<double>();
    if (!std::isfinite(value)) {
        return fail(error, "Expected a finite number for " + key);
    }
    if constexpr (std::is_integral_v<number_t>) {
        if (std::trunc(value) != value
            || value < static_cast<double>(std::numeric_limits<number_t>::lowest())
            || value > static_cast<double>(std::numeric_limits<number_t>::max())) {
            return fail(error, "Expected an integer for " + key);
        }
    }
    destination = static_cast<number_t>(value);
    return true;
}

bool readBool(const picojson::object& object,
              const std::string& key,
              bool& destination,
              std::string* error)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    if (!found->second.is<bool>()) {
        return fail(error, "Expected a boolean for " + key);
    }
    destination = found->second.get<bool>();
    return true;
}

bool readString(const picojson::object& object,
                const std::string& key,
                std::string& destination,
                std::string* error)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    if (!found->second.is<std::string>()) {
        return fail(error, "Expected text for " + key);
    }
    destination = found->second.get<std::string>();
    return true;
}

bool readObject(const picojson::object& parent,
                const std::string& key,
                const picojson::object*& destination,
                std::string* error)
{
    destination = nullptr;
    const auto found = parent.find(key);
    if (found == parent.end()) {
        return true;
    }
    if (!found->second.is<picojson::object>()) {
        return fail(error, "Expected an object for " + key);
    }
    destination = &found->second.get<picojson::object>();
    return true;
}

bool readNumberArray(const picojson::object& object,
                     const std::string& key,
                     std::vector<double>& destination,
                     std::string* error)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    if (!found->second.is<picojson::array>()) {
        return fail(error, "Expected an array for " + key);
    }

    std::vector<double> values;
    for (const picojson::value& value : found->second.get<picojson::array>()) {
        if (!value.is<double>() || !std::isfinite(value.get<double>())) {
            return fail(error, "Expected finite numbers in " + key);
        }
        values.push_back(value.get<double>());
    }
    destination = std::move(values);
    return true;
}

picojson::value number(double value)
{
    return picojson::value(value);
}

const char* solverDeviceName(App::SolverDevice value)
{
    switch (value) {
        case App::SolverDevice::serial: return "serial";
        case App::SolverDevice::parallel: return "parallel";
        case App::SolverDevice::serialCuda: return "serial-cuda";
        case App::SolverDevice::parallelCuda: return "parallel-cuda";
    }
    return "serial";
}

bool replaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination,
                 std::string* error)
{
#ifdef _WIN32
    if (::MoveFileExW(temporary.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fail(error, "Could not replace " + destination.string());
    }
    return true;
#else
    std::error_code file_error;
    std::filesystem::rename(temporary, destination, file_error);
    if (file_error) {
        std::filesystem::remove(temporary, file_error);
        return fail(error, "Could not replace " + destination.string());
    }
    return true;
#endif
}

} // namespace

std::filesystem::path App::defaultAppSettingsPath()
{
#ifdef _WIN32
    if (const char* app_data = std::getenv("APPDATA")) {
        return std::filesystem::path(app_data)
            / "MetamaterialLab" / "settings.json";
    }
#else
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(config_home)
            / "metamaterial-lab" / "settings.json";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home)
            / ".config" / "metamaterial-lab" / "settings.json";
    }
#endif
    return std::filesystem::current_path() / "metamaterial-settings.json";
}

bool App::loadAppSettings(AppSettings& settings,
                          const std::filesystem::path& path,
                          std::string* error)
{
    if (error) {
        error->clear();
    }
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream input(path);
    if (!input) {
        return fail(error, "Could not open " + path.string());
    }
    picojson::value document;
    const std::string parse_error = picojson::parse(document, input);
    if (!parse_error.empty()) {
        return fail(error, "Could not parse settings: " + parse_error);
    }
    if (!document.is<picojson::object>()) {
        return fail(error, "The settings root must be an object");
    }
    const picojson::object& root = document.get<picojson::object>();
    int version = 0;
    if (!readNumber(root, "version", version, error) || version != 1) {
        return fail(error, "Unsupported settings-file version");
    }

    AppSettings loaded = settings;
    const picojson::object* object = nullptr;
    if (!readObject(root, "solver", object, error)) {
        return false;
    }
    if (object) {
        SolverSettings& solver = loaded.solverSettings;
#define READ_SOLVER(name) \
        if (!readNumber(*object, #name, solver.name, error)) return false
        READ_SOLVER(nx);
        READ_SOLVER(ny);
        READ_SOLVER(nz);
        READ_SOLVER(inletLength);
        READ_SOLVER(designLength);
        READ_SOLVER(outletLength);
        READ_SOLVER(sy);
        READ_SOLVER(sz);
        READ_SOLVER(duration);
        READ_SOLVER(dt);
        READ_SOLVER(newmarkBeta);
        READ_SOLVER(newmarkGamma);
        READ_SOLVER(sourceAmplitude);
        READ_SOLVER(sourceSeed);
        READ_SOLVER(initialPatternLx);
        READ_SOLVER(initialPatternLy);
        READ_SOLVER(initialPatternX);
        READ_SOLVER(initialPatternY);
        READ_SOLVER(initialPatternBias);
        READ_SOLVER(initialPatternThreshold);
        READ_SOLVER(filterRadius);
        READ_SOLVER(cutDerivativeRelativeStep);
#undef READ_SOLVER
        if (!readString(*object, "algo", solver.algo, error)
            || !readBool(*object, "isotropicGrid", solver.isotropicGrid, error)
            || !readBool(*object, "useHannWindow", solver.useHannWindow, error)) {
            return false;
        }

        std::string device = solverDeviceName(solver.device);
        if (!readString(*object, "device", device, error)) return false;
        if (device == "serial") solver.device = SolverDevice::serial;
        else if (device == "parallel") solver.device = SolverDevice::parallel;
        else if (device == "serial-cuda") solver.device = SolverDevice::serialCuda;
        else if (device == "parallel-cuda") solver.device = SolverDevice::parallelCuda;
        else return fail(error, "Unknown solver device");

        std::string method = solver.linearSolveMethod == LinearSolveMethod::mumps
            ? "mumps" : "fgmres";
        if (!readString(*object, "linearSolveMethod", method, error)) return false;
        if (method == "fgmres") solver.linearSolveMethod = LinearSolveMethod::fgmres;
        else if (method == "mumps") solver.linearSolveMethod = LinearSolveMethod::mumps;
        else return fail(error, "Unknown linear solve method");

        const picojson::object* physics = nullptr;
        if (!readObject(*object, "physics", physics, error)) {
            return false;
        }
        if (physics) {
            std::string type = "vibroacoustic";
            if (!readString(*physics, "type", type, error)) return false;
            if (type == "electromagnetic") {
                solver.physics = ElectromagneticSettings{};
            }
            else if (type == "vibroacoustic") {
                if (!std::holds_alternative<VibroacousticSettings>(solver.physics)) {
                    solver.physics = VibroacousticSettings{};
                }
                auto& values = std::get<VibroacousticSettings>(solver.physics);
#define READ_PHYSICS(name) \
                if (!readNumber(*physics, #name, values.name, error)) return false
                READ_PHYSICS(rho_s);
                READ_PHYSICS(rho_a);
                READ_PHYSICS(c_a);
                READ_PHYSICS(youngs_modulus);
                READ_PHYSICS(poisson_ratio);
                READ_PHYSICS(zeta);
                READ_PHYSICS(f1);
                READ_PHYSICS(f2);
                READ_PHYSICS(epsilon);
#undef READ_PHYSICS
            }
            else {
                return fail(error, "Unknown physics type");
            }
        }
    }

    object = nullptr;
    if (!readObject(root, "optimizer", object, error)) {
        return false;
    }
    if (object) {
        OptimizerSettings& optimizer = loaded.optSettings;
#define READ_OPTIMIZER(name) \
        if (!readNumber(*object, #name, optimizer.name, error)) return false
        READ_OPTIMIZER(frequencyMin);
        READ_OPTIMIZER(frequencyMax);
        READ_OPTIMIZER(attenuationMinDb);
        READ_OPTIMIZER(attenuationMaxDb);
        READ_OPTIMIZER(frequencySamples);
        READ_OPTIMIZER(maxIterations);
        READ_OPTIMIZER(mmaInitialAsymptote);
        READ_OPTIMIZER(mmaDecreaseAsymptote);
        READ_OPTIMIZER(mmaIncreaseAsymptote);
        READ_OPTIMIZER(mmaConstraintPenalty);
#undef READ_OPTIMIZER
        if (!readBool(*object, "displayTargetInDb",
                      optimizer.displayTargetInDb, error)) {
            return false;
        }

        std::string objective_mode = optimizer.objectiveMode == ObjectiveMode::freeform
            ? "freeform" : "band";
        if (!readString(*object, "objectiveMode", objective_mode, error)) {
            return false;
        }
        if (objective_mode == "band") optimizer.objectiveMode = ObjectiveMode::band;
        else if (objective_mode == "freeform" || objective_mode == "painted") {
            optimizer.objectiveMode = ObjectiveMode::freeform;
        }
        else return fail(error, "Unknown objective mode");

        const auto bands = object->find("frequencyBands");
        if (bands != object->end()) {
            if (!bands->second.is<picojson::array>()) {
                return fail(error, "frequencyBands must be an array");
            }
            std::vector<FrequencyBand> loaded_bands;
            for (const picojson::value& value :
                 bands->second.get<picojson::array>()) {
                if (!value.is<picojson::object>()) {
                    return fail(error, "A frequency band must be an object");
                }
                const picojson::object& band_object = value.get<picojson::object>();
                FrequencyBand band;
                std::string type = "stop";
                if (!readString(band_object, "type", type, error)
                    || !readNumber(band_object, "startHz", band.startHz, error)
                    || !readNumber(band_object, "endHz", band.endHz, error)
                    || !readNumber(band_object, "targetTransmission",
                                   band.targetTransmission, error)) {
                    return false;
                }
                if (type == "pass") band.type = FrequencyBandType::pass;
                else if (type == "stop") band.type = FrequencyBandType::stop;
                else return fail(error, "Unknown frequency-band type");
                loaded_bands.push_back(band);
            }
            optimizer.frequencyBands = std::move(loaded_bands);
        }

        const picojson::object* freeform = nullptr;
        if (!readObject(*object, "freeformObjective", freeform, error)) {
            return false;
        }
        if (freeform == nullptr
            && !readObject(*object, "paintedObjective", freeform, error)) {
            return false;
        }
        if (freeform
            && (!readNumberArray(*freeform, "frequencyHz",
                                 optimizer.freeformObjective.frequencyHz, error)
                || !readNumberArray(*freeform, "targetTransmission",
                                     optimizer.freeformObjective.targetTransmission, error))) {
            return false;
        }
    }

    object = nullptr;
    if (!readObject(root, "ui", object, error)) {
        return false;
    }
    if (object) {
        UiSettings& ui = loaded.uiSettings;
#define READ_UI(name) \
        if (!readNumber(*object, #name, ui.name, error)) return false
        READ_UI(plotXMinHz);
        READ_UI(plotXMaxHz);
        READ_UI(plotYMinDb);
        READ_UI(plotYMaxDb);
        READ_UI(plotYMinLinear);
        READ_UI(plotYMaxLinear);
#undef READ_UI
        if (!readBool(*object, "displayInDb", ui.displayInDb, error)) {
            return false;
        }
    }

    const FreeformObjective& freeform = loaded.optSettings.freeformObjective;
    if (freeform.frequencyHz.size() != freeform.targetTransmission.size()) {
        return fail(error, "Freeform objective arrays must have equal lengths");
    }
    for (std::size_t i = 0; i < freeform.frequencyHz.size(); ++i) {
        if (freeform.frequencyHz[i] < 0.0
            || freeform.targetTransmission[i] <= 0.0
            || (i > 0 && freeform.frequencyHz[i] <= freeform.frequencyHz[i - 1])) {
            return fail(error,
                "Freeform objective frequencies must increase and targets must be positive");
        }
    }
    const UiSettings& ui = loaded.uiSettings;
    if (!(ui.plotXMinHz < ui.plotXMaxHz)
        || !(ui.plotYMinDb < ui.plotYMaxDb)
        || !(ui.plotYMinLinear < ui.plotYMaxLinear)) {
        return fail(error, "Saved UI ranges are invalid");
    }

    settings = std::move(loaded);
    return true;
}

bool App::saveAppSettings(const AppSettings& settings,
                          const std::filesystem::path& path,
                          std::string* error)
{
    if (error) {
        error->clear();
    }

    const SolverSettings& solver = settings.solverSettings;
    picojson::object solver_object;
#define WRITE_SOLVER(name) solver_object[#name] = number(solver.name)
    WRITE_SOLVER(nx);
    WRITE_SOLVER(ny);
    WRITE_SOLVER(nz);
    WRITE_SOLVER(inletLength);
    WRITE_SOLVER(designLength);
    WRITE_SOLVER(outletLength);
    WRITE_SOLVER(sy);
    WRITE_SOLVER(sz);
    WRITE_SOLVER(duration);
    WRITE_SOLVER(dt);
    WRITE_SOLVER(newmarkBeta);
    WRITE_SOLVER(newmarkGamma);
    WRITE_SOLVER(sourceAmplitude);
    WRITE_SOLVER(sourceSeed);
    WRITE_SOLVER(initialPatternLx);
    WRITE_SOLVER(initialPatternLy);
    WRITE_SOLVER(initialPatternX);
    WRITE_SOLVER(initialPatternY);
    WRITE_SOLVER(initialPatternBias);
    WRITE_SOLVER(initialPatternThreshold);
    WRITE_SOLVER(filterRadius);
    WRITE_SOLVER(cutDerivativeRelativeStep);
#undef WRITE_SOLVER
    solver_object["algo"] = picojson::value(solver.algo);
    solver_object["isotropicGrid"] = picojson::value(solver.isotropicGrid);
    solver_object["useHannWindow"] = picojson::value(solver.useHannWindow);
    solver_object["device"] = picojson::value(
        std::string(solverDeviceName(solver.device)));
    solver_object["linearSolveMethod"] = picojson::value(std::string(
        solver.linearSolveMethod == LinearSolveMethod::mumps ? "mumps" : "fgmres"));

    picojson::object physics_object;
    if (const auto* physics = std::get_if<VibroacousticSettings>(&solver.physics)) {
        physics_object["type"] = picojson::value(std::string("vibroacoustic"));
#define WRITE_PHYSICS(name) physics_object[#name] = number(physics->name)
        WRITE_PHYSICS(rho_s);
        WRITE_PHYSICS(rho_a);
        WRITE_PHYSICS(c_a);
        WRITE_PHYSICS(youngs_modulus);
        WRITE_PHYSICS(poisson_ratio);
        WRITE_PHYSICS(zeta);
        WRITE_PHYSICS(f1);
        WRITE_PHYSICS(f2);
        WRITE_PHYSICS(epsilon);
#undef WRITE_PHYSICS
    }
    else {
        physics_object["type"] = picojson::value(std::string("electromagnetic"));
    }
    solver_object["physics"] = picojson::value(physics_object);

    const OptimizerSettings& optimizer = settings.optSettings;
    picojson::object optimizer_object;
#define WRITE_OPTIMIZER(name) optimizer_object[#name] = number(optimizer.name)
    WRITE_OPTIMIZER(frequencyMin);
    WRITE_OPTIMIZER(frequencyMax);
    WRITE_OPTIMIZER(attenuationMinDb);
    WRITE_OPTIMIZER(attenuationMaxDb);
    WRITE_OPTIMIZER(frequencySamples);
    WRITE_OPTIMIZER(maxIterations);
    WRITE_OPTIMIZER(mmaInitialAsymptote);
    WRITE_OPTIMIZER(mmaDecreaseAsymptote);
    WRITE_OPTIMIZER(mmaIncreaseAsymptote);
    WRITE_OPTIMIZER(mmaConstraintPenalty);
#undef WRITE_OPTIMIZER
    optimizer_object["displayTargetInDb"] =
        picojson::value(optimizer.displayTargetInDb);
    optimizer_object["objectiveMode"] = picojson::value(std::string(
        optimizer.objectiveMode == ObjectiveMode::freeform ? "freeform" : "band"));

    picojson::array bands;
    for (const FrequencyBand& band : optimizer.frequencyBands) {
        picojson::object band_object;
        band_object["type"] = picojson::value(std::string(
            band.type == FrequencyBandType::pass ? "pass" : "stop"));
        band_object["startHz"] = number(band.startHz);
        band_object["endHz"] = number(band.endHz);
        band_object["targetTransmission"] = number(band.targetTransmission);
        bands.emplace_back(band_object);
    }
    optimizer_object["frequencyBands"] = picojson::value(bands);

    picojson::array freeform_frequency;
    for (double value : optimizer.freeformObjective.frequencyHz) {
        freeform_frequency.emplace_back(number(value));
    }
    picojson::array freeform_target;
    for (double value : optimizer.freeformObjective.targetTransmission) {
        freeform_target.emplace_back(number(value));
    }
    picojson::object freeform_objective;
    freeform_objective["frequencyHz"] = picojson::value(freeform_frequency);
    freeform_objective["targetTransmission"] = picojson::value(freeform_target);
    optimizer_object["freeformObjective"] = picojson::value(freeform_objective);

    const UiSettings& ui = settings.uiSettings;
    picojson::object ui_object;
#define WRITE_UI(name) ui_object[#name] = number(ui.name)
    WRITE_UI(plotXMinHz);
    WRITE_UI(plotXMaxHz);
    WRITE_UI(plotYMinDb);
    WRITE_UI(plotYMaxDb);
    WRITE_UI(plotYMinLinear);
    WRITE_UI(plotYMaxLinear);
#undef WRITE_UI
    ui_object["displayInDb"] = picojson::value(ui.displayInDb);

    picojson::object root;
    root["version"] = number(1);
    root["solver"] = picojson::value(solver_object);
    root["optimizer"] = picojson::value(optimizer_object);
    root["ui"] = picojson::value(ui_object);

    std::error_code file_error;
    std::filesystem::create_directories(path.parent_path(), file_error);
    if (file_error) {
        return fail(error, "Could not create " + path.parent_path().string());
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        return fail(error, "Could not open " + temporary.string());
    }
    output << picojson::value(root).serialize(true) << '\n';
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(temporary, file_error);
        return fail(error, "Could not finish writing " + temporary.string());
    }
    output.close();
    return replaceFile(temporary, path, error);
}
