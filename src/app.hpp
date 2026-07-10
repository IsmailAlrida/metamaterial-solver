#pragma once

#include <string>
#include <vector>
#include "global_types.hpp"
#include "renderer.hpp"

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
        // Setup should bind the shouldClose bool to the shouldClose from the renderer btw
        Result setup(std::string glvisHost, int glvisPort = 19916);

    private:
        // ImGui code has to mix with solver/optimizer/exporter so the immediate mode stuff can directly call stuff
        Renderer renderer;

    };

}
