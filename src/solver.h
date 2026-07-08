#pragma once
#include <vector>
#include <string> 

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

class Solver {

private:

	//hi

public:

	// Also hi
	SolverResult setupRunner(inputSignal, targetResponse)

}