#pragma once
#include <stdexcept>
#include <utility>
#include <variant>
#include <map>
#include <memory>
#include <functional> // Added missing header
#include <atomic>
#include <mutex>
#include <thread> 
#include <chrono>
#include <future>
#include <deque>
#include "optimizer.hpp" 
#include "solver.hpp"    
#include "exporter.hpp"  
#include "executor.hpp"  
#include "error.hpp"
#include "logging.hpp"


namespace App {

    /*
        If my dispatcher here sets state, i think it would be better to have the 
        dispatcher itself manage its own state internally, and not let the UI 
        manage that state.

        So get this, in the UI the blocking of different UI parts goes like

            if dispatcher.getState == X, do Y in the UI
        
        So now wh
        
    */
class Dispatcher {
        
    public:
        using Callback = std::function<void()>;
        //TODO: Add pause mechanism later to pause from either opt/solve
        enum class State{Idle, Solving, Optimizing, Diverged, Done, Exporting, Error};
        enum class Event{Reset, Cancel, Start, OptFail, SolveFail, SolveConverged, OptConverged, OptSuccess, Export, Error, HandleError, ExportDone};

        Dispatcher(
            Solver& solver, 
            Optimizer& optimizer, 
            Exporter& exporter, 
            const LogFunction& log
        );
        ~Dispatcher();

        Solver& solver;
        Optimizer& optimizer;
        Exporter& exporter;
        const LogFunction& log;

        // FYI: We get the pair bad boys from the utility module
        // TODO: Switch this to a flat array later for less memory fragmentation cuz like... maps be everywhere in the ram
        // Just keep in your mind that when you dispatch inside a function, you're firing an event on the next state

        std::map<std::pair<State, Event>, std::pair<State, Callback>> transitions = {
            {{State::Idle, Event::Start}, {State::Solving, [this](){

                try
                {
                    if (!(
                        solver.setMesh() &&
                        solver.assembleSolutionSpace() &&
                        solver.solve() 
                        )) 
                    {
                        // TODO: make a meaningful message propagate to the catch block, or just remove this
                        throw;
                    }
                    
                }
                catch(const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                    dispatch(Event::SolveFail);

                }
                dispatch(Event::SolveConverged);
                
                
            }}},
            {{State::Idle, Event::Error}, {State::Error, [this](){
                // dunno
                // Actually this might be a useless state transition
            }}},
            {{State::Solving, Event::SolveConverged}, {State::Optimizing, [this](){

                try
                {
                    optimizer.optimize()
                }
                catch(const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                    dispatch(Event::OptFail);
                    // Im seriously starting to think we should just make one error level
                    // If we're only using this to log
                    // Like if i care about flags for optimizier/solver convergence/divergence/error
                    // I could just forward flags from the app top level to the renderer and the respective classes
                    // Instead of making these weird state transitions
                }

                // TODO: Add a convergence getter to the optimizer class.
                // Same thing with the solver.
                // This will help us log warning messages that certain solvers
                // Converged instead of syntax errored or whatever
                if (optimizer.is_done()) {
                    // Max iters reached, or reached the target
                    dispatch(Event::OptSuccess);
                }
                else {
                    // Reiterate the optimizer
                    dispatch(Event::OptConverged);
                }
                
                
                // FYI: doing this promises that the optimizer WILL change the global levelset implicitly
                // And because in solver transition we reset the mesh on each forward solve, we guarantee to have the
                // Latest mesh shape (the global one) for both the optimizer and solver
            }}},
            {{State::Solving, Event::SolveFail},{State::Diverged, [this](){
                // dunno, it's enough to log and do nothing
                log(App::LogLevel::Warning, "solver failed lul");
                // I think we should be careful of how we name the reset
                dispatch(Event::Reset);

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
            {{State::Exporting, Event::Cancel}, {State::Idle, [this](){

            }}},
            {{State::Error, Event::HandleError}, {State::Idle, [this](){

            }}},
        };

        
        
        
        void dispatch(Event e) {
            using namespace std::chrono_literals;


            if (std::find(events.begin(), events.end(), e) == events.end())
            {

            }
            
                events.push_back(e);
            std::pair<State, Event> p(state, e);

            std::pair<State, Callback> sf = transitions.at(p);
            set_state(sf.first);

            // So this is a nonblocking poll of if the worker is donezo
            if (worker.valid() 
                && worker.wait_for(0ms) == std::future_status::ready) {
                
                // If it's donezo
                try
                {
                    // Consume the future!
                    worker.get();
                }
                catch(const std::exception& e)
                {
                    // And also bubble up the exceptions here
                    log(App::LogLevel::Error, e.what());
                }
                
            }
            worker = std::async(std::launch::async, sf.second);

        } 

        void set_state(State s) {
            state.store(s);
        }

        State get_state() {
            State s = state.load();
            return s;
        }

    private:
        std::atomic<State> state;
        std::future<void> worker;
        std::deque<Event> events;
        std::mutex mutex; 
};

} // namespace App
