#pragma once
#include <string>
#include <vector>
#include "mfem.hpp"
#include "settings.hpp"


struct Result {
    int code;
    std::string msg;
};

struct SimulationInfo {
    std::string title;
    std::string body;
};
// TODO: Check if we need or if setup can be straightforward


// Maybe add an ID for bandgap?
struct Bandgap { 
    float center;
    float bandwidth;
    float attenuationDb; 
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
    int success;
    SignalFFT materialImpulseResponse;
};

struct SolverInput {
    
};


struct LevelSet {
    int dim; 

    
};
