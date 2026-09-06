#pragma once

#include <functional>
#include <string>

namespace App {

enum class LogLevel {
    Message,
    Warning,
    Error
};

using LogFunction = std::function<void(LogLevel, std::string)>;

} // namespace App
