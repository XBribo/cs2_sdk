#pragma once

#include <string>

#ifdef _MSC_VER
#define CS2_SDK_FASTCALL __fastcall
#else
#define CS2_SDK_FASTCALL
#endif

namespace cs2_sdk::platform {
// Returns the path of this plugin module, or empty on failure.
std::string SelfModulePath();
} // namespace cs2_sdk::platform
