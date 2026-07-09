#pragma once
#include <string>
#include <vector>

struct Result {
    int code;
    std::string msg;
};

enum solverDevices { 
    serial,
    parallel,
    cuda
};

enum PhysicsProblems {

    vibroacoustic,
    electromagnetic
};

struct Solid {

};

struct Air {

};

struct SignalFFT {
    int N;
    std::vector<float> amplitude;
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
    
}