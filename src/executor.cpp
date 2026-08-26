#include "executor.hpp"

namespace App {

Executor::Executor(const LogFunction& log)
    : log(log)
{
}

Executor::~Executor() = default;

} // namespace App
