#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace palcenter::companion {

std::string load_or_create_api_token(const std::filesystem::path& config_path);
bool constant_time_token_equal(std::string_view supplied, std::string_view expected) noexcept;

}  // namespace palcenter::companion
