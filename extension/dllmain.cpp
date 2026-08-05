#include "palcenter_companion/application.hpp"
#include "palcenter_companion/logger.hpp"

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef PALCENTER_PRODUCTION_HOOKS
#include <Helpers/String.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/BPMacros.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#endif

namespace palcenter::companion::ue4ss {
namespace {

void module_anchor() {}

std::filesystem::path companion_directory() {
  HMODULE module = nullptr;
  const auto address = reinterpret_cast<LPCWSTR>(&module_anchor);
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        address,
                        &module) == 0) {
    throw std::runtime_error("unable to locate Companion module");
  }

  std::wstring path(32768, L'\0');
  const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length == path.size()) {
    throw std::runtime_error("unable to resolve Companion module path");
  }
  path.resize(length);
  return std::filesystem::path(path).parent_path().parent_path();
}

void log_to_ue4ss(const LogLevel level, const std::string_view message) {
  const std::wstring wide_message(message.begin(), message.end());
  switch (level) {
    case LogLevel::debug:
      RC::Output::send<RC::LogLevel::Verbose>(STR("[PalCenterCompanion] {}\n"), wide_message);
      break;
    case LogLevel::information:
      RC::Output::send<RC::LogLevel::Normal>(STR("[PalCenterCompanion] {}\n"), wide_message);
      break;
    case LogLevel::warning:
      RC::Output::send<RC::LogLevel::Warning>(STR("[PalCenterCompanion] {}\n"), wide_message);
      break;
    case LogLevel::error:
      RC::Output::send<RC::LogLevel::Error>(STR("[PalCenterCompanion] {}\n"), wide_message);
      break;
  }
}

#ifdef PALCENTER_PRODUCTION_HOOKS
enum class MapTeleportSupportState {
  runtime_initializing,
  collision_function_unavailable,
  collision_signature_mismatch,
  utility_object_unavailable,
  floor_function_unavailable,
  floor_signature_mismatch,
  ocean_function_unavailable,
  ocean_signature_mismatch,
  probe_failed,
  ready
};

std::string_view support_diagnostic_code(const MapTeleportSupportState state) noexcept {
  switch (state) {
    case MapTeleportSupportState::runtime_initializing:
      return "safe_placement_runtime_initializing";
    case MapTeleportSupportState::collision_function_unavailable:
      return "collision_teleport_function_unavailable";
    case MapTeleportSupportState::collision_signature_mismatch:
      return "collision_teleport_signature_mismatch";
    case MapTeleportSupportState::utility_object_unavailable:
      return "safe_placement_utility_unavailable";
    case MapTeleportSupportState::floor_function_unavailable:
      return "safe_floor_function_unavailable";
    case MapTeleportSupportState::floor_signature_mismatch:
      return "safe_floor_signature_mismatch";
    case MapTeleportSupportState::ocean_function_unavailable:
      return "ocean_check_function_unavailable";
    case MapTeleportSupportState::ocean_signature_mismatch:
      return "ocean_check_signature_mismatch";
    case MapTeleportSupportState::probe_failed:
      return "safe_placement_probe_failed";
    case MapTeleportSupportState::ready:
      return {};
  }
  return "safe_placement_probe_failed";
}
#endif

}  // namespace

class PalCenterCompanionMod final : public RC::CppUserModBase, public AdminActionExecutor {
 public:
  PalCenterCompanionMod() {
    ModName = STR("PalCenter Companion");
    ModVersion = STR("0.3.2");
    ModDescription = STR("Optional authoritative server extension for PalCenter");
    ModAuthors = STR("PalCenter Companion contributors");
  }

  ~PalCenterCompanionMod() override {
#ifdef PALCENTER_PRODUCTION_HOOKS
    unregister_player_hooks();
#endif
    application_.reset();
  }

