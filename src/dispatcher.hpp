#pragma once
#include <iostream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <map>
#include <memory>
#include <functional> // Added missing header
#include "optimizer.hpp" 
#include "solver.hpp"    
#include "exporter.hpp"  
#include "executor.hpp"  
#include "error.hpp"
#include "renderer.hpp"

namespace App {

class Dispatcher {
        
    public:
        using Callback = std::function<void()>;
        //TODO: Add pause mechanism later to pause from either opt/solve
        enum class State{Idle, Solving, Optimizing, Diverged, Done, Exporting, Error};
        enum class Event{Reset, Cancel, Start, OptFail, SolveFail, SolveConverged, OptConverged, OptSuccess, Export, Error, HandleError, ExportDone};

        Dispatcher(Executor& executor, Solver& solver, Optimizer& optimizer, Exporter& exporter, Renderer& renderer, ErrorHandler& error_handler);
        ~Dispatcher();

        Solver& solver;
        Optimizer& optimizer;
        Exporter& exporter;
        Executor& executor;
        ErrorHandler& error_handler;


        // FYI: We get the pair bad boys from the utility module
        // TODO: Switch this to a flat array later for less memory fragmentation cuz like... maps be everywhere in the ram

        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions = {
            {{State::Idle, Event::Start}, {State::Solving, [this](){
                //TODO: Idea, let's NOT make these voids, we can do renderer.log() whatever comes
                // out of them. Or even better yet it wont it be nice to have a streaming handle
                // To pass to the solver/opt/etc.... on init where they can stream to a dockable imgui terminal monitor?
                // you get me? So internally all the boys can just do something like log(message). We dont need to pass the whole renderer
                // Just the log function. I know we SHOULD do queues if this were a proper logger, but we're executing stuff sequentially 
                // So i think we can get away with direct publications to some message vector thing the imgui terminal can iterate over
                solver.setMesh();
                solver.assembleSolutionSpace();
                solver.solve();
            }}},
            {{State::Idle, Event::Error}, {State::Error, [this](){
                // dunno
            }}},
            {{State::Solving, Event::SolveConverged}, {State::Optimizing, [this](){
                optimizer.optimize();
            }}},
            {{State::Solving, Event::SolveFail},{State::Diverged, [this](){
                // dunno
            }}},
            {{State::Solving, Event::Cancel}, {State::Idle, [this](){
                // should NOT send another callback to executor, rather just 
                // Make it stop executing after it finishes its current run
            }}},
            {{State::Solving, Event::Error}, {State::Error, [this](){

            }}},
            {{State::Optimizing, Event::OptConverged}, {State::Solving, [this](){
                // At this point we really need to find a way to post messages 
                // To the renderer. You know what let's pass it in
            }}},
            {{State::Optimizing, Event::OptSuccess}, {State::Done, [this](){

            }}},
            {{State::Optimizing, Event::OptFail}, {State::Diverged, [this](){

            }}},
            {{State::Optimizing, Event::Cancel}, {State::Idle, [this](){

            }}},
            {{State::Optimizing, Event::Error}, {State::Error, [this](){

            }}},
            {{State::Diverged, Event::Reset}, {State::Idle, [this](){

            }}},
            {{State::Done, Event::Export}, {State::Exporting, [this](){

            }}},
            {{State::Exporting, Event::ExportDone}, {State::Idle, [this](){

            }}},
            {{State::Exporting, Event::Error}, {State::Error, [this](){

            }}},
            {{State::Error, Event::HandleError}, {State::Idle, [this](){

            }}},
        };

        State dispatch(Event e, State s) {
            std::pair<State, Event> p(s, e);
            try {
                std::pair<State, Callback> sf = transitions.at(p);
                executor.execute(sf.second);
                return sf.first;
            } catch(const std::exception& err) {
                // Fixed \n to be inside quotes
                std::cerr << err.what() << "\n";
                return State::Error;
            }
        } 

    private:
};

} // namespace App
