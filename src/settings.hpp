#pragma once
#include <string>
#include <variant>
#include <vector>

// If you really wanna get the int of
// This enum class, you should do
// int x = static_cast<int>(SolverDevice::Cuda);
enum class SolverDevice {
    serial,
    parallel,
    serialCuda,
    parallelCuda
};

enum class PhysicsProblem {
    vibroacoustic,
    electromagnetic
};

using Value = std::variant<int, float, double, long, bool, std::string>;
using Number = std::variant<int, float>;

struct SettingEntry {

    std::string key;
    std::string label;
    std::string displayMode;

    Value value;

    Number min;
    Number max;
    Number step;

    std::vector<std::string> choices;
};

// TODO: Add more to this, and this is where setting entries get used.
struct VibroacousticSettings {

};

// TODO: Get the VA solver working first bro
struct ElectromagneticSettings {

};

using PhysicsSettings = std::variant<
    VibroacousticSettings,
    ElectromagneticSettings
>;

struct OptimizerSettings {

};

struct AppSettings {
    PhysicsProblem problem;
    SolverDevice device;
    int nx;
    int ny;
    int nz;
    PhysicsSettings phySettings;
    OptimizerSettings optSettings;
};

struct DesignSettings {

};