  auto on_unreal_init() -> void override {
    try {
      if (!application_) {
        auto executor = std::shared_ptr<AdminActionExecutor>(this, [](AdminActionExecutor*) {});
        application_ =
            std::make_unique<CompanionApplication>(log_to_ue4ss, std::move(executor));
      }
      const auto config_path = companion_directory() / "config" / "PalCenterCompanion.ini";
      static_cast<void>(application_->initialize(config_path));
#ifdef PALCENTER_PRODUCTION_HOOKS
      register_player_hooks();
#endif
    } catch (const std::exception& error) {
      log_to_ue4ss(LogLevel::error,
                   "Companion initialization failed; the Palworld server will continue: " +
                       std::string(error.what()));
      application_.reset();
    }
  }

  [[nodiscard]] bool supports(const AdminActionKind action) const noexcept override {
#ifdef PALCENTER_PRODUCTION_HOOKS
    if (action == AdminActionKind::teleport_player_to_location) {
      return location_teleport_supported_;
    }
    return player_teleport_supported_;
#else
    static_cast<void>(action);
    return false;
#endif
  }

  [[nodiscard]] std::string_view unsupported_reason(
      const AdminActionKind action) const noexcept override {
#ifdef PALCENTER_PRODUCTION_HOOKS
    if (action == AdminActionKind::teleport_player_to_location) {
      return support_diagnostic_code(location_support_state_.load());
    }
    return player_teleport_supported_.load() ? std::string_view{}
                                             : "collision_teleport_function_unavailable";
#else
    static_cast<void>(action);
    return "production_runtime_unavailable";
#endif
  }

  AdminActionExecutionResult execute(const AdminActionRequest& request) noexcept override {
#ifdef PALCENTER_PRODUCTION_HOOKS
    try {
      return execute_teleport(request);
    } catch (const std::exception&) {
      return {false, "execution_failed", "The game rejected the teleport.", {}, {},
              std::nullopt};
    } catch (...) {
      return {false, "execution_failed", "The game rejected the teleport.", {}, {},
              std::nullopt};
    }
#else
    static_cast<void>(request);
    return {false, "action_not_supported",
            "This build cannot execute gameplay actions.", {}, {}, std::nullopt};
#endif
  }

 private:
#ifdef PALCENTER_PRODUCTION_HOOKS
  struct ControllerParameter { RC::Unreal::UObject* controller; };
  struct StageInstanceId { RC::Unreal::FGuid internal_id; bool valid; };
  struct ActivePlayer { RC::Unreal::UObject* controller; PlayerIdentity identity; };

  static bool resolve_safe_floor(RC::Unreal::AActor* TargetActor,
                                 const RC::Unreal::FVector InLocation,
                                 RC::Unreal::FVector& OutLocation) {
    using namespace RC::Unreal;
    const float UpOffset = 0.0F;
    const bool ShortRayLength = false;
    const bool PriorityWater = false;
    const bool onlyCheckWater = false;
    UE_BEGIN_NATIVE_FUNCTION_BODY(
        "/Script/Pal.PalUtility:CanAdjustActorToFloorAtLocation")
    UE_SET_STATIC_SELF("/Script/Pal.Default__PalUtility")
    if (!StaticSelf) throw std::runtime_error("PalUtility is unavailable");
    UE_COPY_PROPERTY(TargetActor, RC::Unreal::AActor*)
    UE_COPY_STRUCT_PROPERTY_BEGIN(InLocation)
    UE_COPY_VECTOR(InLocation, InLocation)
    UE_COPY_PROPERTY(UpOffset, float)
    UE_COPY_PROPERTY(ShortRayLength, bool)
    UE_COPY_PROPERTY(PriorityWater, bool)
    UE_COPY_PROPERTY(onlyCheckWater, bool)
    UE_CALL_STATIC_FUNCTION()
    UE_COPY_OUT_PROPERTY(OutLocation, RC::Unreal::FVector)
    UE_RETURN_PROPERTY(bool)
  }

