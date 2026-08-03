#include "palcenter_companion/authentication.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace palcenter::companion {

bool constant_time_token_equal(const std::string_view supplied,
                               const std::string_view expected) noexcept {
  if (supplied.size() != expected.size()) return false;
  unsigned char difference = 0;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    difference |= static_cast<unsigned char>(supplied[index] ^ expected[index]);
  }
  return difference == 0;
}

std::string load_or_create_api_token(const std::filesystem::path& config_path) {
  const auto path = config_path.parent_path() / "PalCenterCompanion.token";
  if (std::ifstream input(path); input) {
    std::string token;
    std::getline(input, token);
    if (token.size() == 64 &&
        std::ranges::all_of(token, [](const unsigned char value) { return std::isxdigit(value); })) {
      return token;
    }
    throw std::runtime_error("Companion token file is invalid; replace it while PalServer is stopped");
  }

  std::array<unsigned char, 32> entropy{};
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, entropy.data(), static_cast<ULONG>(entropy.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("Unable to generate Companion API token");
  }
#else
  if (getrandom(entropy.data(), entropy.size(), 0) !=
      static_cast<ssize_t>(entropy.size())) {
    throw std::runtime_error("Unable to generate Companion API token");
  }
#endif
  std::ostringstream token;
  token << std::hex << std::setfill('0');
  for (const auto item : entropy) token << std::setw(2) << static_cast<unsigned int>(item);

  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("Unable to persist Companion API token");
  output << token.str() << '\n';
  return token.str();
}

}  // namespace palcenter::companion
