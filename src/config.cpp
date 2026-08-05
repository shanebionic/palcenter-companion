#include "palcenter_companion/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace palcenter::companion {
namespace {

std::string trim(std::string value) {
  const auto is_not_space = [](const unsigned char character) {
    return std::isspace(character) == 0;
  };
  const auto first = std::find_if(value.begin(), value.end(), is_not_space);
  const auto last = std::find_if(value.rbegin(), value.rend(), is_not_space).base();
  return first < last ? std::string(first, last) : std::string{};
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool parse_bool(const std::string& value, const std::string_view key) {
  const auto normalized = lowercase(trim(value));
  if (normalized == "true" || normalized == "1" || normalized == "yes") {
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no") {
    return false;
  }
  throw std::runtime_error(std::string(key) + " must be true or false");
}

LogLevel parse_log_level(const std::string& value) {
  const auto normalized = lowercase(trim(value));
  if (normalized == "debug") {
    return LogLevel::debug;
  }
  if (normalized == "information" || normalized == "info") {
    return LogLevel::information;
  }
  if (normalized == "warning" || normalized == "warn") {
    return LogLevel::warning;
  }
  if (normalized == "error") {
    return LogLevel::error;
  }
  throw std::runtime_error("LogLevel must be Debug, Information, Warning, or Error");
}

}  // namespace

CompanionConfig load_config(const std::filesystem::path& path) {
  CompanionConfig config;
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("configuration file could not be opened");
  }

  bool in_companion_section = false;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const auto cleaned = trim(line);
    if (cleaned.empty() || cleaned.starts_with(';') || cleaned.starts_with('#')) {
      continue;
    }
    if (cleaned.front() == '[' && cleaned.back() == ']') {
      in_companion_section = lowercase(trim(cleaned.substr(1, cleaned.size() - 2))) == "companion";
      continue;
    }
    if (!in_companion_section) {
      continue;
    }

    const auto separator = cleaned.find('=');
    if (separator == std::string::npos) {
      throw std::runtime_error("invalid configuration line " + std::to_string(line_number));
    }
    const auto key = lowercase(trim(cleaned.substr(0, separator)));
    const auto value = trim(cleaned.substr(separator + 1));

    if (key == "enabled") {
      config.enabled = parse_bool(value, "Enabled");
    } else if (key == "bindaddress") {
      if (value.empty()) {
        throw std::runtime_error("BindAddress cannot be empty");
      }
      config.bind_address = value;
    } else if (key == "port") {
      std::size_t consumed = 0;
      const auto parsed = std::stoul(value, &consumed, 10);
      if (consumed != value.size() || parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Port must be between 1 and 65535");
      }
      config.port = static_cast<std::uint16_t>(parsed);
    } else if (key == "loglevel") {
      config.log_level = parse_log_level(value);
    } else if (key == "adminactionsenabled") {
      config.admin_actions_enabled = parse_bool(value, "AdminActionsEnabled");
    } else if (key == "teleportadmintoplayerenabled") {
      config.teleport_admin_to_player_enabled =
          parse_bool(value, "TeleportAdminToPlayerEnabled");
    } else if (key == "teleportplayertoadminenabled") {
      config.teleport_player_to_admin_enabled =
          parse_bool(value, "TeleportPlayerToAdminEnabled");
    } else if (key == "teleportplayertolocationenabled") {
      config.teleport_player_to_location_enabled =
          parse_bool(value, "TeleportPlayerToLocationEnabled");
    }
  }

  return config;
}

std::string_view log_level_name(const LogLevel level) {
  switch (level) {
    case LogLevel::debug:
      return "Debug";
    case LogLevel::information:
      return "Information";
    case LogLevel::warning:
      return "Warning";
    case LogLevel::error:
      return "Error";
  }
  return "Information";
}

}  // namespace palcenter::companion
