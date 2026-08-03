#pragma once

#include <functional>
#include <string_view>

namespace palcenter::companion {

enum class LogLevel { debug, information, warning, error };

using LogSink = std::function<void(LogLevel level, std::string_view message)>;

}  // namespace palcenter::companion
