#include "palcenter_companion/application.hpp"

#include "palcenter_companion/config.hpp"
#include "palcenter_companion/version.hpp"

#include <exception>
#include <string>
#include <utility>

namespace palcenter::companion {
namespace {

int severity(const LogLevel level) {
  return static_cast<int>(level);
}

}  // namespace

CompanionApplication::CompanionApplication(LogSink log_sink) : log_sink_(std::move(log_sink)) {}

CompanionApplication::~CompanionApplication() { shutdown(); }

bool CompanionApplication::initialize(const std::filesystem::path& config_path) noexcept {
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

    http_server_ = std::make_unique<CompanionHttpServer>(config, filtered_log_sink);
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
    runtime_log_sink_ = {};
    return false;
  }
}

void CompanionApplication::shutdown() noexcept {
  if (http_server_) {
    http_server_->stop();
    http_server_.reset();
    if (runtime_log_sink_) {
      runtime_log_sink_(LogLevel::information, "Companion stopped");
    }
    runtime_log_sink_ = {};
  }
}

bool CompanionApplication::is_running() const noexcept {
  return http_server_ != nullptr && http_server_->is_running();
}

}  // namespace palcenter::companion
