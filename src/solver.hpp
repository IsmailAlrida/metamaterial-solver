#pragma once
#include <vector>
#include <string> 
#include "global_types.hpp"
#include "optimizer.hpp"


class Solver {

public:

    // TODO: Maybe map this to a single setup call with all this stuff internal, but right now im more dying to get this stuff running
	// TODO: Come up with better return types for these
    SolverResult setupBackgroundMesh(int nx, int ny = 0, int nz = 0);
    // TODO: figure out the type for the design optimizer later
    SolverResult setupOptimizer(std::string optimizer, std::string designTarget = "Fix me, dude");
    SolverResult initLevelSet();
    SolverResult assembleSolutionSpace(std::string problem);
    SolverResult run();

private:

};
