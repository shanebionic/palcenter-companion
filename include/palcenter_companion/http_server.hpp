#pragma once

#include "palcenter_companion/config.hpp"
#include "palcenter_companion/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

namespace httplib {
class Server;
}

namespace palcenter::companion {

class CompanionHttpServer final {
 public:
  CompanionHttpServer(CompanionConfig config, LogSink log_sink, std::string instance_id = "ephemeral");
  ~CompanionHttpServer();

  CompanionHttpServer(const CompanionHttpServer&) = delete;
  CompanionHttpServer& operator=(const CompanionHttpServer&) = delete;

  bool start();
  void stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] std::uint16_t bound_port() const noexcept;

 private:
  void register_routes();
  std::string health_response() const;

  CompanionConfig config_;
  LogSink log_sink_;
  std::unique_ptr<httplib::Server> server_;
  std::thread listener_thread_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint16_t> bound_port_{0};
  std::chrono::system_clock::time_point started_at_{};
  std::string instance_id_;
  mutable std::mutex lifecycle_mutex_;
};

}  // namespace palcenter::companion
