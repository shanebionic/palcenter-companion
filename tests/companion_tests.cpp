#include "palcenter_companion/application.hpp"
#include "palcenter_companion/authentication.hpp"
#include "palcenter_companion/config.hpp"
#include "palcenter_companion/http_server.hpp"
#include "palcenter_companion/player_activity.hpp"

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
using palcenter::companion::CompanionConfig;
using palcenter::companion::CompanionHttpServer;
using palcenter::companion::LogLevel;
using palcenter::companion::PlayerActivityBuffer;
using palcenter::companion::PlayerIdentity;
using palcenter::companion::PlayerSessionTracker;

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

  const auto path = temporary_config(R"([Companion]
Enabled=false
BindAddress=0.0.0.0
Port=9123
LogLevel=Warning
)");
  const auto config = palcenter::companion::load_config(path);
  std::filesystem::remove(path);

  expect(!config.enabled, "Enabled should load as false");
  expect(config.bind_address == "0.0.0.0", "BindAddress should load");
  expect(config.port == 9123, "Port should load");
  expect(config.log_level == LogLevel::warning, "LogLevel should load");

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
  PlayerSessionTracker sessions("instance-test", *activity);
  const PlayerIdentity denalb{"user-one", "player-one", "Denalb"};
  expect(sessions.player_joined(denalb), "First join should create a session");
  expect(!sessions.player_joined(denalb), "Repeated join should not duplicate a session");
  CompanionHttpServer server(config, [&messages](const LogLevel, const std::string_view message) {
    messages.emplace_back(message);
  }, "instance-test", "test-token", activity);

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
  expect(version->body.find("\"applicationVersion\":\"0.3.0\"") != std::string::npos,
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
  expect(contains("PalCenter Companion v0.3.0"), "Startup should log the version");
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
    test_private_bind_warning();
    std::cout << "All PalCenter Companion tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
