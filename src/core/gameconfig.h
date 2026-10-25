#pragma once
#include "memory_module.h"
#include <nlohmann/json.hpp>
#include <span>
namespace cs2_sdk::gameconfig {
struct OffsetBinding
{
    int* target;
    const char* name;
};

// Requires a JSON object; returns false on open or parse failure.
bool LoadGamedata(const char* path, nlohmann::json& out);
// Missing or non-string signatures return an empty string.
std::string FindPlatformSig(const nlohmann::json& gamedata, const std::string& name);
// Returns fallback for missing, non-integer or out-of-range offsets.
int FindPlatformOffset(const nlohmann::json& gamedata, const std::string& name, int fallback);
// Overrides bindings from gamedata, retaining each current value when unavailable.
void LoadOffsets(const nlohmann::json& gamedata, std::span<const OffsetBinding> offsets);
// Gamedata uses "windows" or "linux", not the module filename suffix.
const char* PlatformName();
// Requires exactly one match in the supplied module view.
void* ResolveSig(const nlohmann::json& gamedata, const modules::ModuleInfo& module, const char* name, char* errorOut, size_t errorOutLen);
} // namespace cs2_sdk::gameconfig
