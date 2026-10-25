#include "gameconfig.h"
#include <cstdio>
#include <fstream>
#include <utility>
#include <vector>
namespace cs2_sdk::gameconfig {
namespace {
// The caller may omit the diagnostic buffer.
void SetError(char* out, size_t outLen, const char* fmt, const char* a, const char* b = nullptr)
{
    if (!out || outLen == 0) return;
    if (b) std::snprintf(out, outLen, fmt, a, b);
    else
        std::snprintf(out, outLen, fmt, a);
}
} // namespace
bool LoadGamedata(const char* path, nlohmann::json& out)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    try
    {
        out = nlohmann::json::parse(ifs);
    }
    catch (...)
    {
        return false;
    }
    return out.is_object();
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
    const auto matches = modules::FindPatternMatchesIn(module, bytes, wild);
    if (matches.size() != 1)
    {
        if (errorOut && errorOutLen)
            std::snprintf(errorOut, errorOutLen, "sig '%s' expected one executable match, got %s", name,
                          matches.empty() ? "0" : "2 or more");
        return nullptr;
    }
    return matches.front();
}
} // namespace cs2_sdk::gameconfig
