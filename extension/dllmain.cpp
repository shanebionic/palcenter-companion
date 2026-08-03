#include "palcenter_companion/application.hpp"
#include "palcenter_companion/logger.hpp"

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef PALCENTER_PRODUCTION_HOOKS
#include <Helpers/String.hpp>
#include <Unreal/AActor.hpp>
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

}  // namespace

class PalCenterCompanionMod final : public RC::CppUserModBase {
 public:
  PalCenterCompanionMod() {
    ModName = STR("PalCenter Companion");
    ModVersion = STR("0.3.0");
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
        application_ = std::make_unique<CompanionApplication>(log_to_ue4ss);
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

 private:
#ifdef PALCENTER_PRODUCTION_HOOKS
  struct ControllerParameter { RC::Unreal::UObject* controller; };

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

  static void left(RC::Unreal::UnrealScriptFunctionCallableContext& context,
                   void* custom_data) {
    auto* application = static_cast<CompanionApplication*>(custom_data);
    const auto& parameters = context.GetParams<ControllerParameter>();
    const auto identity = identity_from_controller(parameters.controller);
    if (!identity.player_id.empty()) static_cast<void>(application->player_left(identity.player_id));
  }

  void register_player_hooks() {
    if (!application_ || hooks_registered_) return;
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
          if (!application_ || pending_joins_.empty()) return;
          const auto now = std::chrono::steady_clock::now();
          std::erase_if(pending_joins_, [this, now](const PendingJoin& pending) {
            const auto identity = identity_from_controller(pending.controller);
            if (is_resolved_identity(identity)) {
              static_cast<void>(application_->player_joined(identity));
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
  }

  std::pair<int, int> logout_hooks_{};
  struct PendingJoin {
    RC::Unreal::UObject* controller;
    std::chrono::steady_clock::time_point deadline;
  };
  static constexpr std::size_t maximum_pending_joins_{256};
  static constexpr std::chrono::seconds identity_resolution_timeout_{30};
  std::vector<PendingJoin> pending_joins_;
  RC::Unreal::Hook::GlobalCallbackId end_play_hook_{RC::Unreal::Hook::ERROR_ID};
  RC::Unreal::Hook::GlobalCallbackId begin_play_hook_{RC::Unreal::Hook::ERROR_ID};
  RC::Unreal::Hook::GlobalCallbackId engine_tick_hook_{RC::Unreal::Hook::ERROR_ID};
  bool hooks_registered_{false};
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
