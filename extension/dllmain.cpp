#include "palcenter_companion/application.hpp"
#include "palcenter_companion/logger.hpp"

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Windows.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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
    ModVersion = STR("0.1.0");
    ModDescription = STR("Optional authoritative server extension for PalCenter");
    ModAuthors = STR("PalCenter Companion contributors");
  }

  ~PalCenterCompanionMod() override { application_.reset(); }

  auto on_unreal_init() -> void override {
    try {
      if (!application_) {
        application_ = std::make_unique<CompanionApplication>(log_to_ue4ss);
      }
      const auto config_path = companion_directory() / "config" / "PalCenterCompanion.ini";
      static_cast<void>(application_->initialize(config_path));
    } catch (const std::exception& error) {
      log_to_ue4ss(LogLevel::error,
                   "Companion initialization failed; the Palworld server will continue: " +
                       std::string(error.what()));
      application_.reset();
    }
  }

 private:
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
