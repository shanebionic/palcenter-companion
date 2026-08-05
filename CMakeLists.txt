cmake_minimum_required(VERSION 3.22)

project(PalCenterCompanion VERSION 0.3.0 LANGUAGES CXX)

include(FetchContent)

option(PALCENTER_BUILD_TESTS "Build the platform-neutral Companion tests" ON)
set(PALCENTER_UE4SS_BUILD_MODE "OFF" CACHE STRING "UE4SS DLL mode: OFF, CONTRACT, or PRODUCTION")
set_property(CACHE PALCENTER_UE4SS_BUILD_MODE PROPERTY STRINGS OFF CONTRACT PRODUCTION)
set(PALCENTER_UE4SS_ROOT "" CACHE PATH "Path to a compatible RE-UE4SS source checkout")

set(PALCENTER_PINNED_UE4SS_COMMIT "c838a8acaade1a0f860bdf249f039e58f4e10088")
set(PALCENTER_PINNED_WINDOWS_SDK "10.0.26100.0")
# The pinned PatternSleuth submodule declares nightly; this is the nightly
# snapshot contemporaneous with the pinned UE4SS revision.
set(PALCENTER_PINNED_RUST_VERSION "1.97.0-nightly")
set(PALCENTER_BUILD_COMMIT "unknown" CACHE STRING "Source commit embedded in diagnostics")
set(PALCENTER_BUILD_BRANCH "unknown" CACHE STRING "Source branch embedded in diagnostics")
string(TIMESTAMP PALCENTER_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Pinned for reproducible builds. The HTTP library is embedded in the Companion;
# it does not create a separate process or service.
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG 095a5c1caf9e467ff840ee2ffd19be1a7852203b
  GIT_SHALLOW TRUE
)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpp_httplib)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(
  palcenter_companion_core
  STATIC
  src/admin_actions.cpp
  src/application.cpp
  src/authentication.cpp
  src/config.cpp
  src/http_server.cpp
  src/player_activity.cpp
  src/player_location.cpp
)
target_include_directories(palcenter_companion_core PUBLIC include)
target_link_libraries(palcenter_companion_core PUBLIC httplib::httplib nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(palcenter_companion_core PUBLIC bcrypt)
endif()
target_compile_definitions(
  palcenter_companion_core
  PUBLIC
    PALCENTER_COMPANION_VERSION="${PROJECT_VERSION}"
    PALCENTER_COMPANION_API_VERSION="v1"
    PALCENTER_BUILD_COMMIT="${PALCENTER_BUILD_COMMIT}"
    PALCENTER_BUILD_BRANCH="${PALCENTER_BUILD_BRANCH}"
    PALCENTER_BUILD_DATE="${PALCENTER_BUILD_DATE}"
  PRIVATE
    CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=2048
    CPPHTTPLIB_HEADER_MAX_LENGTH=8192
)
set_target_properties(palcenter_companion_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(palcenter_companion_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(palcenter_companion_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT PALCENTER_UE4SS_BUILD_MODE STREQUAL "OFF")
  if(NOT WIN32)
    message(FATAL_ERROR "The UE4SS Companion extension is a Windows DLL. Linux hosts require Wine or Proton.")
  endif()
  if(NOT PALCENTER_UE4SS_BUILD_MODE MATCHES "^(CONTRACT|PRODUCTION)$")
    message(FATAL_ERROR "PALCENTER_UE4SS_BUILD_MODE must be OFF, CONTRACT, or PRODUCTION.")
  endif()

  if(PALCENTER_UE4SS_BUILD_MODE STREQUAL "CONTRACT")
    add_library(UE4SS INTERFACE)
    target_include_directories(UE4SS INTERFACE tests/ue4ss_stubs)
  else()
    if(NOT EXISTS "${PALCENTER_UE4SS_ROOT}/CMakeLists.txt")
      message(FATAL_ERROR
        "Production mode requires -DPALCENTER_UE4SS_ROOT=<authorized RE-UE4SS checkout>. "
        "The path must contain CMakeLists.txt and initialized access-controlled submodules."
      )
    endif()
    if(NOT EXISTS "${PALCENTER_UE4SS_ROOT}/deps/first/Unreal/CMakeLists.txt")
      message(FATAL_ERROR
        "The authorized UEPseudo dependency is missing. Initialize the pinned RE-UE4SS submodules "
        "with your Epic-linked GitHub credentials; do not copy it into this repository."
      )
    endif()
    execute_process(
      COMMAND git -C "${PALCENTER_UE4SS_ROOT}" rev-parse HEAD
      OUTPUT_VARIABLE PALCENTER_UE4SS_ACTUAL_COMMIT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE PALCENTER_UE4SS_GIT_RESULT
    )
    if(NOT PALCENTER_UE4SS_GIT_RESULT EQUAL 0 OR
       NOT PALCENTER_UE4SS_ACTUAL_COMMIT STREQUAL PALCENTER_PINNED_UE4SS_COMMIT)
      message(FATAL_ERROR
        "Unsupported RE-UE4SS checkout. Expected ${PALCENTER_PINNED_UE4SS_COMMIT}, "
        "found '${PALCENTER_UE4SS_ACTUAL_COMMIT}'."
      )
    endif()
    if(NOT CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022" OR NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
      message(FATAL_ERROR "Production mode requires -G \"Visual Studio 17 2022\" -A x64.")
    endif()
    if(NOT MSVC_VERSION EQUAL 1944)
      message(FATAL_ERROR "Production mode is pinned to the MSVC 19.44 toolset (MSVC_VERSION 1944).")
    endif()
    if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL PALCENTER_PINNED_WINDOWS_SDK)
      message(FATAL_ERROR
        "Production mode requires Windows SDK ${PALCENTER_PINNED_WINDOWS_SDK}; "
        "configure with -DCMAKE_SYSTEM_VERSION=${PALCENTER_PINNED_WINDOWS_SDK}."
      )
    endif()
    find_program(PALCENTER_RUSTC_EXECUTABLE rustc REQUIRED)
    execute_process(
      COMMAND "${PALCENTER_RUSTC_EXECUTABLE}" --version
      OUTPUT_VARIABLE PALCENTER_RUSTC_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT PALCENTER_RUSTC_VERSION_OUTPUT MATCHES "^rustc ${PALCENTER_PINNED_RUST_VERSION} ")
      message(FATAL_ERROR
        "Production mode requires rustc ${PALCENTER_PINNED_RUST_VERSION}; "
        "found '${PALCENTER_RUSTC_VERSION_OUTPUT}'."
      )
    endif()
    add_subdirectory("${PALCENTER_UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/ue4ss" EXCLUDE_FROM_ALL)
  endif()

  add_library(PalCenterCompanion SHARED extension/dllmain.cpp)
  target_link_libraries(PalCenterCompanion PRIVATE palcenter_companion_core UE4SS)
  target_compile_definitions(PalCenterCompanion PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  set_target_properties(PalCenterCompanion PROPERTIES PREFIX "")
  if(PALCENTER_UE4SS_BUILD_MODE STREQUAL "CONTRACT")
    set_target_properties(PalCenterCompanion PROPERTIES OUTPUT_NAME "PalCenterCompanion-contract-test")
    configure_file(
      tests/fixtures/PalCenterCompanion.ini
      "${CMAKE_BINARY_DIR}/config/PalCenterCompanion.ini"
      COPYONLY
    )
    add_executable(palcenter_ue4ss_adapter_smoke tests/ue4ss_adapter_smoke.cpp)
    target_include_directories(palcenter_ue4ss_adapter_smoke PRIVATE tests/ue4ss_stubs)
    target_link_libraries(palcenter_ue4ss_adapter_smoke PRIVATE httplib::httplib)
    target_compile_definitions(palcenter_ue4ss_adapter_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_dependencies(palcenter_ue4ss_adapter_smoke PalCenterCompanion)
  else()
    target_compile_definitions(PalCenterCompanion PRIVATE PALCENTER_PRODUCTION_HOOKS=1)
    set_target_properties(PalCenterCompanion PROPERTIES OUTPUT_NAME "main")
  endif()

  install(TARGETS PalCenterCompanion RUNTIME DESTINATION PalCenterCompanion/dlls)
  install(FILES config/PalCenterCompanion.ini DESTINATION PalCenterCompanion/config)
  install(FILES extension/enabled.txt DESTINATION PalCenterCompanion)
  install(FILES packaging/README.txt DESTINATION PalCenterCompanion)
  install(FILES docs/ADMIN-ACTIONS-UAT.md DESTINATION PalCenterCompanion)
  install(FILES packaging/LICENSES/THIRD-PARTY-NOTICES.txt DESTINATION PalCenterCompanion/LICENSES)
endif()

if(PALCENTER_BUILD_TESTS)
  enable_testing()
  add_executable(palcenter_companion_tests tests/companion_tests.cpp)
  target_link_libraries(palcenter_companion_tests PRIVATE palcenter_companion_core)
  add_test(NAME palcenter_companion_tests COMMAND palcenter_companion_tests)
  if(TARGET palcenter_ue4ss_adapter_smoke)
    add_test(NAME palcenter_ue4ss_adapter_smoke COMMAND palcenter_ue4ss_adapter_smoke)
    set_tests_properties(
      palcenter_ue4ss_adapter_smoke
      PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:palcenter_ue4ss_adapter_smoke>"
    )
  endif()
endif()
