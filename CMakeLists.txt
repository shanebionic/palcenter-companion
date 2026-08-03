cmake_minimum_required(VERSION 3.22)

project(PalCenterCompanion VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

option(PALCENTER_BUILD_TESTS "Build the platform-neutral Companion tests" ON)
option(PALCENTER_BUILD_UE4SS_EXTENSION "Build the UE4SS C++ extension DLL" OFF)
option(PALCENTER_USE_UE4SS_TEST_STUBS "Use test-only UE4SS lifecycle declarations" OFF)
set(PALCENTER_UE4SS_ROOT "" CACHE PATH "Path to a compatible RE-UE4SS source checkout")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_library(
  palcenter_companion_core
  STATIC
  src/application.cpp
  src/config.cpp
  src/http_server.cpp
)
target_include_directories(palcenter_companion_core PUBLIC include)
target_link_libraries(palcenter_companion_core PUBLIC httplib::httplib)
target_compile_definitions(
  palcenter_companion_core
  PUBLIC
    PALCENTER_COMPANION_VERSION="${PROJECT_VERSION}"
    PALCENTER_COMPANION_API_VERSION="v1"
)
set_target_properties(palcenter_companion_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(palcenter_companion_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(palcenter_companion_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(PALCENTER_BUILD_UE4SS_EXTENSION)
  if(NOT WIN32)
    message(FATAL_ERROR "The UE4SS Companion extension is a Windows DLL. Linux hosts require Wine or Proton.")
  endif()
  if(PALCENTER_USE_UE4SS_TEST_STUBS)
    add_library(UE4SS INTERFACE)
    target_include_directories(UE4SS INTERFACE tests/ue4ss_stubs)
  else()
    if(NOT EXISTS "${PALCENTER_UE4SS_ROOT}/CMakeLists.txt")
      message(FATAL_ERROR "Set PALCENTER_UE4SS_ROOT to an authorized compatible RE-UE4SS source checkout.")
    endif()
    add_subdirectory("${PALCENTER_UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/ue4ss" EXCLUDE_FROM_ALL)
  endif()

  add_library(PalCenterCompanion SHARED extension/dllmain.cpp)
  target_link_libraries(PalCenterCompanion PRIVATE palcenter_companion_core UE4SS)
  target_compile_definitions(PalCenterCompanion PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  set_target_properties(PalCenterCompanion PROPERTIES PREFIX "")
  if(PALCENTER_USE_UE4SS_TEST_STUBS)
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
  endif()

  install(TARGETS PalCenterCompanion RUNTIME DESTINATION PalCenterCompanion/dlls)
  install(FILES config/PalCenterCompanion.ini DESTINATION PalCenterCompanion/config)
  install(FILES extension/enabled.txt DESTINATION PalCenterCompanion)
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
