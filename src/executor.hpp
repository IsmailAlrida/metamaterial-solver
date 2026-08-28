#pragma once

#include <functional>
#include "logging.hpp"

namespace App {

class Executor {

    public:

        Executor(const LogFunction& log);
        ~Executor();

        // I need a way to propagate errors from lower level solvers 
        // Up from the executor to the dispatcher to the 
        void execute(std::function<void()> cb);

    private:
        const LogFunction& log;

};

} // namespace App
