#include "palcenter_companion/application.hpp"
#include "palcenter_companion/admin_actions.hpp"
#include "palcenter_companion/authentication.hpp"
#include "palcenter_companion/config.hpp"
#include "palcenter_companion/http_server.hpp"
#include "palcenter_companion/player_activity.hpp"
#include "palcenter_companion/player_location.hpp"

#include <httplib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using palcenter::companion::CompanionApplication;
using palcenter::companion::AdminActionExecutionResult;
using palcenter::companion::AdminActionExecutor;
using palcenter::companion::AdminActionKind;
using palcenter::companion::AdminActionRequest;
using palcenter::companion::AdminActionService;
using palcenter::companion::CompanionConfig;
using palcenter::companion::CompanionHttpServer;
using palcenter::companion::LogLevel;
using palcenter::companion::PlayerActivityBuffer;
using palcenter::companion::PlayerIdentity;
using palcenter::companion::PlayerSessionTracker;
using palcenter::companion::PlayerAreaKind;
using palcenter::companion::PlayerLocation;
using palcenter::companion::PlayerLocationStore;
using palcenter::companion::WorldPoint;

class FakeAdminActionExecutor final : public AdminActionExecutor {
 public:
  [[nodiscard]] bool supports(const AdminActionKind action) const noexcept override {
    switch (action) {
      case AdminActionKind::teleport_admin_to_player:
        return support_admin_to_player;
      case AdminActionKind::teleport_player_to_admin:
        return support_player_to_admin;
      case AdminActionKind::teleport_player_to_location:
        return support_player_to_location;
    }
    return false;
  }

  [[nodiscard]] std::string_view unsupported_reason(
      const AdminActionKind action) const noexcept override {
    return supports(action) ? std::string_view{} : runtime_unsupported_reason;
  }

  AdminActionExecutionResult execute(const AdminActionRequest& request) noexcept override {
    ++executions;
    last_request = request;
    return next_result;
  }

  bool support_admin_to_player{true};
  bool support_player_to_admin{true};
  bool support_player_to_location{true};
  std::string runtime_unsupported_reason{"runtime_support_unavailable"};
  std::atomic<int> executions{0};
  AdminActionRequest last_request;
  AdminActionExecutionResult next_result{
      true, {}, "Teleport completed.", "palpagos", "palpagos",
      WorldPoint{100.0, 200.0, 300.0}};
};

class AdminActionPump final {
 public:
  explicit AdminActionPump(std::shared_ptr<AdminActionService> service)
      : service_(std::move(service)), thread_([this] {
          while (!stopping_) {
            service_->process_pending();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
          }
        }) {}

  ~AdminActionPump() {
    stopping_ = true;
    thread_.join();
  }

 private:
  std::shared_ptr<AdminActionService> service_;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
};

class OccupiedPort final {
 public:
  OccupiedPort() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("Unable to initialize Winsock fixture");
    }
#endif
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == invalid_socket) {
      throw std::runtime_error("Unable to create occupied-port fixture");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket_, 1) != 0) {
      close_socket();
      throw std::runtime_error("Unable to bind occupied-port fixture");
    }

#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
      close_socket();
      throw std::runtime_error("Unable to inspect occupied-port fixture");
    }
    port_ = ntohs(address.sin_port);
  }

  ~OccupiedPort() {
    close_socket();
#ifdef _WIN32
    WSACleanup();
#endif
  }

  OccupiedPort(const OccupiedPort&) = delete;
  OccupiedPort& operator=(const OccupiedPort&) = delete;

  [[nodiscard]] std::uint16_t port() const { return port_; }

 private:
#ifdef _WIN32
  using Socket = SOCKET;
  static constexpr Socket invalid_socket = INVALID_SOCKET;
#else
  using Socket = int;
  static constexpr Socket invalid_socket = -1;
#endif

  void close_socket() {
    if (socket_ == invalid_socket) {
      return;
    }
#ifdef _WIN32
    closesocket(socket_);
#else
    close(socket_);
#endif
    socket_ = invalid_socket;
  }

  Socket socket_{invalid_socket};
  std::uint16_t port_{0};
};

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path temporary_config(const std::string& content) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("palcenter-companion-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ".ini");
  std::ofstream output(path);
  output << content;
  output.close();
  return path;
}

