#pragma once

#include <string_view>

#ifndef PALCENTER_COMPANION_VERSION
#define PALCENTER_COMPANION_VERSION "0.1.0"
#endif

#ifndef PALCENTER_COMPANION_API_VERSION
#define PALCENTER_COMPANION_API_VERSION "v1"
#endif

namespace palcenter::companion {

inline constexpr std::string_view application_name{"palcenter-companion"};
inline constexpr std::string_view application_version{PALCENTER_COMPANION_VERSION};
inline constexpr std::string_view api_version{PALCENTER_COMPANION_API_VERSION};

}  // namespace palcenter::companion
