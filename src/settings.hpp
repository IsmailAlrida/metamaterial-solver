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

enum class ObjectiveMode {
    bandgap,
    freeform
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
    float rho_s = 1000.0f;
    float rho_a = 1.21f;
    float c_a = 343.0f;
    float youngs_modulus = 50.0e6f;
    float poisson_ratio = 0.4f;

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

// Maybe add an ID for bandgap?
struct Bandgap {
    float center = 1500.0f;
    float bandwidth = 400.0f;
    float attenuationDb = -35.0f;
};

using PhysicsSettings = std::variant<
    VibroacousticSettings,
    ElectromagneticSettings
>;

struct OptimizerSettings {
    float filterRadius = 0.008f;
    float frequencyMin = 0.0f;
    float frequencyMax = 5000.0f;
    float attenuationMinDb = -80.0f;
    float attenuationMaxDb = 5.0f;
    int frequencySamples = 256;
    int maxIterations = 8;
    ObjectiveMode objectiveMode = ObjectiveMode::bandgap;
    std::vector<Bandgap> bandgaps{
        {1450.0f, 500.0f, -32.0f},
        {2850.0f, 700.0f, -44.0f}
    };
    std::vector<float> freeformTarget;
};

// TODO: For all settings, pick default app values.
struct SolverSettings {
    int nx = 250;
    int ny = 50;
    int nz = 0;
    double inletLength = 0.1;
    double designLength = 0.3;
    double outletLength = 0.1;
    double sy = 0.1;
    double sz = 1.0;
    double duration = 0.02;
    double dt = 2.0e-5;
    std::string algo = "newmark";
    bool isotropicGrid = false;
    bool useHannWindow = true;
    int fftSamples = 1000;
    PhysicsSettings physics;
    SolverDevice device = SolverDevice::serial;
    
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
