#pragma once

#include <iostream> 
#include <map> 
#include <utility>
#include "global_types.hpp"
#include "solver.hpp"
#include "optimizer.hpp"
#include "exporter.hpp" 
#include "imgui.h"
// TODO: im kind of leaning more towards nfd
#include "imfilebrowser.h"
#include "implot.h"



    class Renderer { 
        
        public:
            bool shouldClose;
            Renderer();
            ~Renderer();

            // Does the boring imgui window setup and flags and stuff, App class should call this
            void setup();
            void displayFrame();

            //TODO: Add pause mechanism later to pause from either opt/solve
            enum class State{Idle, Solving, Optimizing, Diverged, Done, Exporting, Error};
            enum class Event{Reset, Cancel, Start, OptFail, SolveFail, SolveConverged, OptConverged, OptSuccess, Export, Error, HandleError, ExportDone};

            std::map<std::pair<State, Event>, State> transitions = {
                {{State::Idle, Event::Start}, State::Solving},
                {{State::Idle, Event::Error}, State::Error},

                {{State::Solving, Event::SolveConverged}, State::Optimizing},
                {{State::Solving, Event::SolveFail}, State::Diverged},
                {{State::Solving, Event::Cancel}, State::Idle},
                {{State::Solving, Event::Error}, State::Error},

                {{State::Optimizing, Event::OptConverged}, State::Solving},
                {{State::Optimizing, Event::OptSuccess}, State::Done},
                {{State::Optimizing, Event::OptFail}, State::Diverged},
                {{State::Optimizing, Event::Cancel}, State::Idle},
                {{State::Optimizing, Event::Error}, State::Error},

                {{State::Diverged, Event::Reset}, State::Idle},

                {{State::Done, Event::Export}, State::Exporting},
                {{State::Exporting, Event::ExportDone}, State::Idle},
                {{State::Exporting, Event::Error}, State::Error},

                {{State::Error, Event::HandleError}, State::Idle},
            };

        private:
            Solver solver; 
            Optimizer optimizer;
            Exporter exporter; 
            LevelSet geometry; 
            std::vector<Bandgap> designTargets;
            //todo: maybe rename this to SolverOutput. Optimizer will directly modify the LevelSet, so no need for opt output
            SolverResult result;
            AppSettings settings;

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
            void StartMenu();
            void SimulationSettingsPanel();
            void SimulationInfoPanel();
            void OptimizerDesignPanel();
            void ActionPanel();
            void GlvisPanel();
    };
