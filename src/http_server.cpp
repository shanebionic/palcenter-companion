#include "palcenter_companion/http_server.hpp"

#include "palcenter_companion/authentication.hpp"
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

std::string version_response(const std::string_view instance_id,
                             const std::chrono::system_clock::time_point started_at) {
  const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now() - started_at)
                          .count();
  return "{\"application\":\"" + std::string(application_name) +
         "\",\"applicationVersion\":\"" + std::string(application_version) +
         "\",\"apiVersion\":\"" + std::string(api_version) +
         "\",\"buildCommit\":\"" + std::string(build_commit) +
         "\",\"buildBranch\":\"" + std::string(build_branch) +
         "\",\"buildDate\":\"" + std::string(build_date) +
         "\",\"compiler\":\"C++20\",\"palworldVersion\":null,\"ue4ssVersion\":null,"
         "\"compatibility\":{\"minimumPalCenter\":\"1.4.0\","
         "\"testedPalCenter\":\"1.4.0\",\"testedPalworld\":\"v1.0.2.101103\"},"
         "\"runtime\":{\"startedAt\":\"" + format_utc(started_at) +
         "\",\"uptimeSeconds\":" + std::to_string(uptime) +
         ",\"instanceId\":\"" + std::string(instance_id) +
         "\",\"checks\":{\"configuration\":\"healthy\",\"httpListener\":\"healthy\","
         "\"ue4ssIntegration\":\"healthy\",\"eventEngine\":\"not_available\"}}}";
}

constexpr std::string_view capabilities_response{
    R"({"schemaVersion":"1","categories":{"events":{"supported":false,"capabilityVersion":"1"},"coordinateSpaces":{"supported":false,"capabilityVersion":"1"},"guilds":{"supported":false,"capabilityVersion":"1"},"bases":{"supported":false,"capabilityVersion":"1"},"performance":{"supported":false,"capabilityVersion":"1"},"moderation":{"supported":false,"capabilityVersion":"1"},"administration":{"supported":false,"capabilityVersion":"1"},"health":{"supported":true,"capabilityVersion":"1"},"version":{"supported":true,"capabilityVersion":"1"}}})"};

}  // namespace

CompanionHttpServer::CompanionHttpServer(CompanionConfig config, LogSink log_sink,
                                         std::string instance_id, std::string api_token)
    : config_(std::move(config)),
      log_sink_(std::move(log_sink)),
      server_(std::make_unique<httplib::Server>()), instance_id_(std::move(instance_id)),
      api_token_(std::move(api_token)) {
  server_->set_payload_max_length(1024);
  server_->set_read_timeout(5, 0);
  server_->set_write_timeout(5, 0);
  server_->set_keep_alive_max_count(10);
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
  const auto authenticated = [this](const httplib::Request& request, httplib::Response& response) {
    const auto authorization = request.get_header_value("Authorization");
    constexpr std::string_view prefix{"Bearer "};
    if (!authorization.starts_with(prefix) || authorization.size() > prefix.size() + 128) {
      response.status = 401;
      response.set_content(R"({"error":"authentication_required","message":"Valid bearer authentication is required."})",
                           std::string(json_content_type));
      return false;
    }
    const std::string_view supplied{authorization.data() + prefix.size(),
                                    authorization.size() - prefix.size()};
    if (supplied.empty() || !constant_time_token_equal(supplied, api_token_)) {
      response.status = 401;
      response.set_content(R"({"error":"authentication_required","message":"Valid bearer authentication is required."})",
                           std::string(json_content_type));
      return false;
    }
    return true;
  };
  server_->Get("/palcenter/v1/health", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(R"({"status":"healthy"})", std::string(json_content_type));
  });
  server_->Get("/palcenter/v1/version", [authenticated, this](const httplib::Request& request, httplib::Response& response) {
    if (!authenticated(request, response)) return;
    response.set_content(version_response(instance_id_, started_at_), std::string(json_content_type));
  });
  server_->Get("/palcenter/v1/capabilities", [authenticated](const httplib::Request& request, httplib::Response& response) {
    if (!authenticated(request, response)) return;
    response.set_content(std::string(capabilities_response), std::string(json_content_type));
  });
}

}  // namespace palcenter::companion
