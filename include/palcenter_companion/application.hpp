#pragma once

#include "palcenter_companion/http_server.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace palcenter::companion {

class CompanionApplication final {
 public:
  explicit CompanionApplication(LogSink log_sink);
  ~CompanionApplication();

  CompanionApplication(const CompanionApplication&) = delete;
  CompanionApplication& operator=(const CompanionApplication&) = delete;

  bool initialize(const std::filesystem::path& config_path) noexcept;
  void shutdown() noexcept;
  [[nodiscard]] bool is_running() const noexcept;
  bool player_joined(const PlayerIdentity& player) noexcept;
  bool player_left(std::string_view stable_player_key) noexcept;
  void update_player_location(PlayerLocation location) noexcept;

 private:
  LogSink log_sink_;
  LogSink runtime_log_sink_;
  std::unique_ptr<CompanionHttpServer> http_server_;
  std::shared_ptr<PlayerActivityBuffer> activity_buffer_;
  std::shared_ptr<PlayerLocationStore> location_store_;
  std::unique_ptr<PlayerSessionTracker> session_tracker_;
  mutable std::mutex lifecycle_mutex_;
  bool initialization_attempted_{false};
};

std::string load_or_create_instance_id(const std::filesystem::path& config_path);

}  // namespace palcenter::companion
