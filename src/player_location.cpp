#include "palcenter_companion/player_location.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace palcenter::companion {
namespace {

std::string key(const PlayerIdentity& player) {
  if (!player.user_id.empty()) return player.user_id;
  if (!player.player_id.empty()) return player.player_id;
  return player.player_name;
}

std::string escape_json(const std::string_view value) {
  std::ostringstream output;
  for (const unsigned char character : value) {
    if (character == '"' || character == '\\') output << '\\';
    if (character >= 0x20) output << character;
  }
  return output.str();
}

std::string format_utc(const std::chrono::system_clock::time_point value) {
  const auto time = std::chrono::system_clock::to_time_t(value);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

}  // namespace

void PlayerLocationStore::update(PlayerLocation location) {
  const auto player_key = key(location.player);
  if (player_key.empty()) return;
  std::scoped_lock lock(mutex_);
  locations_.insert_or_assign(player_key, std::move(location));
}

void PlayerLocationStore::remove(const std::string_view stable_player_key) {
  std::scoped_lock lock(mutex_);
  locations_.erase(std::string(stable_player_key));
}

std::vector<PlayerLocation> PlayerLocationStore::current() const {
  std::scoped_lock lock(mutex_);
  std::vector<PlayerLocation> result;
  result.reserve(locations_.size());
  for (const auto& [_, location] : locations_) result.push_back(location);
  std::ranges::sort(result, {}, [](const PlayerLocation& location) {
    return key(location.player);
  });
  return result;
}

std::string player_locations_json(const std::vector<PlayerLocation>& locations) {
  std::string output{R"({"schemaVersion":"1","locations":[)"};
  for (std::size_t index = 0; index < locations.size(); ++index) {
    const auto& location = locations[index];
    if (index > 0) output += ',';
    output += R"({"player":{"userId":)";
    output += location.player.user_id.empty()
                  ? "null"
                  : "\"" + escape_json(location.player.user_id) + "\"";
    output += R"(,"playerId":)";
    output += location.player.player_id.empty()
                  ? "null"
                  : "\"" + escape_json(location.player.player_id) + "\"";
    output += R"(,"name":")" + escape_json(location.player.player_name) +
              R"("},"position":{"x":)" + std::to_string(location.x) +
              R"(,"y":)" + std::to_string(location.y) + R"(,"z":)" +
              std::to_string(location.z) + R"(},"coordinateSpaceId":")" +
              (location.area == PlayerAreaKind::palpagos ? "palpagos" : "special_area") +
              R"(","stageInstanceId":)";
    output += location.stage_instance_id.empty()
                  ? "null"
                  : "\"" + escape_json(location.stage_instance_id) + "\"";
    output += R"(,"capturedAt":")" + format_utc(location.captured_at) +
              R"(","source":"palworld_server_state"})";
  }
  output += "]}";
  return output;
}

}  // namespace palcenter::companion
