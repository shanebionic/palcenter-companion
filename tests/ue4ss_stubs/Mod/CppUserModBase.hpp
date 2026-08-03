#pragma once

#include <string>

#define STR(value) L##value

namespace RC {

class CppUserModBase {
 public:
  virtual ~CppUserModBase() = default;
  virtual auto on_unreal_init() -> void {}

  std::wstring ModName;
  std::wstring ModVersion;
  std::wstring ModDescription;
  std::wstring ModAuthors;
};

}  // namespace RC
