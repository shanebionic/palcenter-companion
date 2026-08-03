#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace palcenter::companion {

enum class PlayerActivityType { player_joined, player_left, session_started, session_ended };

struct PlayerIdentity {
  std::string user_id;
  std::string player_id;
  std::string player_name;
};

struct PlayerActivityRecord {
  std::string event_id;
  PlayerActivityType type;
  std::string timestamp;
  std::string server_instance_id;
  PlayerIdentity player;
  std::string session_id;
  std::string source{"palworld_server_hook"};
  std::string schema_version{"1"};
  std::optional<std::uint64_t> duration_seconds;
};

struct ActivityQuery {
  std::size_t limit{100};
  std::optional<std::string> after;
  std::optional<std::string> player;
};

class PlayerActivityBuffer final {
 public:
  explicit PlayerActivityBuffer(std::size_t capacity = 1'000);
  bool publish(PlayerActivityRecord record);
  [[nodiscard]] std::vector<PlayerActivityRecord> query(const ActivityQuery& query) const;
  [[nodiscard]] std::size_t size() const;

 private:
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<PlayerActivityRecord> records_;
  std::unordered_set<std::string> event_ids_;
};

class PlayerSessionTracker final {
 public:
  PlayerSessionTracker(std::string instance_id, PlayerActivityBuffer& buffer);
  bool player_joined(const PlayerIdentity& player,
                     std::chrono::system_clock::time_point occurred_at =
                         std::chrono::system_clock::now());
  bool player_left(std::string_view stable_player_key,
                   std::chrono::system_clock::time_point occurred_at =
                       std::chrono::system_clock::now());

 private:
  struct ActiveSession {
    PlayerIdentity player;
    std::string session_id;
    std::chrono::system_clock::time_point started_at;
  };
  std::string instance_id_;
  PlayerActivityBuffer& buffer_;
  std::mutex mutex_;
  std::unordered_map<std::string, ActiveSession> active_sessions_;
};

[[nodiscard]] std::string activity_type_name(PlayerActivityType type);
[[nodiscard]] std::string activity_record_json(const PlayerActivityRecord& record);

}  // namespace palcenter::companion
