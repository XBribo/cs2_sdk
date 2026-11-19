#include "gameconfig.h"
#include "../utils/platform.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>
namespace cs2_sdk::gameconfig {
namespace {
// Writes an optional diagnostic.
void SetError(char* out, size_t outLen, const char* fmt, const char* a, const char* b = nullptr)
{
    if (!out || outLen == 0) return;
    if (b) std::snprintf(out, outLen, fmt, a, b);
    else
        std::snprintf(out, outLen, fmt, a);
}
} // namespace
std::string ComputePath()
{
    std::filesystem::path path(platform::SelfModulePath());
    if (path.empty()) return "";
    for (int i = 0; i < 3; ++i)
    {
        path = path.parent_path();
        if (path.empty()) return "";
    }
    return (path / "gamedata.json").string();
}

bool LoadGamedata(const char* path, nlohmann::json& out)
{
    return LoadGamedata(path, out, nullptr, 0);
}
bool LoadGamedata(const char* path, nlohmann::json& out, char* error, size_t maxlen)
{
    if (!path || !*path)
    {
        SetError(error, maxlen, "%s", "gamedata path is empty");
        return false;
    }
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open())
    {
        SetError(error, maxlen, "cannot open gamedata: %s", path);
        return false;
    }
    try
    {
        auto parsed = nlohmann::json::parse(ifs);
        if (!parsed.is_object())
        {
            SetError(error, maxlen, "gamedata root must be an object: %s", path);
            return false;
        }
        out = std::move(parsed);
    }
    catch (const nlohmann::json::exception& ex)
    {
        SetError(error, maxlen, "invalid gamedata JSON in %s: %s", path, ex.what());
        return false;
    }
    return true;
}
const char* PlatformName()
{
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}
std::string FindPlatformSig(const nlohmann::json& gamedata, const std::string& name)
{
    auto it = gamedata.find(name);
    if (it == gamedata.end() || !it->is_object()) return "";
    auto sigIt = it->find("signatures");
    if (sigIt == it->end() || !sigIt->is_object()) return "";
    auto platformIt = sigIt->find(PlatformName());
    if (platformIt == sigIt->end() || !platformIt->is_string()) return "";
    return platformIt->get<std::string>();
}
int FindPlatformOffset(const nlohmann::json& gamedata, const std::string& name, int fallback)
{
    auto it = gamedata.find(name);
    if (it == gamedata.end() || !it->is_object()) return fallback;
    auto offIt = it->find("offsets");
    if (offIt == it->end() || !offIt->is_object()) return fallback;
    auto platformIt = offIt->find(PlatformName());
    if (platformIt == offIt->end() || !platformIt->is_number_integer()) return fallback;
    if (platformIt->is_number_unsigned())
    {
        const auto value = platformIt->get<nlohmann::json::number_unsigned_t>();
        return std::in_range<int>(value) ? static_cast<int>(value) : fallback;
    }
    const auto value = platformIt->get<nlohmann::json::number_integer_t>();
    return std::in_range<int>(value) ? static_cast<int>(value) : fallback;
}
void LoadOffsets(const nlohmann::json& gamedata, std::span<const OffsetBinding> offsets)
{
    for (const OffsetBinding& offset : offsets)
        *offset.target = FindPlatformOffset(gamedata, offset.name, *offset.target);
}

bool LoadRequiredOffsets(const nlohmann::json& gamedata, std::span<const OffsetBinding> offsets, char* error, size_t maxlen)
{
    std::string invalid;
    for (const OffsetBinding& offset : offsets)
    {
        const int value = FindPlatformOffset(gamedata, offset.name, -1);
        if (value >= 0)
        {
            *offset.target = value;
            continue;
        }
        *offset.target = -1;
        if (!invalid.empty()) invalid += ", ";
        invalid += '\'';
        invalid += offset.name;
        invalid += '\'';
    }
    if (invalid.empty()) return true;
    SetError(error, maxlen, "Required gamedata offsets missing or invalid (%s): %s", PlatformName(), invalid.c_str());
    return false;
}

void* ResolveSig(const nlohmann::json& gamedata, const modules::ModuleInfo& module, const char* name, char* errorOut, size_t errorOutLen)
{
    std::string sig = FindPlatformSig(gamedata, name);
    if (sig.empty())
    {
        SetError(errorOut, errorOutLen, "gamedata missing '%s.signatures.%s'", name, PlatformName());
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    std::vector<bool> wild;
    if (!modules::ParseSigString(sig, bytes, wild))
    {
        SetError(errorOut, errorOutLen, "failed to parse '%s' sig: '%s'", name, sig.c_str());
        return nullptr;
    }
    if (!module)
    {
        SetError(errorOut, errorOutLen, "module unavailable while resolving '%s'", name);
        return nullptr;
    }
    const auto matches = modules::TryFindPatternMatchesIn(module, bytes, wild);
    if (!matches)
    {
        SetError(errorOut, errorOutLen, "KHook unavailable while resolving '%s'", name);
        return nullptr;
    }
    if (matches->size() != 1)
    {
        SetError(errorOut, errorOutLen, "sig '%s' expected one executable match, got %s", name, matches->empty() ? "0" : "2 or more");
        return nullptr;
    }
    return matches->front();
}
} // namespace cs2_sdk::gameconfig
