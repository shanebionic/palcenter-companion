#pragma once

#include "palcenter_companion/player_activity.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace palcenter::companion {

enum class PlayerAreaKind { palpagos, special_area };

struct PlayerLocation {
  PlayerIdentity player;
  double x{};
  double y{};
  double z{};
  PlayerAreaKind area{PlayerAreaKind::palpagos};
  std::string stage_instance_id;
  std::chrono::system_clock::time_point captured_at;
};

class PlayerLocationStore final {
 public:
  void update(PlayerLocation location);
  void remove(std::string_view stable_player_key);
  [[nodiscard]] std::vector<PlayerLocation> current() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, PlayerLocation> locations_;
};

[[nodiscard]] std::string player_locations_json(
    const std::vector<PlayerLocation>& locations);

}  // namespace palcenter::companion
