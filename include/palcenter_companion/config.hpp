#pragma once

#include "palcenter_companion/logger.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace palcenter::companion {

struct CompanionConfig {
  bool enabled{true};
  std::string bind_address{"127.0.0.1"};
  std::uint16_t port{8213};
  LogLevel log_level{LogLevel::information};
};

CompanionConfig load_config(const std::filesystem::path& path);
std::string_view log_level_name(LogLevel level);

}  // namespace palcenter::companion