template <typename Operation>
void expect_throws(Operation operation, const std::string& message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  expect(rejected, message);
}

void test_configuration() {
  const auto missing = std::filesystem::temp_directory_path() / "palcenter-companion-missing.ini";
  std::filesystem::remove(missing);
  expect_throws([&missing] { static_cast<void>(palcenter::companion::load_config(missing)); },
                "Missing configuration should fail clearly");

  const auto defaults_path = temporary_config("[Companion]\n");
  const auto defaults = palcenter::companion::load_config(defaults_path);
  std::filesystem::remove(defaults_path);
  expect(defaults.enabled, "Companion should be enabled by default");
  expect(defaults.bind_address == "127.0.0.1", "Default bind should be loopback");
  expect(defaults.port == 8213, "Default port should be 8213");
  expect(defaults.log_level == LogLevel::information,
         "Default log level should be Information");
  expect(!defaults.admin_actions_enabled && !defaults.teleport_admin_to_player_enabled &&
             !defaults.teleport_player_to_admin_enabled &&
             !defaults.teleport_player_to_location_enabled,
         "Privileged admin actions must be disabled by default");

  const auto path = temporary_config(R"([Companion]
Enabled=false
BindAddress=0.0.0.0
Port=9123
LogLevel=Warning
AdminActionsEnabled=true
TeleportAdminToPlayerEnabled=true
TeleportPlayerToAdminEnabled=false
TeleportPlayerToLocationEnabled=true
)");
  const auto config = palcenter::companion::load_config(path);
  std::filesystem::remove(path);

  expect(!config.enabled, "Enabled should load as false");
  expect(config.bind_address == "0.0.0.0", "BindAddress should load");
  expect(config.port == 9123, "Port should load");
  expect(config.log_level == LogLevel::warning, "LogLevel should load");
  expect(config.admin_actions_enabled && config.teleport_admin_to_player_enabled &&
             !config.teleport_player_to_admin_enabled &&
             config.teleport_player_to_location_enabled,
         "Admin-action gates should load independently");

  const auto invalid_path = temporary_config("[Companion]\nPort=0\n");
  expect_throws(
      [&invalid_path] { static_cast<void>(palcenter::companion::load_config(invalid_path)); },
      "Invalid ports should be rejected");
  std::filesystem::remove(invalid_path);

  for (const auto* level : {"Debug", "Information", "Warning", "Error"}) {
    const auto level_path = temporary_config("[Companion]\nLogLevel=" + std::string(level) + "\n");
    static_cast<void>(palcenter::companion::load_config(level_path));
    std::filesystem::remove(level_path);
  }

  for (const auto& malformed : {std::string{"[Companion]\nnot-an-assignment\n"},
                                std::string{"[Companion]\nEnabled=perhaps\n"},
                                std::string{"[Companion]\nLogLevel=Trace\n"},
                                std::string{"[Companion]\nBindAddress=   \n"}}) {
    const auto malformed_path = temporary_config(malformed);
    expect_throws(
        [&malformed_path] {
          static_cast<void>(palcenter::companion::load_config(malformed_path));
        },
        "Malformed configuration should be rejected");
    std::filesystem::remove(malformed_path);
  }
}

