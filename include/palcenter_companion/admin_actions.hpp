#pragma once

#include "palcenter_companion/config.hpp"
#include "palcenter_companion/logger.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace palcenter::companion {

enum class AdminActionKind {
  teleport_admin_to_player,
  teleport_player_to_admin,
  teleport_player_to_location
};

struct WorldPoint {
  double x{};
  double y{};
  double z{};
};

struct MapPoint {
  double x{};
  double y{};
};

struct AdminActionRequest {
  std::string request_id;
  AdminActionKind action{AdminActionKind::teleport_admin_to_player};
  std::string administrator_player_id;
  std::optional<std::string> target_player_id;
  std::optional<MapPoint> requested_destination;
  std::string destination_coordinate_space;
};

struct AdminActionExecutionResult {
  bool succeeded{false};
  std::string error;
  std::string message;
  std::string source_coordinate_space;
  std::string destination_coordinate_space;
  std::optional<WorldPoint> resolved_destination;
};

struct AdminActionResponse {
  int http_status{500};
  std::string request_id;
  AdminActionKind action{AdminActionKind::teleport_admin_to_player};
  std::string status{"failed"};
  std::string error;
  std::string message;
  std::string source_coordinate_space;
  std::string destination_coordinate_space;
  std::optional<WorldPoint> resolved_destination;
  bool replayed{false};

  [[nodiscard]] std::string json() const;
};

class AdminActionExecutor {
 public:
  virtual ~AdminActionExecutor() = default;
  [[nodiscard]] virtual bool supports(AdminActionKind action) const noexcept = 0;
  virtual AdminActionExecutionResult execute(const AdminActionRequest& request) noexcept = 0;
};

class AdminActionService final {
 public:
  AdminActionService(CompanionConfig config, std::shared_ptr<AdminActionExecutor> executor,
                     std::filesystem::path audit_path, LogSink log_sink,
                     std::size_t maximum_audit_bytes = 5 * 1024 * 1024);
  ~AdminActionService();

  AdminActionService(const AdminActionService&) = delete;
  AdminActionService& operator=(const AdminActionService&) = delete;

  [[nodiscard]] bool advertised(AdminActionKind action) const noexcept;
  [[nodiscard]] std::string capabilities_json() const;
  AdminActionResponse handle(AdminActionKind action, std::string_view request_body);
  void audit_authentication_rejection(AdminActionKind action,
                                      std::string_view request_body);
  void process_pending(std::size_t maximum_actions = 16) noexcept;
  void shutdown() noexcept;

 private:
  struct SharedResult {
    std::mutex mutex;
    std::condition_variable completed;
    std::optional<AdminActionResponse> response;
    bool claimed{false};
    bool cancelled{false};
  };

  struct PendingAction {
    AdminActionRequest request;
    std::shared_ptr<SharedResult> result;
  };

  struct IdempotencyEntry {
    std::string fingerprint;
    std::shared_ptr<SharedResult> result;
  };

  [[nodiscard]] bool configured(AdminActionKind action) const noexcept;
  bool audit(const AdminActionRequest& request, const AdminActionResponse& response,
             std::string_view result_override = {});
  void load_idempotency_records();
  void complete(const std::shared_ptr<SharedResult>& result, AdminActionResponse response);

  CompanionConfig config_;
  std::shared_ptr<AdminActionExecutor> executor_;
  std::filesystem::path audit_path_;
  LogSink log_sink_;
  std::size_t maximum_audit_bytes_;
  mutable std::mutex queue_mutex_;
  std::deque<PendingAction> pending_;
  std::mutex idempotency_mutex_;
  std::unordered_map<std::string, IdempotencyEntry> idempotency_;
  std::deque<std::string> idempotency_order_;
  std::mutex audit_mutex_;
  bool stopping_{false};
};

[[nodiscard]] std::string_view admin_action_name(AdminActionKind action) noexcept;

}  // namespace palcenter::companion
