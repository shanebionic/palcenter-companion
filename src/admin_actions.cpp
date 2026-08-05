#include "palcenter_companion/admin_actions.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace palcenter::companion {
namespace {

using Json = nlohmann::json;

constexpr double palpagos_min_x{-999'940.0};
constexpr double palpagos_max_x{447'900.0};
constexpr double palpagos_min_y{-738'920.0};
constexpr double palpagos_max_y{708'920.0};
constexpr double minimum_supported_z{-200'000.0};
constexpr double maximum_supported_z{1'000'000.0};
constexpr std::size_t maximum_idempotency_records{10'000};
constexpr auto dispatch_timeout = std::chrono::seconds{5};

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

Json point_json(const WorldPoint& point) {
  return {{"x", point.x}, {"y", point.y}, {"z", point.z}};
}

bool valid_request_id(const std::string_view value) {
  if (value.size() < 8 || value.size() > 128) return false;
  return std::ranges::all_of(value, [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_' ||
           character == '.' || character == ':';
  });
}

bool valid_player_id(const std::string_view value) {
  return value.size() == 32 && std::ranges::all_of(value, [](const unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

bool valid_point(const WorldPoint& point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
         point.x >= palpagos_min_x && point.x <= palpagos_max_x &&
         point.y >= palpagos_min_y && point.y <= palpagos_max_y &&
         point.z >= minimum_supported_z && point.z <= maximum_supported_z;
}

std::string fingerprint(const AdminActionRequest& request) {
  std::ostringstream value;
  value << admin_action_name(request.action) << '|' << request.administrator_player_id << '|'
        << request.target_player_id.value_or("") << '|' << request.destination_coordinate_space;
  if (request.requested_destination) {
    value << '|' << std::setprecision(std::numeric_limits<double>::max_digits10)
          << request.requested_destination->x << '|' << request.requested_destination->y << '|'
          << request.requested_destination->z;
  }
  return value.str();
}

AdminActionResponse failure(const AdminActionRequest& request, const int http_status,
                            std::string error, std::string message) {
  return {http_status,
          request.request_id,
          request.action,
          "rejected",
          std::move(error),
          std::move(message),
          {},
          request.destination_coordinate_space,
          std::nullopt,
          false};
}

int execution_http_status(const std::string_view error) {
  if (error == "player_offline" || error == "player_state_stale" ||
      error == "player_ambiguous" || error == "special_area_not_supported") {
    return 409;
  }
  if (error == "safe_destination_unavailable" || error == "teleport_rejected") return 422;
  if (error == "action_not_supported") return 501;
  return 500;
}

bool idempotency_tracked(const AdminActionResponse& response) {
  if (response.error.empty()) return true;
  return response.error != "authentication_required" &&
         response.error != "invalid_request" && response.error != "invalid_request_id" &&
         response.error != "invalid_administrator_player_id" &&
         response.error != "invalid_target_player_id" &&
         response.error != "identical_players" &&
         response.error != "invalid_destination" &&
         response.error != "unsupported_coordinate_space" &&
         response.error != "unverified_destination" &&
         response.error != "invalid_coordinates" &&
         response.error != "coordinates_out_of_range" &&
         response.error != "idempotency_conflict";
}

struct ParsedRequest {
  AdminActionRequest request;
  std::optional<AdminActionResponse> error;
};

ParsedRequest parse_request(const AdminActionKind action, const std::string_view body) {
  ParsedRequest parsed;
  parsed.request.action = action;
  Json document;
  try {
    document = Json::parse(body);
  } catch (const Json::exception&) {
    parsed.request.request_id = "<missing>";
    parsed.error = failure(parsed.request, 400, "invalid_request", "The request body must be valid JSON.");
    return parsed;
  }
  if (!document.is_object()) {
    parsed.request.request_id = "<missing>";
    parsed.error = failure(parsed.request, 400, "invalid_request", "The request body must be a JSON object.");
    return parsed;
  }

  if (document.contains("requestId") && document["requestId"].is_string()) {
    parsed.request.request_id = document["requestId"].get<std::string>();
  } else {
    parsed.request.request_id = "<missing>";
  }
  if (document.contains("administratorPlayerId") &&
      document["administratorPlayerId"].is_string()) {
    parsed.request.administrator_player_id = document["administratorPlayerId"].get<std::string>();
  }
  if (document.contains("targetPlayerId") && document["targetPlayerId"].is_string()) {
    parsed.request.target_player_id = document["targetPlayerId"].get<std::string>();
  }

  if (!valid_request_id(parsed.request.request_id)) {
    parsed.error = failure(parsed.request, 400, "invalid_request_id",
                           "requestId must be a unique 8-128 character identifier.");
    return parsed;
  }
  if (!valid_player_id(parsed.request.administrator_player_id)) {
    parsed.error = failure(parsed.request, 400, "invalid_administrator_player_id",
                           "administratorPlayerId must be a stable 32-digit player ID.");
    return parsed;
  }
  std::ranges::transform(parsed.request.administrator_player_id,
                         parsed.request.administrator_player_id.begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::toupper(character));
                         });

  if (action != AdminActionKind::teleport_player_to_location) {
    if (!parsed.request.target_player_id || !valid_player_id(*parsed.request.target_player_id)) {
      parsed.error = failure(parsed.request, 400, "invalid_target_player_id",
                             "targetPlayerId must be a stable 32-digit player ID.");
      return parsed;
    }
    std::ranges::transform(*parsed.request.target_player_id,
                           parsed.request.target_player_id->begin(),
                           [](const unsigned char character) {
                             return static_cast<char>(std::toupper(character));
                           });
    if (*parsed.request.target_player_id == parsed.request.administrator_player_id) {
      parsed.error = failure(parsed.request, 400, "identical_players",
                             "The administrator and target player must be different players.");
      return parsed;
    }
    return parsed;
  }

  if (!parsed.request.target_player_id || !valid_player_id(*parsed.request.target_player_id)) {
    parsed.error = failure(parsed.request, 400, "invalid_target_player_id",
                           "targetPlayerId must be a stable 32-digit player ID.");
    return parsed;
  }
  std::ranges::transform(*parsed.request.target_player_id,
                         parsed.request.target_player_id->begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::toupper(character));
                         });
  if (!document.contains("destination") || !document["destination"].is_object()) {
    parsed.error = failure(parsed.request, 400, "invalid_destination",
                           "destination must contain verified Palpagos coordinates.");
    return parsed;
  }
  const auto& destination = document["destination"];
  if (destination.contains("coordinateSpace") && destination["coordinateSpace"].is_string()) {
    parsed.request.destination_coordinate_space =
        destination["coordinateSpace"].get<std::string>();
  }
  if (parsed.request.destination_coordinate_space != "palpagos") {
    parsed.error = failure(parsed.request, 400, "unsupported_coordinate_space",
                           "Only the palpagos coordinate space is supported.");
    return parsed;
  }
  if (!destination.contains("verification") || !destination["verification"].is_string() ||
      destination["verification"].get<std::string>() != "palpagos_map") {
    parsed.error = failure(parsed.request, 400, "unverified_destination",
                           "The destination must be confirmed as a Palpagos map location.");
    return parsed;
  }
  if (!destination.contains("x") || !destination.contains("y") ||
      !destination.contains("z") || !destination["x"].is_number() ||
      !destination["y"].is_number() || !destination["z"].is_number()) {
    parsed.error = failure(parsed.request, 400, "invalid_coordinates",
                           "Destination x, y, and z must be finite numbers.");
    return parsed;
  }
  try {
    parsed.request.requested_destination = WorldPoint{destination["x"].get<double>(),
                                                       destination["y"].get<double>(),
                                                       destination["z"].get<double>()};
  } catch (const Json::exception&) {
    parsed.error = failure(parsed.request, 400, "invalid_coordinates",
                           "Destination x, y, and z must be finite numbers.");
    return parsed;
  }
  if (!valid_point(*parsed.request.requested_destination)) {
    parsed.error = failure(parsed.request, 400, "coordinates_out_of_range",
                           "The destination is outside verified Palpagos bounds.");
  }
  return parsed;
}

}  // namespace

std::string_view admin_action_name(const AdminActionKind action) noexcept {
  switch (action) {
    case AdminActionKind::teleport_admin_to_player:
      return "teleportAdminToPlayer";
    case AdminActionKind::teleport_player_to_admin:
      return "teleportPlayerToAdmin";
    case AdminActionKind::teleport_player_to_location:
      return "teleportPlayerToLocation";
  }
  return "unknown";
}

std::string AdminActionResponse::json() const {
  Json body{{"schemaVersion", "1"},
            {"requestId", request_id},
            {"action", admin_action_name(action)},
            {"status", status},
            {"replayed", replayed},
            {"sourceCoordinateSpace",
             source_coordinate_space.empty() ? Json(nullptr) : Json(source_coordinate_space)},
            {"destinationCoordinateSpace",
             destination_coordinate_space.empty() ? Json(nullptr)
                                                    : Json(destination_coordinate_space)},
            {"resolvedDestination",
             resolved_destination ? point_json(*resolved_destination) : Json(nullptr)}};
  if (error.empty()) {
    body["error"] = nullptr;
    body["message"] = message.empty() ? "Teleport completed." : message;
  } else {
    body["error"] = error;
    body["message"] = message;
  }
  return body.dump();
}

AdminActionService::AdminActionService(CompanionConfig config,
                                       std::shared_ptr<AdminActionExecutor> executor,
                                       std::filesystem::path audit_path, LogSink log_sink,
                                       const std::size_t maximum_audit_bytes)
    : config_(std::move(config)),
      executor_(std::move(executor)),
      audit_path_(std::move(audit_path)),
      log_sink_(std::move(log_sink)),
      maximum_audit_bytes_(std::max<std::size_t>(maximum_audit_bytes, 64 * 1024)) {
  load_idempotency_records();
}

AdminActionService::~AdminActionService() { shutdown(); }

bool AdminActionService::configured(const AdminActionKind action) const noexcept {
  if (!config_.admin_actions_enabled) return false;
  switch (action) {
    case AdminActionKind::teleport_admin_to_player:
      return config_.teleport_admin_to_player_enabled;
    case AdminActionKind::teleport_player_to_admin:
      return config_.teleport_player_to_admin_enabled;
    case AdminActionKind::teleport_player_to_location:
      return config_.teleport_player_to_location_enabled;
  }
  return false;
}

bool AdminActionService::advertised(const AdminActionKind action) const noexcept {
  return configured(action) && executor_ && executor_->supports(action);
}

std::string AdminActionService::capabilities_json() const {
  const Json body{
      {"schemaVersion", "1"},
      {"categories",
       {{"events", {{"supported", false}, {"capabilityVersion", "1"}}},
        {"playerActivity", {{"supported", true}, {"capabilityVersion", "1"}}},
        {"playerLocations", {{"supported", true}, {"capabilityVersion", "1"}}},
        {"coordinateSpaces", {{"supported", true}, {"capabilityVersion", "1"}}},
        {"adminActions",
         {{"supported", advertised(AdminActionKind::teleport_admin_to_player) ||
                            advertised(AdminActionKind::teleport_player_to_admin) ||
                            advertised(AdminActionKind::teleport_player_to_location)},
          {"capabilityVersion", "1"},
          {"actions",
           {{"teleportAdminToPlayer",
             advertised(AdminActionKind::teleport_admin_to_player)},
            {"teleportPlayerToAdmin",
             advertised(AdminActionKind::teleport_player_to_admin)},
            {"teleportPlayerToLocation",
             advertised(AdminActionKind::teleport_player_to_location)}}}}},
        {"guilds", {{"supported", false}, {"capabilityVersion", "1"}}},
        {"bases", {{"supported", false}, {"capabilityVersion", "1"}}},
        {"performance", {{"supported", false}, {"capabilityVersion", "1"}}},
        {"moderation", {{"supported", false}, {"capabilityVersion", "1"}}},
        {"administration",
         {{"supported", false}, {"capabilityVersion", "1"}}},
        {"health", {{"supported", true}, {"capabilityVersion", "1"}}},
        {"version", {{"supported", true}, {"capabilityVersion", "1"}}}}}};
  return body.dump();
}

void AdminActionService::complete(const std::shared_ptr<SharedResult>& result,
                                  AdminActionResponse response) {
  {
    std::scoped_lock lock(result->mutex);
    result->response = std::move(response);
  }
  result->completed.notify_all();
}

AdminActionResponse AdminActionService::handle(const AdminActionKind action,
                                               const std::string_view request_body) {
  auto parsed = parse_request(action, request_body);
  const auto request_fingerprint = fingerprint(parsed.request);
  if (parsed.error) {
    audit(parsed.request, *parsed.error);
    return *parsed.error;
  }

  std::shared_ptr<SharedResult> shared;
  bool replay = false;
  {
    std::scoped_lock lock(idempotency_mutex_);
    if (const auto existing = idempotency_.find(parsed.request.request_id);
        existing != idempotency_.end()) {
      if (existing->second.fingerprint != request_fingerprint) {
        auto response = failure(parsed.request, 409, "idempotency_conflict",
                                "requestId was already used for a different action payload.");
        audit(parsed.request, response);
        return response;
      }
      shared = existing->second.result;
      replay = true;
    } else {
      shared = std::make_shared<SharedResult>();
      idempotency_.emplace(parsed.request.request_id,
                           IdempotencyEntry{request_fingerprint, shared});
      idempotency_order_.push_back(parsed.request.request_id);
      while (idempotency_order_.size() > maximum_idempotency_records) {
        const auto oldest = std::move(idempotency_order_.front());
        idempotency_order_.pop_front();
        idempotency_.erase(oldest);
      }
    }
  }

  if (replay) {
    std::unique_lock lock(shared->mutex);
    shared->completed.wait(lock, [&shared] { return shared->response.has_value(); });
    auto response = *shared->response;
    response.replayed = true;
    lock.unlock();
    audit(parsed.request, response, "replayed_" + response.status);
    return response;
  }

  if (!configured(action)) {
    auto response = failure(parsed.request, 403, "admin_action_disabled",
                            "This teleport action is disabled by server configuration.");
    complete(shared, response);
    audit(parsed.request, response);
    return response;
  }
  if (!executor_ || !executor_->supports(action)) {
    auto response = failure(parsed.request, 501, "action_not_supported",
                            "The installed Companion cannot safely execute this action.");
    complete(shared, response);
    audit(parsed.request, response);
    return response;
  }

  AdminActionResponse requested;
  requested.http_status = 202;
  requested.request_id = parsed.request.request_id;
  requested.action = parsed.request.action;
  requested.status = "requested";
  requested.destination_coordinate_space = parsed.request.destination_coordinate_space;
  requested.message = "The teleport request was accepted for game-thread dispatch.";
  if (!audit(parsed.request, requested)) {
    auto response = failure(parsed.request, 503, "audit_unavailable",
                            "The action was not executed because its audit record could not be persisted.");
    complete(shared, response);
    return response;
  }

  {
    std::scoped_lock lock(queue_mutex_);
    if (stopping_) {
      auto response = failure(parsed.request, 503, "companion_stopping",
                              "The Companion is stopping and did not execute the action.");
      complete(shared, response);
      audit(parsed.request, response);
      return response;
    }
    pending_.push_back({parsed.request, shared});
  }

  std::unique_lock lock(shared->mutex);
  if (!shared->completed.wait_for(lock, dispatch_timeout,
                                  [&shared] { return shared->response.has_value(); })) {
    if (!shared->claimed) {
      shared->cancelled = true;
      shared->response = failure(parsed.request, 503, "game_thread_unavailable",
                                 "The game thread did not accept the action before the deadline.");
      shared->completed.notify_all();
    } else {
      shared->completed.wait(lock, [&shared] { return shared->response.has_value(); });
    }
  }
  const auto response = *shared->response;
  lock.unlock();
  audit(parsed.request, response);
  return response;
}

void AdminActionService::audit_authentication_rejection(
    const AdminActionKind action, const std::string_view request_body) {
  auto parsed = parse_request(action, request_body);
  auto response = failure(parsed.request, 401, "authentication_required",
                          "Valid bearer authentication is required.");
  audit(parsed.request, response);
}

void AdminActionService::process_pending(const std::size_t maximum_actions) noexcept {
  for (std::size_t processed = 0; processed < maximum_actions; ++processed) {
    PendingAction pending;
    {
      std::scoped_lock lock(queue_mutex_);
      if (stopping_ || pending_.empty()) return;
      pending = std::move(pending_.front());
      pending_.pop_front();
    }
    {
      std::scoped_lock lock(pending.result->mutex);
      if (pending.result->cancelled || pending.result->response) continue;
      pending.result->claimed = true;
    }

    AdminActionExecutionResult execution;
    try {
      execution = executor_->execute(pending.request);
    } catch (...) {
      execution = {false, "execution_failed", "The game rejected the teleport.", {}, {},
                   std::nullopt};
    }
    AdminActionResponse response;
    response.request_id = pending.request.request_id;
    response.action = pending.request.action;
    response.source_coordinate_space = execution.source_coordinate_space;
    response.destination_coordinate_space = execution.destination_coordinate_space;
    response.resolved_destination = execution.resolved_destination;
    response.message = execution.message;
    if (execution.succeeded) {
      response.http_status = 200;
      response.status = "succeeded";
    } else {
      response.http_status = execution_http_status(execution.error);
      response.status = "rejected";
      response.error = execution.error.empty() ? "execution_failed" : execution.error;
    }
    complete(pending.result, std::move(response));
  }
}

void AdminActionService::shutdown() noexcept {
  std::deque<PendingAction> abandoned;
  {
    std::scoped_lock lock(queue_mutex_);
    if (stopping_) return;
    stopping_ = true;
    abandoned.swap(pending_);
  }
  for (auto& pending : abandoned) {
    complete(pending.result,
             failure(pending.request, 503, "companion_stopping",
                     "The Companion stopped before executing the action."));
  }
}

bool AdminActionService::audit(const AdminActionRequest& request,
                               const AdminActionResponse& response,
                               const std::string_view result_override) {
  try {
    Json record{{"timestamp", format_utc(std::chrono::system_clock::now())},
                {"requestId", request.request_id},
                {"action", admin_action_name(request.action)},
                {"administratorPlayerId",
                 request.administrator_player_id.empty() ? Json(nullptr)
                                                          : Json(request.administrator_player_id)},
                {"targetPlayerId",
                 request.target_player_id ? Json(*request.target_player_id) : Json(nullptr)},
                {"sourceCoordinateSpace",
                 response.source_coordinate_space.empty()
                     ? Json(nullptr)
                     : Json(response.source_coordinate_space)},
                {"destinationCoordinateSpace",
                 response.destination_coordinate_space.empty()
                     ? Json(nullptr)
                     : Json(response.destination_coordinate_space)},
                {"requestedDestination",
                 request.requested_destination ? point_json(*request.requested_destination)
                                               : Json(nullptr)},
                {"resolvedDestination",
                 response.resolved_destination ? point_json(*response.resolved_destination)
                                               : Json(nullptr)},
                {"result", result_override.empty() ? response.status
                                                   : std::string(result_override)},
                {"failureReason", response.error.empty() ? Json(nullptr) : Json(response.error)},
                {"message", response.message},
                {"httpStatus", response.http_status},
                {"idempotencyTracked", idempotency_tracked(response)},
                {"requestFingerprint", fingerprint(request)}};
    const auto line = record.dump() + '\n';
    std::scoped_lock lock(audit_mutex_);
    std::error_code error;
    const auto current_size = std::filesystem::exists(audit_path_, error)
                                  ? std::filesystem::file_size(audit_path_, error)
                                  : 0;
    if (!error && current_size + line.size() > maximum_audit_bytes_) {
      auto backup = audit_path_;
      backup += ".1";
      std::filesystem::remove(backup, error);
      error.clear();
      std::filesystem::rename(audit_path_, backup, error);
      if (error) throw std::filesystem::filesystem_error("rotate audit", audit_path_, error);
    }
    std::ofstream output(audit_path_, std::ios::app | std::ios::binary);
    if (!output) throw std::runtime_error("Unable to open the admin-action audit file");
    output << line;
    output.flush();
    if (!output) throw std::runtime_error("Unable to persist an admin-action audit record");
    return true;
  } catch (const std::exception& error) {
    log_sink_(LogLevel::error, "Unable to persist admin-action audit record: " +
                                   std::string(error.what()));
    return false;
  }
}

void AdminActionService::load_idempotency_records() {
  const auto load = [this](const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
      try {
        const auto record = Json::parse(line);
        if (!record.contains("requestId") || !record["requestId"].is_string() ||
            !record.contains("requestFingerprint") ||
            !record["requestFingerprint"].is_string() ||
            !record.contains("httpStatus") || !record["httpStatus"].is_number_integer()) {
          continue;
        }
        if (!record.value("idempotencyTracked", false)) continue;
        const auto request_id = record["requestId"].get<std::string>();
        if (!valid_request_id(request_id)) continue;
        AdminActionResponse response;
        response.http_status = record["httpStatus"].get<int>();
        response.request_id = request_id;
        const auto action = record.value("action", std::string{});
        if (action == "teleportAdminToPlayer") {
          response.action = AdminActionKind::teleport_admin_to_player;
        } else if (action == "teleportPlayerToAdmin") {
          response.action = AdminActionKind::teleport_player_to_admin;
        } else if (action == "teleportPlayerToLocation") {
          response.action = AdminActionKind::teleport_player_to_location;
        } else {
          continue;
        }
        response.status = record.value("result", std::string{"rejected"});
        if (response.status.starts_with("replayed_")) response.status.erase(0, 9);
        if (record.contains("failureReason") && record["failureReason"].is_string()) {
          response.error = record["failureReason"].get<std::string>();
        }
        response.message = record.value("message", std::string{});
        if (response.status == "requested") {
          response.http_status = 409;
          response.status = "rejected";
          response.error = "action_outcome_unknown";
          response.message =
              "This request was accepted before restart; it will not be executed again.";
        }
        if (record.contains("sourceCoordinateSpace") &&
            record["sourceCoordinateSpace"].is_string()) {
          response.source_coordinate_space =
              record["sourceCoordinateSpace"].get<std::string>();
        }
        if (record.contains("destinationCoordinateSpace") &&
            record["destinationCoordinateSpace"].is_string()) {
          response.destination_coordinate_space =
              record["destinationCoordinateSpace"].get<std::string>();
        }
        auto shared = std::make_shared<SharedResult>();
        shared->response = response;
        if (!idempotency_.contains(request_id)) idempotency_order_.push_back(request_id);
        idempotency_[request_id] =
            {record["requestFingerprint"].get<std::string>(), std::move(shared)};
      } catch (const Json::exception&) {
      }
    }
  };
  auto backup = audit_path_;
  backup += ".1";
  load(backup);
  load(audit_path_);
  while (idempotency_order_.size() > maximum_idempotency_records) {
    const auto oldest = std::move(idempotency_order_.front());
    idempotency_order_.pop_front();
    idempotency_.erase(oldest);
  }
}

}  // namespace palcenter::companion
