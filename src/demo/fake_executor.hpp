#pragma once

#include <functional>
#include <utility>

#include "logging.hpp"

namespace App::Demo {

class FakeExecutor {
    public:
        explicit FakeExecutor(const LogFunction& log)
            : log(log)
        {
        }

        void execute(std::function<void()> cb)
        {
            if (cb) {
                cb();
            }
        }

    private:
        const LogFunction& log;
};

} // namespace App::Demo
