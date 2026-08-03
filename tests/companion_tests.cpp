#include "palcenter_companion/application.hpp"
#include "palcenter_companion/config.hpp"
#include "palcenter_companion/http_server.hpp"

#include <httplib.h>

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

void test_configuration() {
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
  bool rejected = false;
  try {
    static_cast<void>(palcenter::companion::load_config(invalid_path));
  } catch (const std::exception&) {
    rejected = true;
  }
  std::filesystem::remove(invalid_path);
  expect(rejected, "Invalid ports should be rejected");
}

void test_http_endpoints_and_shutdown() {
  std::vector<std::string> messages;
  CompanionConfig config;
  config.port = 0;
  CompanionHttpServer server(config, [&messages](const LogLevel, const std::string_view message) {
    messages.emplace_back(message);
  });

  expect(server.start(), "HTTP server should bind");
  expect(server.is_running(), "HTTP server should report running");
  expect(server.bound_port() != 0, "HTTP server should expose its bound port");

  httplib::Client client("127.0.0.1", server.bound_port());
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const auto health = client.Get("/palcenter/v1/health");
  expect(health && health->status == 200, "Health endpoint should respond");
  expect(health->body.find("\"status\":\"healthy\"") != std::string::npos,
         "Health response should report healthy");
  expect(health->body.find("\"applicationVersion\":\"0.1.0\"") != std::string::npos,
         "Health response should include the application version");
  expect(health->body.find("\"apiVersion\":\"v1\"") != std::string::npos,
         "Health response should include the API version");
  expect(health->body.find("\"startedAt\":") != std::string::npos,
         "Health response should include the start time");
  expect(health->body.find("\"uptimeSeconds\":") != std::string::npos,
         "Health response should include uptime");

  const auto version = client.Get("/palcenter/v1/version");
  expect(version && version->status == 200, "Version endpoint should respond");
  expect(version->body ==
             "{\"application\":\"palcenter-companion\",\"applicationVersion\":\"0.1.0\","
             "\"apiVersion\":\"v1\"}",
         "Version response should match the v1 contract");

  const auto capabilities = client.Get("/palcenter/v1/capabilities");
  expect(capabilities && capabilities->status == 200, "Capabilities endpoint should respond");
  expect(capabilities->body ==
             "{\"events\":false,\"guilds\":false,\"bases\":false,"
             "\"performance\":false,\"moderation\":false}",
         "Foundation capabilities should all be false");

  server.stop();
  expect(!server.is_running(), "HTTP server should stop cleanly");
  expect(server.bound_port() == 0, "Stopped server should release its port");
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
  application.shutdown();
  std::filesystem::remove(path);

  const auto contains = [&messages](const std::string_view expected) {
    return std::ranges::any_of(messages, [expected](const std::string& message) {
      return message.find(expected) != std::string::npos;
    });
  };
  expect(contains("PalCenter Companion v0.1.0"), "Startup should log the version");
  expect(contains("Companion initialized"), "Startup should log initialization");
  expect(contains("Listening on 127.0.0.1:"), "Startup should log the listener");
  expect(contains("API Version v1"), "Startup should log the API version");
  expect(contains("Companion stopped"), "Shutdown should be logged");
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

}  // namespace

int main() {
  try {
    test_configuration();
    test_http_endpoints_and_shutdown();
    test_application_configuration_and_logging();
    test_port_binding_failure_is_contained();
    test_disabled_configuration();
    std::cout << "All PalCenter Companion tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
