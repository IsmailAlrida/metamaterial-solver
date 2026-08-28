#include "executor.hpp"
#include <utility>

namespace App {

Executor::Executor(const LogFunction& log)
    : log(log)
{
}

Executor::~Executor() = default;


void Executor::execute(std::function<void()> cb)
{
    if (cb) {
        std::move(cb)();
    }
}


} // namespace App
