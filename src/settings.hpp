#pragma once
#include <string>
#include <variant>
#include <vector>

namespace App {

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
    float rho_s = 0.0f;
    float rho_a = 1.21f;
    float c_a = 343.0f;
    float youngs_modulus = 0.0f;
    float poisson_ratio = 0.0f;

    // Rayleigh damping
    float zeta = 0.1f;
    float f1 = 1600.0f;
    float f2 = 2200.0f;

    // Epsilon
    float epsilon = 1.0e-8f;
    

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
    int nx = 250;
    int ny = 50;
    int nz = 0;
    double sx = 0.5;
    double sy = 0.1;
    double sz = 1.0;
    double duration = 0.2;
    double dt = duration / 1000.0;
    std::string algo = "newmark";
    PhysicsSettings physics;
    SolverDevice device = SolverDevice::serial;
    std::string glvisHost = "localhost";
    int glvisPort = 19916;
    
};

struct ExporterSettings {
    // I dont know what to put here
    // yet, but this will have something to do with the meshing


};

struct AppSettings {
    SolverSettings solverSettings{};
    OptimizerSettings optSettings{};
    ExporterSettings exporterSettings{};
};

struct DesignSettings {

};

} // namespace App
