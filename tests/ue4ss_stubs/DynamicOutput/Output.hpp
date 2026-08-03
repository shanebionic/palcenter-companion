#pragma once

#include <string_view>

namespace RC {

namespace LogLevel {
enum LogLevel { Default, Normal, Verbose, Warning, Error };
}

namespace Output {

template <auto Level, typename... Arguments>
void send(const std::wstring_view, Arguments&&...) {
  static_cast<void>(Level);
}

}  // namespace Output
}  // namespace RC
