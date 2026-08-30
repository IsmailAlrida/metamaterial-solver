#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "dispatcher.hpp"
#include "exporter.hpp"
#include "global_types.hpp"
#include "imgui.h"
// TODO: im kind of leaning more towards nfd
#include "imfilebrowser.h"
#include "implot.h"
#include "logging.hpp"
#include "optimizer.hpp"
#include "solver.hpp"

#ifndef METAMATERIAL_DEMO_MODE
#define METAMATERIAL_DEMO_MODE 0
#endif

#if METAMATERIAL_DEMO_MODE
#include "demo/demos.hpp"
#endif

struct SDL_Window;
using SDL_GLContext = void*;

namespace App {

    struct AppSettings;
    struct FrequencyBand;
    class GlvisAdapter;

#if METAMATERIAL_DEMO_MODE
    using solver_t = Demo::FakeSolver;
    using optimizer_t = Demo::FakeOptimizer;
    using exporter_t = Demo::FakeExporter;
#else
    using solver_t = Solver;
    using optimizer_t = Optimizer;
    using exporter_t = Exporter;
#endif

    using dispatcher_t = Dispatcher<optimizer_t, exporter_t>;

    class Renderer { 
        
        public:
            bool shouldClose = false;
            Renderer(AppSettings& settings, SolverResult& result, LevelSet& geometry);
            ~Renderer();

            // Need to understand what c++ does with these
            Renderer(const Renderer&) = delete;
            Renderer& operator=(const Renderer&) = delete;
            Renderer(Renderer&&) = delete;
            Renderer& operator=(Renderer&&) = delete;

            // Does the boring imgui window setup and flags and stuff, App class should call this
            void setup();
            void displayFrame(dispatcher_t& dispatcher);
            void LogPanel(dispatcher_t& dispatcher);
            void log(LogLevel level, std::string msg);

            

        private:
            AppSettings& settings;
            SolverResult& result;
            LevelSet& geometry;

            std::vector<float> objectiveFrequency;
            std::vector<float> objectiveTarget;

            SDL_Window* window = nullptr;
            SDL_GLContext glContext = nullptr;
            ImFont* uiFont = nullptr;
            ImFont* consoleFont = nullptr;
            const char* glslVersion = nullptr;
            ImVec4 clearColor = ImVec4(0.035f, 0.047f, 0.067f, 1.0f);

            bool sdlInitialized = false;
            bool imguiContextCreated = false;
            bool implotContextCreated = false;
            bool sdlBackendInitialized = false;
            bool openglBackendInitialized = false;
            std::unique_ptr<GlvisAdapter> glvis;

            struct LogEntry {
                LogLevel level;
                std::string message;
            };

            std::mutex logMutex;
            std::vector<LogEntry> logEntries;
            bool logScrollToBottom = false;
            float clearButtonHover = 0.0f;

            void applyGlobalStyle(float scale);
            void loadFonts();
            void setupInitialDockLayout(ImGuiID dockspaceId,
                                        const ImGuiViewport& viewport);
            void updateObjectiveCurve();
            void rebuildImplicitPassBands();

            //todo: maybe just pass refs to the class objects in each functions JUST to make the interface obvious.
            // Or be a devious dev and just do this in the implementation
            // Because every render frame everything will render

            // TODO because here is where im going with this
            // IM instantiating the imgui as functions of this renderer class
            // Hence there needs to be a class wide signaling for when certain events 
            // Trigger, like for example, the optimizer finishing while sim is running to 
            // Retrigger the solver to solve again until maxIters is reached or the target is met
            // So here is the flow
            // Click start, load initial guess and solve physics (sim running, !solverDone)
            // solverDone && simRunning --> push results to optimizer --> !optimizerDone
            // optimizerDone && !optimizerConverged && simRunning --> push new design to solver --> run solver
            // repeat until maxIter for either or user interrupt or an error occurs --> catch and show error

            // Start menu will generate default settings
            // Based on user choice and call sim settings + Plots window

            // Landing page, will (in far future) have selecting between different design tools
            void StartMenu(const dispatcher_t& dispatcher);

            // All the following are IMGUI function calls to construct UI

            // All tunable settings: 
            /*
            SimulationSettingsPanel
                tunable settings list:

                rho_s
                rho_a
                nx, ny, nz
                sx, sy, sz
                Short text telling ppl that making nz = 0 makes it 2D
                smoothing filter radius
                Hanning window FFT settings
                Physics problem selection
                and basically all you see in teh current app settings that is tunable
                would be nice to have a checkbox to toggle between isotropic and an
                for isotropic, we can just put only s and n instead of the xyz,
                for anisotropic, we can just unlock the UI and let them edit the nx,ny,nz,sx,sy,sz
                

            */
            void SimulationSettingsPanel(const dispatcher_t& dispatcher);

            /*
            SimulationInfoPanel
                Summary and guide of the current
                physical simulation (math eqn, summary, etc..)
            */
            void SimulationInfoPanel(const dispatcher_t& dispatcher);

            /*
            OptimizerDesignPanel
                Designing the objective function graphically here
                You get a sweet plot of the filter frequency response
                Two input fields for the overall filter bandwidth (Basically X-range)
                A input field for the Y-range
                A input field for No. of Samples/Points in the freq domain array; dictates the following
                
                Current stop-band mode:
                    - Only stop bands are exposed; every uncovered frequency is an implicit 0 dB passband
                    - Stop bands are edited as start/end frequencies above one attenuation target
                    - The invisible band row scrolls horizontally when it overflows

                TODO: Add Freeform mode later:
                On the plot itself, since we already have a fixed number of points in the background, we
                can draw them in the following way:
                    - Mouse pointer down and hold
                    - The Point in the array under the cursor (x-axis) moves to the cursor y while the cursor is held
                    - This lets us draw the filter response arbitrarily
                    - Also should have clamps to min/max attenuation; clamp to Y-range value
                    - Reset button
            
                Optional but real nice is Level Mode:
                Similar to bandgap mode:
                    - Small checkbox toggle inside free form mode
                    - add discrete attenuation/amp levels
                    - cursor freeform draw
                    - Auto snap to the nearest level to the cursor
                    - Only ever do discrete steps, at the already specified levels
                    - Feature could be folded directly into freeform mode, replacing the bandgap row of triple sandwiches with single-entry attenuation levels.

            */
            void OptimizerDesignPanel(const dispatcher_t& dispatcher);
            bool frequencyBandGroup(FrequencyBand& band,
                                    double lowerBound,
                                    double upperBound,
                                    bool locked);
            
    };

} // namespace App
