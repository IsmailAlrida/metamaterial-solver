#pragma once
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
#include "logging.hpp"

namespace App {

class Dispatcher {
        
    public:
        using Callback = std::function<void()>;
        //TODO: Add pause mechanism later to pause from either opt/solve
        enum class State{Idle, Solving, Optimizing, Diverged, Done, Exporting, Error};
        enum class Event{Reset, Cancel, Start, OptFail, SolveFail, SolveConverged, OptConverged, OptSuccess, Export, Error, HandleError, ExportDone};

        Dispatcher(
            Executor& executor, 
            Solver& solver, 
            Optimizer& optimizer, 
            Exporter& exporter, 
            ErrorHandler& error_handler, 
            const LogFunction& log
        );
        ~Dispatcher();

        Solver& solver;
        Optimizer& optimizer;
        Exporter& exporter;
        Executor& executor;
        ErrorHandler& error_handler;
        const LogFunction& log;

        // FYI: We get the pair bad boys from the utility module
        // TODO: Switch this to a flat array later for less memory fragmentation cuz like... maps be everywhere in the ram

        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions = {
            {{State::Idle, Event::Start}, {State::Solving, [this](){
                solver.setMesh();
                solver.assembleSolutionSpace();
                solver.solve();
            }}},
            {{State::Idle, Event::Error}, {State::Error, [this](){
                // dunno
            }}},
            {{State::Solving, Event::SolveConverged}, {State::Optimizing, [this](){
                optimizer.optimize();
                // FYI: doing this promises that the optimizer WILL change the global levelset implicitly
                // And because in solver transition we reset the mesh on each forward solve, we guarantee to have the
                // Latest mesh shape (the global one) for both the optimizer and solver
            }}},
            {{State::Solving, Event::SolveFail},{State::Diverged, [this](){
                // dunno, it's enough to log and do nothing
            }}},
            {{State::Solving, Event::Cancel}, {State::Idle, [this](){
                // should NOT send another callback to executor, rather just 
                // Make it stop executing after it finishes its current run
            }}},
            {{State::Solving, Event::Error}, {State::Error, [this](){

            }}},
            {{State::Optimizing, Event::OptConverged}, {State::Solving, [this](){
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
                // TODO: Somehow make execution atomic and async
                // Something like await executor.execute() where you let the async 
                executor.execute(sf.second);
                return sf.first;
            } catch(const std::exception& err) {
                log(LogLevel::Error, err.what());
                return State::Error;
            }
        } 

    private:
};

} // namespace App