  static bool is_below_ocean_plane(RC::Unreal::UObject* WorldContextObject,
                                   const RC::Unreal::FVector Location) {
    using namespace RC::Unreal;
    UE_BEGIN_NATIVE_FUNCTION_BODY("/Script/Pal.PalUtility:IsUnderWorldOceanPlaneZ")
    UE_SET_STATIC_SELF("/Script/Pal.Default__PalUtility")
    if (!StaticSelf) throw std::runtime_error("PalUtility is unavailable");
    UE_COPY_PROPERTY(WorldContextObject, RC::Unreal::UObject*)
    UE_COPY_STRUCT_PROPERTY_BEGIN(Location)
    UE_COPY_VECTOR(Location, Location)
    UE_CALL_STATIC_FUNCTION()
    UE_RETURN_PROPERTY(bool)
  }

  struct ExpectedParameter {
    std::string_view name;
    std::string_view type;
    bool output{false};
    bool returns{false};
  };

  static bool function_signature_matches(
      RC::Unreal::UFunction* function,
      const std::initializer_list<ExpectedParameter> expected) {
    if (!function) return false;
    auto item = expected.begin();
    std::size_t count = 0;
    bool matches = true;
    for (RC::Unreal::FProperty* property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
             function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
      using namespace RC::Unreal;
      if (!property->HasAnyPropertyFlags(CPF_Parm)) continue;
      if (item == expected.end()) {
        matches = false;
        continue;
      }
      const auto returns = property->HasAnyPropertyFlags(CPF_ReturnParm);
      const auto constant = property->HasAnyPropertyFlags(CPF_ConstParm);
      const auto output = property->HasAnyPropertyFlags(CPF_OutParm) && !constant && !returns;
      const auto name = RC::to_string(property->GetName());
      const auto type = RC::to_string(property->GetClass().GetFName().ToString());
      matches = matches && name == item->name && type == item->type &&
                output == item->output && returns == item->returns;
      ++item;
      ++count;
    }
    return matches && count == expected.size();
  }

  static std::string describe_function_signature(RC::Unreal::UFunction* function) {
    if (!function) return "unavailable";
    std::ostringstream description;
    bool first = true;
    for (RC::Unreal::FProperty* property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
             function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
      using namespace RC::Unreal;
      if (!property->HasAnyPropertyFlags(CPF_Parm)) continue;
      if (!first) description << ", ";
      first = false;
      const auto returns = property->HasAnyPropertyFlags(CPF_ReturnParm);
      const auto constant = property->HasAnyPropertyFlags(CPF_ConstParm);
      const auto reference = property->HasAnyPropertyFlags(CPF_ReferenceParm);
      const auto output = property->HasAnyPropertyFlags(CPF_OutParm) && !constant && !returns;
      description << RC::to_string(property->GetName()) << ':'
                  << RC::to_string(property->GetClass().GetFName().ToString()) << ':'
                  << (returns ? "return"
                              : output ? "out"
                              : constant && reference ? "const-ref"
                                                     : constant ? "const" : "in");
    }
    return first ? "no parameters" : description.str();
  }

