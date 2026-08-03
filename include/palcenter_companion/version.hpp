#pragma once

#include <string_view>

#ifndef PALCENTER_COMPANION_VERSION
#define PALCENTER_COMPANION_VERSION "0.1.0"
#endif

#ifndef PALCENTER_COMPANION_API_VERSION
#define PALCENTER_COMPANION_API_VERSION "v1"
#endif
#ifndef PALCENTER_BUILD_COMMIT
#define PALCENTER_BUILD_COMMIT "unknown"
#endif
#ifndef PALCENTER_BUILD_BRANCH
#define PALCENTER_BUILD_BRANCH "unknown"
#endif
#ifndef PALCENTER_BUILD_DATE
#define PALCENTER_BUILD_DATE "unknown"
#endif

namespace palcenter::companion {

inline constexpr std::string_view application_name{"palcenter-companion"};
inline constexpr std::string_view application_version{PALCENTER_COMPANION_VERSION};
inline constexpr std::string_view api_version{PALCENTER_COMPANION_API_VERSION};
inline constexpr std::string_view build_commit{PALCENTER_BUILD_COMMIT};
inline constexpr std::string_view build_branch{PALCENTER_BUILD_BRANCH};
inline constexpr std::string_view build_date{PALCENTER_BUILD_DATE};

}  // namespace palcenter::companion
