#pragma once

#include <mutex>
#include <string>
#include <vector>
#include "imgui.h"
// TODO: im kind of leaning more towards nfd
#include "imfilebrowser.h"
#include "implot.h"
#include "logging.hpp"

struct SDL_Window;
using SDL_GLContext = void*;

namespace App {

    struct AppSettings;
    struct Bandgap;

    class Renderer { 
        
        public:
            bool shouldClose = false;
            Renderer(AppSettings& settings);
            ~Renderer();

            // Need to understand what c++ does with these
            Renderer(const Renderer&) = delete;
            Renderer& operator=(const Renderer&) = delete;
            Renderer(Renderer&&) = delete;
            Renderer& operator=(Renderer&&) = delete;

            // Does the boring imgui window setup and flags and stuff, App class should call this
            void setup();
            void displayFrame();
            void GlvisPanel();
            void LogPanel();
            void log(LogLevel level, std::string msg);

            

        private:
            AppSettings& settings;

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

            struct LogEntry {
                LogLevel level;
                std::string message;
            };

            std::mutex logMutex;
            std::vector<LogEntry> logEntries;
            bool logScrollToBottom = false;

            void applyGlobalStyle(float scale);
            void loadFonts();

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
            void StartMenu();

            // All the following are IMGUI function calls to construct UI

            // All tunable settings: 
            /*
            SimulationSettingsPanel
                tunable settings list:

                rho_s
                rho_a
                nx, ny, nz
                Short text telling ppl that making nz = 0 makes it 2D
                smoothing filter radius
                Hanning window FFT settings
                Physics problem selection
                

            */
            void SimulationSettingsPanel();

            /*
            SimulationInfoPanel
                Summary and guide of the current
                physical simulation (math eqn, summary, etc..)
            */
            void SimulationInfoPanel();

            /*
            OptimizerDesignPanel
                Designing the objective function graphically here
                You get a sweet plot of the filter frequency response
                Two input fields for the overall filter bandwidth (Basically X-range)
                A input field for the Y-range
                A input field for No. of Samples/Points in the freq domain array; dictates the following
                
                A button to toggle between Bandgap and Freeform mode, with a hover tooltip explaining the difference
                to a user who is assumed not to know what they mean, especially in ap context.

                In Freeform mode:
                On the plot itself, since we already have a fixed number of points in the background, we
                can draw them in the following way:
                    - Mouse pointer down and hold
                    - The Point in the array under the cursor (x-axis) moves to the cursor y while the cursor is held
                    - This lets us draw the filter response arbitrarily
                    - Also should have clamps to min/max attenuation; clamp to Y-range value
                    - Reset button
                
                In Bandgap mode
                Or we can edit another way using the Bandgap struct:
                    - We have a "div" under the plot show up
                    - The div has an "+ Add Bandgap" button (use icon lib if possible)
                    - Clicking add adds to a vector of bandgap smart pointers that each point to a unique bandgap object one such pointer
                    - Spawns one UI element linked to it (bandgapGroup) 
                    - Bandgap group has three input fields stacked vertically in a sandwich: Bandwidth (Hz), Center Frequencey (Hz), Attenuation (dB) which are all values of the bg struct
                    - Inputting whatever moves the bandgap immediately
                    - Bandgaps are iteratively applied over the base design function data points
            
                Optional but real nice is Level Mode:
                Similar to bandgap mode:
                    - Small checkbox toggle inside free form mode
                    - add discrete attenuation/amp levels
                    - cursor freeform draw
                    - Auto snap to the nearest level to the cursor
                    - Only ever do discrete steps, at the already specified levels
                    - Feature could be folded directly into freeform mode, replacing the bandgap row of triple sandwiches with single-entry attenuation levels.

            */
            void OptimizerDesignPanel();
            void bandgapGroup(Bandgap* bg); // This is the bandgap UI group, linked to one bg, recursively created in an inline block horizontal overflow x auto 

            /*
            ActionPanel
                This the stuff that starts/cancels/pauses/exports runs,
                and this is where most state machine events will be dispatched from

                While the other panels edit the run settings, the action panel will dictate
                when thing.

                Run/Pause/Cancel:
                    - Run: Runs the solver <-> optimizer feedback loop until convergence/divergence/error. Switches from idle to solving to whatever to optimizing and so on and so forth.
                    - Pause: Halts operation temproarily without freeing resources only after atomic next step is done (i.e, solver finishes its iteration then the halt signal is acked)
                    - Cancel: Waits like pause, discards work/progress with no saving. Should give a popup warning.
                
                Export:
                    - Run the exporter class to meshify the level set into a real mesh file viewable in 3D
                    - Possibly dispatch any other scripts/algos we might bundle with this. IM feeling peckish for an HTML form with CDNs for three.js consuming a JSON of our data to make anice little html report.
                
                FYI: I will make a threadpool class called Executor
            */
            void ActionPanel();
            
    };

} // namespace App