  static RC::Unreal::AActor* pawn_actor(RC::Unreal::UObject* controller) {
    if (!controller) return nullptr;
    const auto pawn = controller->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        STR("Pawn"));
    return pawn && *pawn ? RC::Unreal::Cast<RC::Unreal::AActor>(*pawn) : nullptr;
  }

  static std::string player_id_from_guid(const RC::Unreal::FGuid& guid) {
    std::ostringstream value;
    value << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << guid.A
          << std::setw(8) << guid.B << std::setw(8) << guid.C << std::setw(8) << guid.D;
    return value.str();
  }

  static bool is_resolved_identity(const PlayerIdentity& identity) {
    return !identity.player_id.empty() &&
           identity.player_id != "00000000000000000000000000000000" &&
           !identity.player_name.empty() && identity.player_name != identity.player_id;
  }

  static PlayerIdentity identity_from_state(RC::Unreal::UObject* state) {
    PlayerIdentity identity;
    if (!state) return identity;
    if (const auto player_uid =
            state->GetValuePtrByPropertyNameInChain<RC::Unreal::FGuid>(STR("PlayerUId"))) {
      identity.player_id = player_id_from_guid(*player_uid);
    }
    if (const auto name = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FString>(
            STR("PlayerNamePrivate")); name && !name->IsEmpty()) {
      identity.player_name = RC::to_string(**name);
    } else {
      const auto account_name = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FString>(
          STR("AccountName"));
      identity.player_name = account_name && !account_name->IsEmpty()
                                 ? RC::to_string(**account_name)
                                 : identity.player_id;
    }
    return identity;
  }

  static PlayerIdentity identity_from_controller(RC::Unreal::UObject* controller) {
    if (!controller) return {};
    const auto state_pointer = controller->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        STR("PlayerState"));
    return identity_from_state(state_pointer ? *state_pointer : nullptr);
  }

  static std::optional<PlayerLocation> location_from_controller(const ActivePlayer& player) {
    PlayerLocation result;
    result.player = player.identity;
    result.captured_at = std::chrono::system_clock::now();
    if (!player.controller) return std::nullopt;
    const auto pawn_pointer = player.controller->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(STR("Pawn"));
    if (!pawn_pointer || !*pawn_pointer) return std::nullopt;
    const auto state_pointer = player.controller->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(STR("PlayerState"));
    auto* state = state_pointer ? *state_pointer : nullptr;
    if (!state) return std::nullopt;
    const auto position = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FVector>(STR("CachedPlayerLocation"));
    if (!position) return std::nullopt;
    result.x = position->X();
    result.y = position->Y();
    result.z = position->Z();
    const auto record_pointer = state->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(STR("RecordData"));
    auto* record = record_pointer ? *record_pointer : nullptr;
    if (record) {
      if (const auto stage = record->GetValuePtrByPropertyNameInChain<StageInstanceId>(STR("EnteringStageInstanceId")); stage && stage->valid) {
        result.area = PlayerAreaKind::special_area;
        result.stage_instance_id = player_id_from_guid(stage->internal_id);
      }
    }
    return result;
  }

  std::pair<ActivePlayer*, std::string> find_online_player(const std::string_view player_id) {
    ActivePlayer* match = nullptr;
    std::size_t matches = 0;
    for (auto& player : active_players_) {
      if (player.identity.player_id == player_id) {
        match = &player;
        ++matches;
      }
    }
    if (matches == 0) return {nullptr, "player_offline"};
    if (matches > 1) return {nullptr, "player_ambiguous"};
    return {match, {}};
  }

  static AdminActionExecutionResult player_failure(const std::string_view role,
                                                    const std::string& error) {
    if (error == "player_ambiguous") {
      return {false, error,
              "The " + std::string(role) + " player ID is ambiguous in the live game state.",
              {}, {}, std::nullopt};
    }
    return {false, "player_offline",
            "The " + std::string(role) + " character is not online.", {}, {},
            std::nullopt};
  }

  AdminActionExecutionResult execute_teleport(const AdminActionRequest& request) {
    const auto [administrator, administrator_error] =
        find_online_player(request.administrator_player_id);
    if (!administrator) return player_failure("administrator", administrator_error);
    if (!request.target_player_id) {
      return {false, "player_offline", "The target character is not online.", {}, {},
              std::nullopt};
    }
    const auto [target, target_error] = find_online_player(*request.target_player_id);
    if (!target) return player_failure("target", target_error);

    const auto administrator_location = location_from_controller(*administrator);
    const auto target_location = location_from_controller(*target);
    if (!administrator_location || !target_location || !pawn_actor(administrator->controller) ||
        !pawn_actor(target->controller)) {
      return {false, "player_state_stale",
              "A current gameplay state was not available for both characters.", {}, {},
              std::nullopt};
    }
    if (administrator_location->area != PlayerAreaKind::palpagos ||
        target_location->area != PlayerAreaKind::palpagos) {
      return {false, "special_area_not_supported",
              "Teleportation is unavailable while either character is in a special area.",
              administrator_location->area == PlayerAreaKind::palpagos ? "palpagos"
                                                                       : "special_area",
              target_location->area == PlayerAreaKind::palpagos ? "palpagos"
                                                                : "special_area",
              std::nullopt};
    }

    ActivePlayer* moving = target;
    const PlayerLocation* source = &*target_location;
    RC::Unreal::FVector destination;
    if (request.action == AdminActionKind::teleport_admin_to_player) {
      moving = administrator;
      source = &*administrator_location;
      destination = RC::Unreal::FVector(target_location->x, target_location->y,
                                        target_location->z);
    } else if (request.action == AdminActionKind::teleport_player_to_admin) {
      destination = RC::Unreal::FVector(administrator_location->x, administrator_location->y,
                                        administrator_location->z);
    } else {
      if (!request.requested_destination ||
          request.destination_coordinate_space != "palpagos") {
        return {false, "safe_destination_unavailable",
                "A verified Palpagos destination was not available.", "palpagos",
                request.destination_coordinate_space, std::nullopt};
      }
      auto* moving_actor = pawn_actor(moving->controller);
      if (!moving_actor) {
        return {false, "player_state_stale", "The moving character is no longer available.",
                "palpagos", "palpagos", std::nullopt};
      }
      const auto runtime_location = moving_actor->K2_GetActorLocation();
      RC::Unreal::FVector requested(request.requested_destination->x,
                                    request.requested_destination->y,
                                    runtime_location.Z());
      if (!resolve_safe_floor(moving_actor, requested, destination) ||
          is_below_ocean_plane(moving->controller, destination)) {
        return {false, "safe_destination_unavailable",
                "The game could not resolve a safe land destination.", "palpagos",
                "palpagos", std::nullopt};
      }
    }

    auto* actor = pawn_actor(moving->controller);
    if (!actor) {
      return {false, "player_state_stale", "The moving character is no longer available.",
              "palpagos", "palpagos", std::nullopt};
    }
    const auto rotation = actor->K2_GetActorRotation();
    if (!actor->K2_TeleportTo(destination, rotation)) {
      return {false, "teleport_rejected",
              "The game could not place the character safely at that destination.",
              "palpagos", "palpagos", std::nullopt};
    }
    const auto resolved = actor->K2_GetActorLocation();
    return {true,
            {},
            "Teleport completed.",
            source->area == PlayerAreaKind::palpagos ? "palpagos" : "special_area",
            "palpagos",
            WorldPoint{resolved.X(), resolved.Y(), resolved.Z()}};
  }

  void probe_admin_action_support() noexcept {
    try {
      using namespace RC::Unreal;
      auto* collision = UObjectGlobals::StaticFindObject<UFunction*>(
          nullptr, nullptr, STR("/Script/Engine.Actor:K2_TeleportTo"));
      auto* utility = UObjectGlobals::StaticFindObject<UObject*>(
          nullptr, nullptr, STR("/Script/Pal.Default__PalUtility"));
      auto* floor = UObjectGlobals::StaticFindObject<UFunction*>(
          nullptr, nullptr, STR("/Script/Pal.PalUtility:CanAdjustActorToFloorAtLocation"));
      auto* ocean = UObjectGlobals::StaticFindObject<UFunction*>(
          nullptr, nullptr, STR("/Script/Pal.PalUtility:IsUnderWorldOceanPlaneZ"));

      const auto collision_signature = function_signature_matches(
          collision, {{"DestLocation", "StructProperty"},
                      {"DestRotation", "StructProperty"},
                      {"ReturnValue", "BoolProperty", false, true}});
      const auto floor_signature = function_signature_matches(
          floor, {{"TargetActor", "ObjectProperty"},
                  {"InLocation", "StructProperty"},
                  {"UpOffset", "FloatProperty"},
                  {"OutLocation", "StructProperty", true},
                  {"ShortRayLength", "BoolProperty"},
                  {"PriorityWater", "BoolProperty"},
                  {"onlyCheckWater", "BoolProperty"},
                  {"ReturnValue", "BoolProperty", false, true}});
      const auto ocean_signature = function_signature_matches(
          ocean, {{"WorldContextObject", "ObjectProperty"},
                  {"Location", "StructProperty"},
                  {"ReturnValue", "BoolProperty", false, true}});

      player_teleport_supported_.store(collision != nullptr);
      MapTeleportSupportState state{MapTeleportSupportState::ready};
      if (!collision) {
        state = MapTeleportSupportState::collision_function_unavailable;
      } else if (!collision_signature) {
        state = MapTeleportSupportState::collision_signature_mismatch;
      } else if (!utility) {
        state = MapTeleportSupportState::utility_object_unavailable;
      } else if (!floor) {
        state = MapTeleportSupportState::floor_function_unavailable;
      } else if (!floor_signature) {
        state = MapTeleportSupportState::floor_signature_mismatch;
      } else if (!ocean) {
        state = MapTeleportSupportState::ocean_function_unavailable;
      } else if (!ocean_signature) {
        state = MapTeleportSupportState::ocean_signature_mismatch;
      }
      location_support_state_.store(state);
      location_teleport_supported_.store(state == MapTeleportSupportState::ready);

      unsigned int readiness = 0;
      readiness |= collision ? 1U << 0U : 0;
      readiness |= collision_signature ? 1U << 1U : 0;
      readiness |= utility ? 1U << 2U : 0;
      readiness |= floor ? 1U << 3U : 0;
      readiness |= floor_signature ? 1U << 4U : 0;
      readiness |= ocean ? 1U << 5U : 0;
      readiness |= ocean_signature ? 1U << 6U : 0;
      if (!support_probe_logged_ || readiness != last_support_readiness_) {
        const auto status = [](const bool ready) { return ready ? "ready" : "unavailable"; };
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite collision function: " +
                         std::string(status(collision != nullptr)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite collision signature: " +
                         std::string(status(collision_signature)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite Palworld utility object: " +
                         std::string(status(utility != nullptr)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite floor function: " +
                         std::string(status(floor != nullptr)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite floor signature: " +
                         std::string(status(floor_signature)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite ocean function: " +
                         std::string(status(ocean != nullptr)));
        log_to_ue4ss(LogLevel::information,
                     "Map teleport prerequisite ocean signature: " +
                         std::string(status(ocean_signature)));
        if (collision && !collision_signature) {
          log_to_ue4ss(LogLevel::warning,
                       "Map teleport collision runtime signature: " +
                           describe_function_signature(collision));
        }
        if (floor && !floor_signature) {
          log_to_ue4ss(LogLevel::warning,
                       "Map teleport floor runtime signature: " +
                           describe_function_signature(floor));
        }
        if (ocean && !ocean_signature) {
          log_to_ue4ss(LogLevel::warning,
                       "Map teleport ocean runtime signature: " +
                           describe_function_signature(ocean));
        }
        const auto code = support_diagnostic_code(state);
        log_to_ue4ss(
            state == MapTeleportSupportState::ready ? LogLevel::information
                                                    : LogLevel::warning,
            state == MapTeleportSupportState::ready
                ? "Map teleport runtime support: ready"
                : "Map teleport runtime support: unavailable (" + std::string(code) +
                      "); lifecycle probe will retry");
        support_probe_logged_ = true;
        last_support_readiness_ = readiness;
      }
    } catch (...) {
      location_teleport_supported_.store(false);
      location_support_state_.store(MapTeleportSupportState::probe_failed);
      if (!support_probe_logged_ || last_support_readiness_ != 0) {
        log_to_ue4ss(LogLevel::warning,
                     "Map teleport runtime support: probe failed; lifecycle probe will retry");
        support_probe_logged_ = true;
        last_support_readiness_ = 0;
      }
    }
  }

  static void left(RC::Unreal::UnrealScriptFunctionCallableContext& context,
                   void* custom_data) {
    auto* application = static_cast<CompanionApplication*>(custom_data);
    const auto& parameters = context.GetParams<ControllerParameter>();
    const auto identity = identity_from_controller(parameters.controller);
    if (!identity.player_id.empty()) static_cast<void>(application->player_left(identity.player_id));
  }

  void register_player_hooks() {
    if (!application_ || hooks_registered_) return;
    probe_admin_action_support();
    logout_hooks_ = RC::Unreal::UObjectGlobals::RegisterHook(
        STR("/Script/Engine.GameModeBase:K2_OnLogout"), left, {}, application_.get());
    RC::Unreal::Hook::FCallbackOptions options;
    options.bReadonly = true;
    options.OwnerModName = STR("PalCenterCompanion");
    options.HookName = STR("PlayerControllerEndPlay");
    end_play_hook_ = RC::Unreal::Hook::RegisterEndPlayPreCallback(
        [this](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::AActor* actor,
               RC::Unreal::EEndPlayReason) {
          if (!application_ || !actor) return;
          const auto actor_name = RC::to_string(actor->GetFullName());
          if (actor_name.find("PalPlayerState") != std::string::npos) {
            const auto identity = identity_from_state(actor);
            if (is_resolved_identity(identity)) {
              static_cast<void>(application_->player_left(identity.player_id));
              std::erase_if(active_players_, [&identity](const ActivePlayer& player) { return player.identity.player_id == identity.player_id; });
              log_to_ue4ss(LogLevel::debug, "Palworld player-state end-play hook fired");
            }
            return;
          }
          if (actor_name.find("PalPlayerController") == std::string::npos) return;
          std::erase_if(pending_joins_,
                        [actor](const PendingJoin& pending) { return pending.controller == actor; });
          const auto identity = identity_from_controller(actor);
          if (is_resolved_identity(identity)) {
            static_cast<void>(application_->player_left(identity.player_id));
            std::erase_if(active_players_, [actor](const ActivePlayer& player) { return player.controller == actor; });
            log_to_ue4ss(LogLevel::debug, "Palworld player controller end-play hook fired");
          }
        },
        std::move(options));
    RC::Unreal::Hook::FCallbackOptions begin_options;
    begin_options.bReadonly = true;
    begin_options.OwnerModName = STR("PalCenterCompanion");
    begin_options.HookName = STR("PlayerControllerBeginPlay");
    begin_play_hook_ = RC::Unreal::Hook::RegisterBeginPlayPostCallback(
        [this](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::AActor* actor) {
          if (!application_ || !actor) return;
          const auto actor_name = RC::to_string(actor->GetFullName());
          if (actor_name.find("PalPlayerController") == std::string::npos) return;
          if (pending_joins_.size() >= maximum_pending_joins_) {
            log_to_ue4ss(LogLevel::warning, "Player identity queue is full; join was not recorded");
            return;
          }
          const auto duplicate = std::ranges::any_of(
              pending_joins_, [actor](const PendingJoin& pending) { return pending.controller == actor; });
          if (!duplicate) {
            pending_joins_.push_back(
                {actor, std::chrono::steady_clock::now() + identity_resolution_timeout_});
            log_to_ue4ss(LogLevel::debug, "Palworld player controller begin-play hook fired");
          }
        },
        std::move(begin_options));
    RC::Unreal::Hook::FCallbackOptions tick_options;
    tick_options.bReadonly = true;
    tick_options.OwnerModName = STR("PalCenterCompanion");
    tick_options.HookName = STR("PendingPlayerIdentityResolution");
    engine_tick_hook_ = RC::Unreal::Hook::RegisterEngineTickPostCallback(
        [this](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float,
               bool) {
          if (!application_) return;
          application_->process_pending_admin_actions();
          const auto now = std::chrono::steady_clock::now();
          if (now >= next_support_probe_) {
            probe_admin_action_support();
            next_support_probe_ =
                now + (location_teleport_supported_.load()
                           ? supported_probe_interval_
                           : initializing_probe_interval_);
          }
          std::erase_if(pending_joins_, [this, now](const PendingJoin& pending) {
            const auto identity = identity_from_controller(pending.controller);
            if (is_resolved_identity(identity)) {
              static_cast<void>(application_->player_joined(identity));
              const auto active = std::ranges::any_of(active_players_, [&identity](const ActivePlayer& player) {
                return player.identity.player_id == identity.player_id;
              });
              if (!active) active_players_.push_back({pending.controller, identity});
              log_to_ue4ss(LogLevel::debug, "Palworld player identity resolved");
              return true;
            }
            if (now >= pending.deadline) {
              log_to_ue4ss(LogLevel::warning,
                           "Player identity was not available before the join deadline");
              return true;
            }
            return false;
          });
          if (now >= next_location_sample_) {
            for (const auto& player : active_players_) {
              if (auto location = location_from_controller(player)) {
                application_->update_player_location(std::move(*location));
              }
            }
            next_location_sample_ = now + location_sample_interval_;
          }
        },
        std::move(tick_options));
    hooks_registered_ = true;
    log_to_ue4ss(LogLevel::information, "Player activity hooks registered");
  }

  void unregister_player_hooks() noexcept {
    if (!hooks_registered_) return;
    try {
      RC::Unreal::UObjectGlobals::UnregisterHook(
          STR("/Script/Engine.GameModeBase:K2_OnLogout"), logout_hooks_);
      if (end_play_hook_ != RC::Unreal::Hook::ERROR_ID) {
        static_cast<void>(RC::Unreal::Hook::UnregisterCallback(end_play_hook_));
      }
      if (begin_play_hook_ != RC::Unreal::Hook::ERROR_ID) {
        static_cast<void>(RC::Unreal::Hook::UnregisterCallback(begin_play_hook_));
      }
      if (engine_tick_hook_ != RC::Unreal::Hook::ERROR_ID) {
        static_cast<void>(RC::Unreal::Hook::UnregisterCallback(engine_tick_hook_));
      }
    } catch (...) {
    }
    hooks_registered_ = false;
    player_teleport_supported_.store(false);
    location_teleport_supported_.store(false);
    location_support_state_.store(MapTeleportSupportState::runtime_initializing);
  }

  std::pair<int, int> logout_hooks_{};
  struct PendingJoin {
    RC::Unreal::UObject* controller;
    std::chrono::steady_clock::time_point deadline;
  };
  static constexpr std::size_t maximum_pending_joins_{256};
  static constexpr std::chrono::seconds identity_resolution_timeout_{30};
  static constexpr std::chrono::seconds location_sample_interval_{2};
  static constexpr std::chrono::seconds initializing_probe_interval_{2};
  static constexpr std::chrono::seconds supported_probe_interval_{30};
  std::vector<PendingJoin> pending_joins_;
  std::vector<ActivePlayer> active_players_;
  std::chrono::steady_clock::time_point next_location_sample_{};
  std::chrono::steady_clock::time_point next_support_probe_{};
  RC::Unreal::Hook::GlobalCallbackId end_play_hook_{RC::Unreal::Hook::ERROR_ID};
  RC::Unreal::Hook::GlobalCallbackId begin_play_hook_{RC::Unreal::Hook::ERROR_ID};
  RC::Unreal::Hook::GlobalCallbackId engine_tick_hook_{RC::Unreal::Hook::ERROR_ID};
  bool hooks_registered_{false};
  std::atomic<bool> player_teleport_supported_{false};
  std::atomic<bool> location_teleport_supported_{false};
  std::atomic<MapTeleportSupportState> location_support_state_{
      MapTeleportSupportState::runtime_initializing};
  bool support_probe_logged_{false};
  unsigned int last_support_readiness_{0};
#endif
  std::unique_ptr<CompanionApplication> application_;
};

}  // namespace palcenter::companion::ue4ss

#define PALCENTER_COMPANION_EXPORT __declspec(dllexport)

extern "C" {

PALCENTER_COMPANION_EXPORT RC::CppUserModBase* start_mod() {
  return new palcenter::companion::ue4ss::PalCenterCompanionMod();
}

PALCENTER_COMPANION_EXPORT void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }

}
