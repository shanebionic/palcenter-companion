#pragma once

#include "palcenter_companion/http_server.hpp"

#include <filesystem>
#include <memory>

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

 private:
  LogSink log_sink_;
  LogSink runtime_log_sink_;
  std::unique_ptr<CompanionHttpServer> http_server_;
};

}  // namespace palcenter::companion
