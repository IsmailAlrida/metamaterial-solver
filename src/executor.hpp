#pragma once

#include <vector>
#include <any> 
#include <functional>
#include <thread>
#include <stdexcept>

class Executor {

    public:

        Executor();
        ~Executor();

        // I need a way to propagate errors from lower level solvers 
        // Up from the executor to the dispatcher to the 
        void execute(std::function<void()> cb) {

        }

    private:

};