void test_http_endpoints_and_shutdown() {
  std::vector<std::string> messages;
  CompanionConfig config;
  config.port = 0;
  auto activity = std::make_shared<PlayerActivityBuffer>(4);
  auto locations = std::make_shared<PlayerLocationStore>();
  PlayerSessionTracker sessions("instance-test", *activity);
  const PlayerIdentity denalb{"user-one", "player-one", "Denalb"};
  expect(sessions.player_joined(denalb), "First join should create a session");
  expect(!sessions.player_joined(denalb), "Repeated join should not duplicate a session");
  locations->update({denalb, 123.5, -42.25, 7.0, PlayerAreaKind::palpagos, {},
                     std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}});
  locations->update({{"user-two", "player-two", "Alex"}, 900.0, 800.0, 700.0,
                     PlayerAreaKind::special_area, "stage-one",
                     std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'001}}});
  CompanionHttpServer server(config, [&messages](const LogLevel, const std::string_view message) {
    messages.emplace_back(message);
  }, "instance-test", "test-token", activity, locations);

  expect(server.start(), "HTTP server should bind");
  expect(server.is_running(), "HTTP server should report running");
  expect(server.bound_port() != 0, "HTTP server should expose its bound port");

  httplib::Client client("127.0.0.1", server.bound_port());
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const auto health = client.Get("/palcenter/v1/health");
  expect(health && health->status == 200, "Health endpoint should respond");
  expect(health->body == "{\"status\":\"healthy\"}",
         "Unauthenticated health must expose only status");

  const auto missing_auth = client.Get("/palcenter/v1/version");
  expect(missing_auth && missing_auth->status == 401, "Version should require authentication");
  httplib::Headers headers{{"Authorization", "Bearer test-token"}};
  const auto version = client.Get("/palcenter/v1/version", headers);
  expect(version && version->status == 200, "Version endpoint should respond");
  expect(version->body.find("\"applicationVersion\":\"0.3.2\"") != std::string::npos,
         "Version response should include the application version");
  expect(version->body.find("\"compatibility\":") != std::string::npos,
         "Version response should include informational compatibility");
  expect(version->body.find("\"runtime\":") != std::string::npos,
         "Authenticated version should include runtime diagnostics");

  const httplib::Headers invalid_headers{{"Authorization", "Bearer wrong-token"}};
  const auto invalid = client.Get("/palcenter/v1/capabilities", invalid_headers);
  expect(invalid && invalid->status == 401, "Invalid token should be rejected");
  const httplib::Headers malformed_headers{{"Authorization", "Basic test-token"}};
  const auto malformed = client.Get("/palcenter/v1/capabilities", malformed_headers);
  expect(malformed && malformed->status == 401, "Malformed scheme should be rejected");
  const httplib::Headers oversized_headers{
      {"Authorization", "Bearer " + std::string(129, 'x')}};
  const auto oversized = client.Get("/palcenter/v1/capabilities", oversized_headers);
  expect(oversized && oversized->status == 401, "Oversized token should be rejected");
  const auto capabilities = client.Get("/palcenter/v1/capabilities", headers);
  expect(capabilities && capabilities->status == 200, "Capabilities endpoint should respond");
  expect(capabilities->body.find("\"categories\":") != std::string::npos,
         "Capabilities should use grouped categories");
  expect(capabilities->body.find("\"health\":{\"supported\":true") != std::string::npos,
         "Health capability should be advertised");
  expect(capabilities->body.find("\"playerActivity\":{\"supported\":true") !=
             std::string::npos,
         "Player activity capability should be advertised");
  expect(capabilities->body.find("\"playerLocations\":{\"supported\":true") !=
             std::string::npos,
         "Player locations capability should be advertised");
  expect(capabilities->body.find("\"teleportAdminToPlayer\":false") !=
             std::string::npos,
         "Default configuration must not advertise privileged actions");

  const auto missing_activity_auth = client.Get("/palcenter/v1/activity");
  expect(missing_activity_auth && missing_activity_auth->status == 401,
         "Activity should require authentication");
  const auto recent = client.Get("/palcenter/v1/activity?limit=1&player=user-one", headers);
  expect(recent && recent->status == 200, "Filtered activity should respond");
  expect(recent->body.find("\"eventType\":\"session_started\"") != std::string::npos,
         "Activity should return stable chronological records");
  expect(recent->body.find("Denalb") != std::string::npos,
         "Activity should contain the public player name");
  expect(recent->body.find("Authorization") == std::string::npos,
         "Activity must not expose authentication data");
  const auto invalid_limit = client.Get("/palcenter/v1/activity?limit=201", headers);
  expect(invalid_limit && invalid_limit->status == 400, "Activity limit should be bounded");
  const auto location_response = client.Get("/palcenter/v1/locations", headers);
  expect(location_response && location_response->status == 200,
         "Authenticated locations should respond");
  expect(location_response->body.find("\"coordinateSpaceId\":\"palpagos\"") !=
             std::string::npos,
         "Main-world locations should identify Palpagos explicitly");
  expect(location_response->body.find("\"x\":123.500000") != std::string::npos,
         "Locations should preserve authoritative coordinates");
  expect(location_response->body.find("\"coordinateSpaceId\":\"special_area\"") !=
             std::string::npos,
         "Active Palworld stages should remain separate from Palpagos");

  server.stop();
  expect(!server.is_running(), "HTTP server should stop cleanly");
  expect(server.bound_port() == 0, "Stopped server should release its port");
}

void test_player_activity_sessions_and_buffer() {
  PlayerActivityBuffer buffer(4);
  PlayerSessionTracker sessions("instance-one", buffer);
  const PlayerIdentity first{"user-one", "player-one", "Denalb"};
  const PlayerIdentity second{"user-two", "player-two", "Alex"};
  const auto start = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
  expect(sessions.player_joined(first, start), "Join should create activity");
  expect(!sessions.player_joined(first, start), "Duplicate hook notification should be ignored");
  expect(sessions.player_joined(second, start + std::chrono::seconds{1}),
         "Multiple players should have independent sessions");
  expect(sessions.player_left("user-one", start + std::chrono::seconds{2}),
         "Leave should close the matching session");
  expect(!sessions.player_left("user-one", start + std::chrono::seconds{2}),
         "Repeated leave should not duplicate activity");
  expect(buffer.size() == 4, "Bounded buffer should evict the oldest records");
  const auto all = buffer.query({200, std::nullopt, std::nullopt});
  expect(all.size() == 4, "Bounded query should return retained records");
  expect(all.front().timestamp <= all.back().timestamp, "Activity should remain chronological");
  expect(all.back().event_id.find(all.back().session_id) == 0,
         "Event IDs should remain stable within a session");
  const auto player = buffer.query({10, std::nullopt, std::string{"user-one"}});
  expect(player.size() == 2, "Player filter should select only retained matching records");
  expect(player[0].duration_seconds == 2 && player[1].duration_seconds == 2,
         "Departure records should include the completed session duration");
  expect(activity_record_json(player.back()).find("\"durationSeconds\":2") !=
             std::string::npos,
         "Serialized activity should expose the completed session duration");
  const auto after = buffer.query({10, all[1].timestamp, std::nullopt});
  expect(after.size() == 2, "Timestamp cursor should be exclusive and duplicate-safe");

  PlayerActivityBuffer restarted(4);
  expect(restarted.size() == 0, "Activity buffer should honestly reset with the process");
}

void test_instance_id_persists() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("palcenter-instance-test-" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  const auto config = directory / "PalCenterCompanion.ini";
  std::ofstream(config) << "[Companion]\n";
  const auto first = palcenter::companion::load_or_create_instance_id(config);
  const auto second = palcenter::companion::load_or_create_instance_id(config);
  expect(first.size() == 36, "Instance ID should be UUID-shaped");
  expect(first == second, "Instance ID should persist across restarts");
  std::filesystem::remove_all(directory);
}

void test_api_token_generation_and_persistence() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("palcenter-token-test-" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  const auto config = directory / "PalCenterCompanion.ini";
  std::ofstream(config) << "[Companion]\n";
  const auto first = palcenter::companion::load_or_create_api_token(config);
  const auto second = palcenter::companion::load_or_create_api_token(config);
  expect(first.size() == 64, "Generated token should contain 256 bits encoded as hex");
  expect(first == second, "Generated token should persist across restarts");
  expect(palcenter::companion::constant_time_token_equal(first, second),
         "Constant-time comparison should accept equal tokens");
  expect(!palcenter::companion::constant_time_token_equal(first, std::string(64, '0')),
         "Constant-time comparison should reject unequal tokens");
  std::filesystem::remove_all(directory);
}

void test_application_configuration_and_logging() {
  CompanionConfig probe_config;
  probe_config.port = 0;
  CompanionHttpServer probe(probe_config, [](const LogLevel, const std::string_view) {});
  expect(probe.start(), "Port probe should start");
  const auto port = probe.bound_port();
  probe.stop();

  const auto path = temporary_config("[Companion]\nEnabled=true\nBindAddress=127.0.0.1\nPort=" +
                                     std::to_string(port) + "\nLogLevel=Information\n");
  std::vector<std::string> messages;
  CompanionApplication application(
      [&messages](const LogLevel, const std::string_view message) { messages.emplace_back(message); });
  expect(application.initialize(path), "Application should initialize from configuration");
  expect(application.is_running(), "Application should run after initialization");
  expect(application.initialize(path), "Duplicate initialization should preserve the listener");
  application.shutdown();
  std::filesystem::remove(path);

  const auto contains = [&messages](const std::string_view expected) {
    return std::ranges::any_of(messages, [expected](const std::string& message) {
      return message.find(expected) != std::string::npos;
    });
  };
  expect(contains("PalCenter Companion v0.3.2"), "Startup should log the version");
  expect(contains("Companion initialized"), "Startup should log initialization");
  expect(contains("Listening on 127.0.0.1:"), "Startup should log the listener");
  expect(contains("API Version v1"), "Startup should log the API version");
  expect(contains("Companion stopped"), "Shutdown should be logged");
  expect(contains("Ignoring duplicate Companion initialization request"),
         "Duplicate initialization should be logged");
  expect(std::ranges::count_if(messages, [](const std::string& message) {
           return message.find("Companion initialized") != std::string::npos;
         }) == 1,
         "Companion should initialize exactly once per application lifetime");
}

void test_port_binding_failure_is_contained() {
  CompanionConfig invalid_config;
  invalid_config.bind_address = "not-a-local-address.invalid";
  invalid_config.port = 8213;
  std::vector<std::string> errors;
  CompanionHttpServer invalid_listener(
      invalid_config, [&errors](const LogLevel level, const std::string_view message) {
        if (level == LogLevel::error) {
          errors.emplace_back(message);
        }
      });
  expect(!invalid_listener.start(), "An invalid bind address should fail without crashing");
  expect(!invalid_listener.is_running(), "Failed listener should remain stopped");
  expect(!errors.empty(), "Port binding failure should be logged");
}

void test_disabled_configuration() {
  const auto path = temporary_config(
      "[Companion]\nEnabled=false\nBindAddress=127.0.0.1\nPort=8213\nLogLevel=Information\n");
  CompanionApplication application([](const LogLevel, const std::string_view) {});
  expect(!application.initialize(path), "Disabled Companion should not start");
  expect(!application.is_running(), "Disabled Companion should remain stopped");
  std::filesystem::remove(path);
}

void test_occupied_port_and_repeated_cycles() {
  OccupiedPort reservation;
  CompanionConfig occupied_config;
  occupied_config.port = reservation.port();
  std::vector<std::string> errors;
  CompanionHttpServer occupied(
      occupied_config, [&errors](const LogLevel level, const std::string_view message) {
        if (level == LogLevel::error) {
          errors.emplace_back(message);
        }
      });
  expect(!occupied.start(), "Occupied port should fail without crashing");
  expect(!errors.empty(), "Occupied port should produce a clear error");

  for (int cycle = 0; cycle < 3; ++cycle) {
    CompanionConfig config;
    config.port = 0;
    CompanionHttpServer server(config, [](const LogLevel, const std::string_view) {});
    expect(server.start(), "Repeated listener cycle should start");
    httplib::Client client("127.0.0.1", server.bound_port());
    const auto health = client.Get("/palcenter/v1/health");
    expect(health && health->status == 200, "Repeated listener cycle should serve health");
    server.stop();
    expect(!server.is_running(), "Repeated listener cycle should stop cleanly");
  }
}

void test_admin_actions_contract_dispatch_and_audit() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("palcenter-admin-actions-test-" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  const auto audit_path = directory / "teleport-audit.jsonl";
  const std::string administrator(32, 'A');
  const std::string target(32, 'B');
  const std::string other_target(32, 'C');
  const auto player_body = [&](const std::string_view request_id,
                               const std::string& target_id = std::string(32, 'B')) {
    return "{\"requestId\":\"" + std::string(request_id) +
           "\",\"administratorPlayerId\":\"" + administrator +
           "\",\"targetPlayerId\":\"" + target_id + "\"}";
  };
  const auto location_body = [&](const std::string_view request_id,
                                 const std::string_view coordinate_space,
                                 const std::string_view x = "100") {
    return "{\"requestId\":\"" + std::string(request_id) +
           "\",\"administratorPlayerId\":\"" + administrator +
           "\",\"targetPlayerId\":\"" + target +
           "\",\"coordinateSpace\":\"" +
           std::string(coordinate_space) +
           "\",\"verification\":\"palpagos_map\",\"x\":" + std::string(x) +
           ",\"y\":200}";
  };
  const auto legacy_location_body = [&](const std::string_view request_id) {
    return "{\"requestId\":\"" + std::string(request_id) +
           "\",\"administratorPlayerId\":\"" + administrator +
           "\",\"targetPlayerId\":\"" + target +
           "\",\"destination\":{\"coordinateSpace\":\"palpagos\","
           "\"verification\":\"palpagos_map\",\"x\":100,\"y\":200,\"z\":300}}";
  };
  const auto caller_z_location_body = [&](const std::string_view request_id) {
    return "{\"requestId\":\"" + std::string(request_id) +
           "\",\"administratorPlayerId\":\"" + administrator +
           "\",\"targetPlayerId\":\"" + target +
           "\",\"coordinateSpace\":\"palpagos\",\"verification\":\"palpagos_map\","
           "\"x\":100,\"y\":200,\"z\":300}";
  };

  CompanionConfig enabled;
  enabled.admin_actions_enabled = true;
  enabled.teleport_admin_to_player_enabled = true;
  enabled.teleport_player_to_admin_enabled = true;
  enabled.teleport_player_to_location_enabled = true;
  auto executor = std::make_shared<FakeAdminActionExecutor>();
  auto service = std::make_shared<AdminActionService>(
      enabled, executor, audit_path, [](const LogLevel, const std::string_view) {}, 64 * 1024);
  AdminActionPump pump(service);

  const auto capabilities = service->capabilities_json();
  expect(capabilities.find("\"teleportAdminToPlayer\":true") != std::string::npos &&
             capabilities.find("\"teleportPlayerToAdmin\":true") != std::string::npos &&
             capabilities.find("\"teleportPlayerToLocation\":true") != std::string::npos,
         "Capabilities should advertise each executable enabled action independently");
  expect(capabilities.find("\"capabilityVersion\":\"2\"") != std::string::npos,
         "Admin actions capability version 2 should identify the runtime-height contract");

  const auto first = service->handle(AdminActionKind::teleport_admin_to_player,
                                     player_body("request-success-001"));
  expect(first.http_status == 200 && first.status == "succeeded" && !first.replayed,
         "A valid action should dispatch successfully");
  expect(executor->executions == 1, "A successful action should execute exactly once");
  expect(executor->last_request.administrator_player_id == administrator &&
             executor->last_request.target_player_id == target,
         "Dispatch should use stable player IDs from the request");

  const auto replay = service->handle(AdminActionKind::teleport_admin_to_player,
                                      player_body("request-success-001"));
  expect(replay.http_status == 200 && replay.replayed,
         "An identical retry should return the original result as a replay");
  expect(executor->executions == 1, "An idempotent retry must not execute twice");

  const auto conflict = service->handle(AdminActionKind::teleport_admin_to_player,
                                        player_body("request-success-001", other_target));
  expect(conflict.http_status == 409 && conflict.error == "idempotency_conflict",
         "Reusing a request ID for another payload should be rejected");
  expect(executor->executions == 1, "An idempotency conflict must not dispatch");

  const auto unsupported_space = service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-world-tree-001", "world_tree"));
  expect(unsupported_space.http_status == 400 &&
             unsupported_space.error == "unsupported_coordinate_space",
         "World Tree and unsupported coordinate spaces should be rejected");
  const auto invalid_coordinate = service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-invalid-coordinate-001", "palpagos", "\"not-finite\""));
  expect(invalid_coordinate.http_status == 400 &&
             invalid_coordinate.error == "invalid_coordinates",
         "Malformed coordinates should be rejected");
  const auto out_of_range = service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-out-of-range-001", "palpagos", "999999"));
  expect(out_of_range.http_status == 400 &&
             out_of_range.error == "coordinates_out_of_range",
         "Coordinates outside verified Palpagos bounds should be rejected");
  const auto legacy_location = service->handle(
      AdminActionKind::teleport_player_to_location,
      legacy_location_body("request-legacy-location-001"));
  expect(legacy_location.http_status == 400 &&
             legacy_location.error == "legacy_location_contract",
         "The nested location contract with caller-provided Z should be rejected clearly");
  const auto caller_z = service->handle(
      AdminActionKind::teleport_player_to_location,
      caller_z_location_body("request-caller-z-001"));
  expect(caller_z.http_status == 400 && caller_z.error == "legacy_location_contract",
         "A top-level caller-provided Z should be rejected clearly");
  expect(executor->executions == 1, "Validation failures must not reach gameplay dispatch");

  executor->next_result = {false, "player_offline", "The target character is not online.",
                           {}, {}, std::nullopt};
  const auto offline = service->handle(AdminActionKind::teleport_player_to_admin,
                                       player_body("request-offline-001"));
  expect(offline.http_status == 409 && offline.error == "player_offline",
         "Offline players should be rejected without retry");
  expect(executor->executions == 2, "An offline result should be attempted only once");

  executor->next_result = {false, "safe_destination_unavailable",
                           "The game could not resolve a safe land destination.",
                           "palpagos", "palpagos", std::nullopt};
  const auto unsafe = service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-unsafe-001", "palpagos"));
  expect(unsafe.http_status == 422 && unsafe.error == "safe_destination_unavailable",
         "Failed safe-destination resolution should reject the action");
  expect(executor->executions == 3, "Safe placement failure must not be retried internally");

  executor->next_result = {true, {}, "Teleport completed.", "palpagos", "palpagos",
                           WorldPoint{100.0, 200.0, 345.0}};
  const auto safe_location = service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-safe-location-001", "palpagos"));
  expect(safe_location.http_status == 200 && safe_location.resolved_destination &&
             safe_location.resolved_destination->x == 100.0 &&
             safe_location.resolved_destination->y == 200.0 &&
             safe_location.resolved_destination->z == 345.0,
         "A successful map teleport should return the runtime-resolved X, Y, and Z");
  expect(executor->last_request.requested_destination &&
             executor->last_request.requested_destination->x == 100.0 &&
             executor->last_request.requested_destination->y == 200.0,
         "Gameplay dispatch should receive only the verified map X and Y");
  expect(executor->executions == 4, "A safe map teleport should execute exactly once");

  CompanionConfig disabled = enabled;
  disabled.admin_actions_enabled = false;
  auto disabled_service = std::make_shared<AdminActionService>(
      disabled, executor, directory / "disabled-audit.jsonl",
      [](const LogLevel, const std::string_view) {});
  const auto disabled_response = disabled_service->handle(
      AdminActionKind::teleport_admin_to_player, player_body("request-disabled-001"));
  expect(disabled_response.http_status == 403 &&
             disabled_response.error == "admin_action_disabled",
         "The privileged global configuration gate should fail closed");
  expect(disabled_service->capabilities_json().find("\"teleportAdminToPlayer\":false") !=
             std::string::npos,
         "Disabled actions must not be advertised");

  CompanionConfig partial = enabled;
  partial.teleport_player_to_admin_enabled = false;
  partial.teleport_player_to_location_enabled = false;
  auto partial_service = std::make_shared<AdminActionService>(
      partial, executor, directory / "partial-audit.jsonl",
      [](const LogLevel, const std::string_view) {});
  const auto partial_capabilities = partial_service->capabilities_json();
  expect(partial_capabilities.find("\"teleportAdminToPlayer\":true") != std::string::npos &&
             partial_capabilities.find("\"teleportPlayerToAdmin\":false") !=
                 std::string::npos &&
             partial_capabilities.find("\"teleportPlayerToLocation\":false") !=
                 std::string::npos,
         "Independent gates should produce independent capability flags");

  executor->support_player_to_location = false;
  executor->runtime_unsupported_reason = "safe_floor_function_unavailable";
  const auto runtime_capabilities = service->capabilities_json();
  expect(runtime_capabilities.find("\"teleportPlayerToLocation\":false") !=
             std::string::npos &&
             runtime_capabilities.find(
                 "\"teleportPlayerToLocation\":\"safe_floor_function_unavailable\"") !=
                 std::string::npos,
         "Capabilities should expose a concise additive runtime diagnostic code");
  executor->support_player_to_location = true;

  executor->next_result = {true, {}, "Teleport completed.", "palpagos", "palpagos",
                           WorldPoint{100.0, 200.0, 300.0}};
  CompanionConfig http_config = enabled;
  http_config.port = 0;
  CompanionHttpServer server(http_config, [](const LogLevel, const std::string_view) {},
                             "admin-http-test", "test-token",
                             std::make_shared<PlayerActivityBuffer>(),
                             std::make_shared<PlayerLocationStore>(), service);
  expect(server.start(), "Admin-action HTTP fixture should start");
  httplib::Client client("127.0.0.1", server.bound_port());
  const auto action_path = "/palcenter/v1/admin-actions/teleport-admin-to-player";
  const auto unauthenticated =
      client.Post(action_path, player_body("request-auth-001"), "application/json");
  expect(unauthenticated && unauthenticated->status == 401,
         "Every admin-action endpoint should require existing bearer authentication");
  const httplib::Headers headers{{"Authorization", "Bearer test-token"}};
  const auto authenticated = client.Post(action_path, headers,
                                         player_body("request-auth-001"), "application/json");
  expect(authenticated && authenticated->status == 200 &&
             authenticated->body.find("\"status\":\"succeeded\"") != std::string::npos,
         "An authenticated action should return a structured success response");
  server.stop();

  std::ifstream audit_input(audit_path);
  const std::string audit_content((std::istreambuf_iterator<char>(audit_input)),
                                  std::istreambuf_iterator<char>());
  for (const auto& required : {"\"timestamp\"", "\"requestId\"", "\"action\"",
                               "\"administratorPlayerId\"", "\"targetPlayerId\"",
                               "\"sourceCoordinateSpace\"",
                               "\"destinationCoordinateSpace\"",
                               "\"requestedDestination\"", "\"result\"",
                               "\"failureReason\""}) {
    expect(audit_content.find(required) != std::string::npos,
           "Audit records should contain all required fields");
  }
  expect(audit_content.find("test-token") == std::string::npos,
         "Audit records must never contain authentication tokens");
  expect(audit_content.find("authentication_required") != std::string::npos,
         "Rejected authentication attempts should be present in the action audit");
  expect(audit_content.find("\"resolvedDestination\":{\"x\":100.0,\"y\":200.0,\"z\":345.0}") !=
             std::string::npos,
         "The audit should include the runtime-resolved successful destination");
  audit_input.close();

  service->shutdown();
  disabled_service->shutdown();
  partial_service->shutdown();
  auto restart_executor = std::make_shared<FakeAdminActionExecutor>();
  auto restarted_service = std::make_shared<AdminActionService>(
      enabled, restart_executor, audit_path, [](const LogLevel, const std::string_view) {},
      64 * 1024);
  const auto durable_replay = restarted_service->handle(
      AdminActionKind::teleport_admin_to_player, player_body("request-success-001"));
  expect(durable_replay.replayed && durable_replay.http_status == 200 &&
             restart_executor->executions == 0,
         "Retained audit records should preserve idempotency across restarts");
  const auto durable_location_replay = restarted_service->handle(
      AdminActionKind::teleport_player_to_location,
      location_body("request-safe-location-001", "palpagos"));
  expect(durable_location_replay.replayed && durable_location_replay.http_status == 200 &&
             durable_location_replay.resolved_destination &&
             durable_location_replay.resolved_destination->z == 345.0 &&
             restart_executor->executions == 0,
         "A retained location replay should include the original resolved destination");
  restarted_service->shutdown();
  std::filesystem::remove_all(directory);
}

