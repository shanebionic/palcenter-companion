#include "palcenter_companion/player_activity.hpp"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace palcenter::companion {
namespace {

std::string format_utc(const std::chrono::system_clock::time_point value) {
  const auto time = std::chrono::system_clock::to_time_t(value);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                value.time_since_epoch()) % 1000;
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
         << std::setw(3) << milliseconds.count() << 'Z';
  return output.str();
}

std::string escape_json(const std::string_view value) {
  std::ostringstream output;
  for (const unsigned char character : value) {
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(character) << std::dec;
        } else {
          output << character;
        }
    }
  }
  return output.str();
}

std::string stable_key(const PlayerIdentity& player) {
  if (!player.user_id.empty()) return player.user_id;
  if (!player.player_id.empty()) return player.player_id;
  return player.player_name;
}

std::string identifier(const std::string_view prefix, const std::string_view instance,
                       const std::string_view player, const std::string_view timestamp) {
  static std::atomic<std::uint64_t> sequence{0};
  const auto value = std::string(instance) + '|' + std::string(player) + '|' +
                     std::string(timestamp) + '|' + std::to_string(sequence.fetch_add(1));
  const auto hash = std::hash<std::string>{}(value);
  std::ostringstream output;
  output << prefix << '-' << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

PlayerActivityRecord record(const PlayerActivityType type, const std::string& timestamp,
                            const std::string& instance_id, const PlayerIdentity& player,
                            const std::string& session_id,
                            const std::optional<std::uint64_t> duration_seconds = std::nullopt) {
  return {session_id + '-' + activity_type_name(type), type, timestamp, instance_id, player,
          session_id, "palworld_server_hook", "1", duration_seconds};
}

}  // namespace

PlayerActivityBuffer::PlayerActivityBuffer(const std::size_t capacity) : capacity_(capacity) {
  if (capacity == 0) throw std::invalid_argument("Activity buffer capacity must be positive");
}

bool PlayerActivityBuffer::publish(PlayerActivityRecord record) {
  std::scoped_lock lock(mutex_);
  if (event_ids_.contains(record.event_id)) return false;
  while (records_.size() >= capacity_) {
    event_ids_.erase(records_.front().event_id);
    records_.pop_front();
  }
  event_ids_.insert(record.event_id);
  records_.push_back(std::move(record));
  return true;
}

std::vector<PlayerActivityRecord> PlayerActivityBuffer::query(const ActivityQuery& query) const {
  std::scoped_lock lock(mutex_);
  std::vector<PlayerActivityRecord> result;
  const auto limit = std::clamp<std::size_t>(query.limit, 1, 200);
  for (const auto& item : records_) {
    if (query.after && item.timestamp <= *query.after) continue;
    if (query.player && item.player.user_id != *query.player &&
        item.player.player_id != *query.player) continue;
    result.push_back(item);
  }
  if (result.size() > limit) result.erase(result.begin(), result.end() - limit);
  return result;
}

std::size_t PlayerActivityBuffer::size() const {
  std::scoped_lock lock(mutex_);
  return records_.size();
}

PlayerSessionTracker::PlayerSessionTracker(std::string instance_id, PlayerActivityBuffer& buffer)
    : instance_id_(std::move(instance_id)), buffer_(buffer) {}

bool PlayerSessionTracker::player_joined(const PlayerIdentity& player,
                                         const std::chrono::system_clock::time_point occurred_at) {
  const auto key = stable_key(player);
  if (key.empty()) return false;
  std::scoped_lock lock(mutex_);
  if (active_sessions_.contains(key)) return false;
  const auto timestamp = format_utc(occurred_at);
  const auto session_id = identifier("session", instance_id_, key, timestamp);
  active_sessions_.emplace(key, ActiveSession{player, session_id, occurred_at});
  buffer_.publish(record(PlayerActivityType::player_joined, timestamp, instance_id_, player,
                         session_id));
  buffer_.publish(record(PlayerActivityType::session_started, timestamp, instance_id_, player,
                         session_id));
  return true;
}

bool PlayerSessionTracker::player_left(const std::string_view stable_player_key,
                                       const std::chrono::system_clock::time_point occurred_at) {
  std::scoped_lock lock(mutex_);
  const auto existing = active_sessions_.find(std::string(stable_player_key));
  if (existing == active_sessions_.end()) return false;
  const auto session = existing->second;
  active_sessions_.erase(existing);
  const auto timestamp = format_utc(occurred_at);
  const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      std::max(occurred_at, session.started_at) - session.started_at);
  const auto duration_seconds = static_cast<std::uint64_t>(duration.count());
  buffer_.publish(record(PlayerActivityType::player_left, timestamp, instance_id_, session.player,
                         session.session_id, duration_seconds));
  buffer_.publish(record(PlayerActivityType::session_ended, timestamp, instance_id_, session.player,
                         session.session_id, duration_seconds));
  return true;
}

std::string activity_type_name(const PlayerActivityType type) {
  switch (type) {
    case PlayerActivityType::player_joined: return "player_joined";
    case PlayerActivityType::player_left: return "player_left";
    case PlayerActivityType::session_started: return "session_started";
    case PlayerActivityType::session_ended: return "session_ended";
  }
  return "unknown";
}

std::string activity_record_json(const PlayerActivityRecord& item) {
  return "{\"eventId\":\"" + escape_json(item.event_id) + "\",\"eventType\":\"" +
         activity_type_name(item.type) + "\",\"timestamp\":\"" +
         escape_json(item.timestamp) + "\",\"serverInstanceId\":\"" +
         escape_json(item.server_instance_id) + "\",\"player\":{\"userId\":" +
         (item.player.user_id.empty() ? "null" : "\"" + escape_json(item.player.user_id) + "\"") +
         ",\"playerId\":" +
         (item.player.player_id.empty() ? "null" : "\"" + escape_json(item.player.player_id) + "\"") +
         ",\"name\":\"" + escape_json(item.player.player_name) + "\"},\"sessionId\":\"" +
         escape_json(item.session_id) + "\",\"source\":\"" + escape_json(item.source) +
         "\",\"schemaVersion\":\"" + escape_json(item.schema_version) +
         "\",\"durationSeconds\":" +
         (item.duration_seconds ? std::to_string(*item.duration_seconds) : "null") +
         ",\"metadata\":{}}";
}

}  // namespace palcenter::companion
