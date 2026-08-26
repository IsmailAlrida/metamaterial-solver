#pragma once
#include <iostream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <map>
#include <functional> // Added missing header
#include "optimizer.hpp" 
#include "solver.hpp"    
#include "exporter.hpp"  
#include "executor.hpp"  

class Dispatcher {
        
    public:
        using Callback = std::function<void()>;
        //TODO: Add pause mechanism later to pause from either opt/solve
        enum class State{Idle, Solving, Optimizing, Diverged, Done, Exporting, Error};
        enum class Event{Reset, Cancel, Start, OptFail, SolveFail, SolveConverged, OptConverged, OptSuccess, Export, Error, HandleError, ExportDone};

        Dispatcher(Executor& executor);
        ~Dispatcher();

        // FYI: We get the pair bad boys from the utility module
        // TODO: Switch this to a flat array later for less memory fragmentation cuz like... maps be everywhere in the ram

        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions = {
            {{State::Idle, Event::Start}, {State::Solving, {}}},
            {{State::Idle, Event::Error}, {State::Error, {}}},
            {{State::Solving, Event::SolveConverged}, {State::Optimizing, {}}},
            {{State::Solving, Event::SolveFail},{State::Diverged, {}}},
            {{State::Solving, Event::Cancel}, {State::Idle, {}}},
            {{State::Solving, Event::Error}, {State::Error, {}}},
            {{State::Optimizing, Event::OptConverged}, {State::Solving, {}}},
            {{State::Optimizing, Event::OptSuccess}, {State::Done, {}}},
            {{State::Optimizing, Event::OptFail}, {State::Diverged, {}}},
            {{State::Optimizing, Event::Cancel}, {State::Idle, {}}},
            {{State::Optimizing, Event::Error}, {State::Error, {}}},
            {{State::Diverged, Event::Reset}, {State::Idle, {}}},
            {{State::Done, Event::Export}, {State::Exporting, {}}},
            {{State::Exporting, Event::ExportDone}, {State::Idle, {}}},
            {{State::Exporting, Event::Error}, {State::Error, {}}},
            {{State::Error, Event::HandleError}, {State::Idle, {}}},
        };

        State dispatch(Event e, State s) {
            std::pair<State, Event> p(s, e);
            try {
                std::pair<State, Callback> sf = transitions.at(p);
                Executor.execute(sf.second);
                return sf.first;
            } catch(const std::exception& err) {
                // Fixed \n to be inside quotes
                std::cerr << err.what() << "\n";
                return State::Error;
            }
        } 

    private:
};
