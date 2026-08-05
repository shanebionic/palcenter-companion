#include "palcenter_companion/application.hpp"

#include "palcenter_companion/config.hpp"
#include "palcenter_companion/authentication.hpp"
#include "palcenter_companion/version.hpp"

#include <exception>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace palcenter::companion {
namespace {

int severity(const LogLevel level) {
  return static_cast<int>(level);
}

}  // namespace

std::string load_or_create_instance_id(const std::filesystem::path& config_path) {
  const auto path = config_path.parent_path() / "PalCenterCompanion.instance-id";
  if (std::ifstream input(path); input) {
    std::string value;
    std::getline(input, value);
    if (value.size() == 36) {
      return value;
    }
  }

  std::random_device source;
  std::uniform_int_distribution<unsigned int> byte(0, 255);
  unsigned char value[16];
  for (auto& item : value) {
    item = static_cast<unsigned char>(byte(source));
  }
  value[6] = static_cast<unsigned char>((value[6] & 0x0f) | 0x40);
  value[8] = static_cast<unsigned char>((value[8] & 0x3f) | 0x80);

  std::ostringstream id;
  id << std::hex << std::setfill('0');
  for (int index = 0; index < 16; ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      id << '-';
    }
    id << std::setw(2) << static_cast<unsigned int>(value[index]);
  }

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Unable to persist Companion instance ID");
  }
  output << id.str() << '\n';
  return id.str();
}

CompanionApplication::CompanionApplication(
    LogSink log_sink, std::shared_ptr<AdminActionExecutor> admin_action_executor)
    : log_sink_(std::move(log_sink)),
      admin_action_executor_(std::move(admin_action_executor)) {}

CompanionApplication::~CompanionApplication() { shutdown(); }

bool CompanionApplication::initialize(const std::filesystem::path& config_path) noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  if (initialization_attempted_) {
    log_sink_(LogLevel::warning, "Ignoring duplicate Companion initialization request");
    return http_server_ != nullptr && http_server_->is_running();
  }
  initialization_attempted_ = true;
  try {
    const auto config = load_config(config_path);
    const LogSink filtered_log_sink = [sink = log_sink_, minimum = config.log_level](
                                          const LogLevel level, const std::string_view message) {
      if (severity(level) >= severity(minimum)) {
        sink(level, message);
      }
    };
    runtime_log_sink_ = filtered_log_sink;
    filtered_log_sink(LogLevel::information,
                      "PalCenter Companion v" + std::string(application_version));

    if (!config.enabled) {
      filtered_log_sink(LogLevel::information, "Companion is disabled by configuration");
      return false;
    }
    if (config.bind_address != "127.0.0.1" && config.bind_address != "::1" &&
        config.bind_address != "localhost") {
      filtered_log_sink(
          LogLevel::warning,
          "Companion is bound beyond loopback; restrict access with the host firewall");
    }

    const auto instance_id = load_or_create_instance_id(config_path);
    activity_buffer_ = std::make_shared<PlayerActivityBuffer>();
    location_store_ = std::make_shared<PlayerLocationStore>();
    session_tracker_ = std::make_unique<PlayerSessionTracker>(instance_id, *activity_buffer_);
    admin_actions_ = std::make_shared<AdminActionService>(
        config, admin_action_executor_,
        config_path.parent_path() / "PalCenterCompanion.teleport-audit.jsonl",
        filtered_log_sink);
    http_server_ = std::make_unique<CompanionHttpServer>(
        config, filtered_log_sink, instance_id, load_or_create_api_token(config_path),
        activity_buffer_, location_store_, admin_actions_);
    if (!http_server_->start()) {
      http_server_.reset();
      return false;
    }

    filtered_log_sink(LogLevel::information, "Companion initialized");
    filtered_log_sink(LogLevel::information,
                      "Listening on " + config.bind_address + ":" +
                          std::to_string(http_server_->bound_port()));
    filtered_log_sink(LogLevel::information, "API Version " + std::string(api_version));
    return true;
  } catch (const std::exception& error) {
    log_sink_(LogLevel::error,
              "Companion initialization failed; the Palworld server will continue: " +
                  std::string(error.what()));
    http_server_.reset();
    session_tracker_.reset();
    activity_buffer_.reset();
    location_store_.reset();
    admin_actions_.reset();
    runtime_log_sink_ = {};
    return false;
  }
}

void CompanionApplication::update_player_location(PlayerLocation location) noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  if (location_store_) location_store_->update(std::move(location));
}

void CompanionApplication::process_pending_admin_actions() noexcept {
  std::shared_ptr<AdminActionService> admin_actions;
  {
    std::scoped_lock lock(lifecycle_mutex_);
    admin_actions = admin_actions_;
  }
  if (admin_actions) admin_actions->process_pending();
}

void CompanionApplication::shutdown() noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  if (http_server_) {
    if (admin_actions_) admin_actions_->shutdown();
    http_server_->stop();
    http_server_.reset();
    session_tracker_.reset();
    activity_buffer_.reset();
    admin_actions_.reset();
    if (runtime_log_sink_) {
      runtime_log_sink_(LogLevel::information, "Companion stopped");
    }
    runtime_log_sink_ = {};
  }
}

bool CompanionApplication::is_running() const noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  return http_server_ != nullptr && http_server_->is_running();
}

bool CompanionApplication::player_joined(const PlayerIdentity& player) noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  return session_tracker_ && session_tracker_->player_joined(player);
}

bool CompanionApplication::player_left(const std::string_view stable_player_key) noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  if (location_store_) location_store_->remove(stable_player_key);
  return session_tracker_ && session_tracker_->player_left(stable_player_key);
}

}  // namespace palcenter::companion
