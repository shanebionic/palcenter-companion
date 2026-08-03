#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Windows.h>
#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using StartMod = RC::CppUserModBase* (*)();
using UninstallMod = void (*)(RC::CppUserModBase*);

template <typename Function>
Function exported_function(const HMODULE module, const char* name) {
  const auto address = GetProcAddress(module, name);
  if (address == nullptr) {
    throw std::runtime_error(std::string("Missing export: ") + name);
  }
  return reinterpret_cast<Function>(address);
}

}  // namespace

int main() {
  const auto dll_path = std::filesystem::current_path() / "PalCenterCompanion-contract-test.dll";
  const auto module = LoadLibraryW(dll_path.c_str());
  if (module == nullptr) {
    std::cerr << "Unable to load contract-test DLL.\n";
    return 1;
  }

  RC::CppUserModBase* companion = nullptr;
  UninstallMod uninstall_mod = nullptr;
  try {
    const auto start_mod = exported_function<StartMod>(module, "start_mod");
    uninstall_mod = exported_function<UninstallMod>(module, "uninstall_mod");
    companion = start_mod();
    if (companion == nullptr) {
      throw std::runtime_error("start_mod returned null");
    }

    companion->on_unreal_init();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    httplib::Client client("127.0.0.1", 18213);
    const auto health = client.Get("/palcenter/v1/health");
    if (!health || health->status != 200 ||
        health->body.find("\"status\":\"healthy\"") == std::string::npos) {
      throw std::runtime_error("embedded health endpoint did not become healthy");
    }

    uninstall_mod(companion);
    companion = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const auto after_unload = client.Get("/palcenter/v1/health");
    if (after_unload) {
      throw std::runtime_error("listener remained available after uninstall_mod");
    }

    FreeLibrary(module);
    std::cout << "UE4SS adapter contract smoke test passed.\n";
    return 0;
  } catch (const std::exception& error) {
    if (companion != nullptr && uninstall_mod != nullptr) {
      uninstall_mod(companion);
    }
    FreeLibrary(module);
    std::cerr << "UE4SS adapter smoke failure: " << error.what() << '\n';
    return 1;
  }
}
