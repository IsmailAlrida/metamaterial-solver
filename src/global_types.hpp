#pragma once
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

struct Result {
    int code;
    std::string msg;
};

struct SimulationInfo {
    std::string title;
    std::string body;
};

struct SignalFFT {
    int size;
    std::vector<float> amplitude;
    std::vector<float> attenuationDB; 
    std::vector<float> phase;
    std::vector<float> frequency;
};

struct SignalTD {
    int size;
    std::vector<float> amplitude;
    std::vector<float> time;
};

struct SolverResult {
    int success = 0;
    SignalFFT materialImpulseResponse;
    std::vector<mfem::Vector> U;
    std::vector<mfem::Vector> R;
};

struct SolverInput {
    
};

} // namespace App
