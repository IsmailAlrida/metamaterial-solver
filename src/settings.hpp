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
    float rho_s;
    float rho_a;

    // Rayleigh damping
    float alpha_d;
    float beta_d;

    // Epsilon
    float epsilon
    

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

// TODO: For all settings, pick default app values.
struct SolverSettings {
    int nx;
    int ny; 
    int nz;
    std::string algo;
    PhysicsSettings physics;
    SolverDevice device; 
    
};

struct ExporterSettings {
    // I dont know what to put here
    // yet, but this will have something to do with the meshing


};

struct AppSettings {
    SolverSettings solverSettings;
    OptimizerSettings optSettings;
    ExporterSettings exporterSettings;
};

struct DesignSettings {

};
