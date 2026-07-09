#pragma once

#include <string>
#include <vector>
#include "global_types.hpp"
#include "solver.hpp"
#include "renderer.hpp"
#include "exporter.hpp"

namespace MetamaterialDesigner
{

    class App{
    public:

        bool shouldClose;
        
        // Build MFEM Initial Mesh, Build Solver Equations
        App(); //Put args here

        
        // Run an ImGui frame that dispatches the calls to the backend
        void frame();

        // Exposing these cuz why not we wanna try running headless ops too.
        // TODO: Consider making a headless server mode instead
        Result runSolver();
        Result setup();
        void setupGui();
        // TODO: For now we're just going to run the web server of glvis alone and mfem::sockstream to it, but later I reaaally do want to fork over the code and put it in one window for the app
        bool setupGlvis(std::string host, int port = );
        Result setupSolver(std::string problem, std::string optimizer, std::string solverDevice, int nx = 0, int ny = 0, int nz = 0);

    private:
        Solver solver;
        Renderer renderer;

    };

}