void test_private_bind_warning() {
  CompanionConfig config;
  config.bind_address = "0.0.0.0";
  config.port = 0;
  std::vector<std::string> warnings;
  CompanionHttpServer probe(config, [](const LogLevel, const std::string_view) {});
  expect(probe.start(), "Private-bind port probe should start");
  const auto port = probe.bound_port();
  probe.stop();

  const auto path = temporary_config("[Companion]\nEnabled=true\nBindAddress=0.0.0.0\nPort=" +
                                     std::to_string(port) + "\nLogLevel=Debug\n");
  CompanionApplication application(
      [&warnings](const LogLevel level, const std::string_view message) {
        if (level == LogLevel::warning) {
          warnings.emplace_back(message);
        }
      });
  expect(application.initialize(path), "Private bind should work when explicitly configured");
  application.shutdown();
  std::filesystem::remove(path);
  expect(!warnings.empty(), "Non-loopback bind should log a security warning");
}

}  // namespace

int main() {
  try {
    test_configuration();
    test_http_endpoints_and_shutdown();
    test_player_activity_sessions_and_buffer();
    test_instance_id_persists();
    test_api_token_generation_and_persistence();
    test_application_configuration_and_logging();
    test_port_binding_failure_is_contained();
    test_disabled_configuration();
    test_occupied_port_and_repeated_cycles();
    test_admin_actions_contract_dispatch_and_audit();
    test_private_bind_warning();
    std::cout << "All PalCenter Companion tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
