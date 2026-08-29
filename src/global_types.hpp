#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>
#include "mfem.hpp"
#include "settings.hpp"
#include "lset.hpp"
#include "coeffs.hpp"
//TODO: Settings, lset, coeffs, etc... should be all folded into this global types, other files needn't reference it all indivudally

constexpr double pi = 3.14159265358979323846;

namespace App {

enum class SolverStatus {
    Idle,
    Working,
    Converged,
    Diverged,
    Error
};

enum class OptimizerStatus {
    Idle,
    Working,
    Converged,
    MaximumIterations,
    Diverged,
    Cancelled,
    Error
};

struct Result {
    int code;
    std::string msg;
};

struct SimulationInfo {
    std::string title;
    std::string body;
};

struct SignalFFT {
    int size = 0;
    std::vector<float> referenceAmplitude;
    std::vector<float> amplitude;
    std::vector<float> transmission;
    std::vector<float> attenuationDB;
    std::vector<float> phase;
    std::vector<float> frequency;
    std::vector<unsigned char> valid;
};

struct SignalTD {
    int size = 0;
    std::vector<double> amplitude;
    std::vector<double> time;
};

struct NewmarkResidualNorms {
    double equilibrium = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

struct SolverResult {
    int success = 0;
    int stateSize = 0;
    int displacementSize = 0;
    int pressureSize = 0;
    int pressureOffset = 0;
    int timeSteps = 0;
    double dt = 0.0;
    std::shared_ptr<const SignalTD> inletPressure;
    std::shared_ptr<const SignalTD> outletPressure;
    std::shared_ptr<const SignalTD> referenceOutletPressure;
    std::shared_ptr<const SignalFFT> materialImpulseResponse;
    std::vector<mfem::Vector> U;
    std::vector<NewmarkResidualNorms> residualNorms;
};

struct SolverInput {
    
};

} // namespace App
