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

enum class LinearSolveMethod {
    fgmres,
    mumps
};

enum class PhysicsProblem {
    vibroacoustic,
    electromagnetic
};

enum class FrequencyBandType {
    pass,
    stop
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

struct FrequencyBand {
    FrequencyBandType type = FrequencyBandType::stop;
    double startHz = 2500.0;
    double endHz = 4000.0;
    double targetTransmission = 1.0e-2;
};

using PhysicsSettings = std::variant<
    VibroacousticSettings,
    ElectromagneticSettings
>;

struct OptimizerSettings {
    float filterRadius = 0.008f;
    float frequencyMin = 1000.0f;
    float frequencyMax = 4000.0f;
    float attenuationMinDb = -120.0f;
    float attenuationMaxDb = 0.0f;
    int frequencySamples = 256;
    int maxIterations = 400;
    double mmaInitialAsymptote = 0.5;
    double mmaDecreaseAsymptote = 0.7;
    double mmaIncreaseAsymptote = 1.2;
    double mmaConstraintPenalty = 1000.0;
    double cutDerivativeRelativeStep = 1.0e-4;
    bool displayTargetInDb = true;
    std::vector<FrequencyBand> frequencyBands{
        {FrequencyBandType::pass, 1000.0, 2500.0, 1.0},
        {FrequencyBandType::stop, 2500.0, 4000.0, 1.0e-2}
    };
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
    double newmarkBeta = 0.25;
    double newmarkGamma = 0.5;
    double sourceAmplitude = 1.0;
    unsigned int sourceSeed = 1337;
    double initialPatternLx = 0.1;
    double initialPatternLy = 0.1;
    int initialPatternX = 7;
    int initialPatternY = 7;
    double initialPatternBias = 0.1;
    double initialPatternThreshold = 0.01;
    std::string algo = "newmark";
    bool isotropicGrid = false;
    bool useHannWindow = true;
    PhysicsSettings physics;
    SolverDevice device = SolverDevice::serial;
    LinearSolveMethod linearSolveMethod = LinearSolveMethod::fgmres;
    
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
