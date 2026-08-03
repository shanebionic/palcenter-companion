#include "palcenter_companion/http_server.hpp"

#include "palcenter_companion/version.hpp"

#include <httplib.h>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace palcenter::companion {
namespace {

constexpr std::string_view json_content_type{"application/json; charset=utf-8"};

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

std::string version_response() {
  return "{\"application\":\"" + std::string(application_name) +
         "\",\"applicationVersion\":\"" + std::string(application_version) +
         "\",\"apiVersion\":\"" + std::string(api_version) + "\"}";
}

constexpr std::string_view capabilities_response{
    R"({"events":false,"guilds":false,"bases":false,"performance":false,"moderation":false})"};

}  // namespace

CompanionHttpServer::CompanionHttpServer(CompanionConfig config, LogSink log_sink)
    : config_(std::move(config)),
      log_sink_(std::move(log_sink)),
      server_(std::make_unique<httplib::Server>()) {
  register_routes();
}

CompanionHttpServer::~CompanionHttpServer() { stop(); }

bool CompanionHttpServer::start() {
  std::scoped_lock lock(lifecycle_mutex_);
  if (running_) {
    return true;
  }

  int port = 0;
  if (config_.port == 0) {
    port = server_->bind_to_any_port(config_.bind_address);
  } else if (server_->bind_to_port(config_.bind_address, config_.port)) {
    port = config_.port;
  }
  if (port <= 0) {
    log_sink_(LogLevel::error,
              "Unable to bind the Companion HTTP listener; the Palworld server will continue without it");
    return false;
  }

  bound_port_ = static_cast<std::uint16_t>(port);
  started_at_ = std::chrono::system_clock::now();
  running_ = true;
  listener_thread_ = std::thread([this] {
    const auto listen_succeeded = server_->listen_after_bind();
    if (!listen_succeeded && running_) {
      log_sink_(LogLevel::error, "Companion HTTP listener stopped unexpectedly");
    }
    running_ = false;
  });
  return true;
}

void CompanionHttpServer::stop() noexcept {
  std::scoped_lock lock(lifecycle_mutex_);
  if (!listener_thread_.joinable()) {
    running_ = false;
    return;
  }
  running_ = false;
  server_->stop();
  listener_thread_.join();
  running_ = false;
  bound_port_ = 0;
}

bool CompanionHttpServer::is_running() const noexcept { return running_; }

std::uint16_t CompanionHttpServer::bound_port() const noexcept { return bound_port_; }

void CompanionHttpServer::register_routes() {
  server_->Get("/palcenter/v1/health", [this](const httplib::Request&, httplib::Response& response) {
    response.set_content(health_response(), std::string(json_content_type));
  });
  server_->Get("/palcenter/v1/version", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(version_response(), std::string(json_content_type));
  });
  server_->Get("/palcenter/v1/capabilities", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(std::string(capabilities_response), std::string(json_content_type));
  });
}

std::string CompanionHttpServer::health_response() const {
  const auto now = std::chrono::system_clock::now();
  const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - started_at_).count();
  return "{\"status\":\"healthy\",\"applicationVersion\":\"" +
         std::string(application_version) + "\",\"apiVersion\":\"" + std::string(api_version) +
         "\",\"startedAt\":\"" + format_utc(started_at_) + "\",\"uptimeSeconds\":" +
         std::to_string(uptime) + "}";
}

}  // namespace palcenter::companion
