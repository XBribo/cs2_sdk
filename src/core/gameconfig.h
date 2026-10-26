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

// Builds the <plugin>/gamedata.json path.
std::string ComputePath();
// Requires a JSON object; returns false on open or parse failure.
bool LoadGamedata(const char* path, nlohmann::json& out);
// Missing or non-string signatures return an empty string.
std::string FindPlatformSig(const nlohmann::json& gamedata, const std::string& name);
// Returns fallback for missing, non-integer or out-of-range offsets.
int FindPlatformOffset(const nlohmann::json& gamedata, const std::string& name, int fallback);
// Applies valid gamedata offsets, preserving other bindings.
void LoadOffsets(const nlohmann::json& gamedata, std::span<const OffsetBinding> offsets);
// Returns the gamedata platform key: "windows" or "linux".
const char* PlatformName();
// Requires exactly one match in the supplied module view.
void* ResolveSig(const nlohmann::json& gamedata, const modules::ModuleInfo& module, const char* name, char* errorOut, size_t errorOutLen);
} // namespace cs2_sdk::gameconfig
