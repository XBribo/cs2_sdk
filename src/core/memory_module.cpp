#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "memory_module.h"
#include <khook.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifdef _M_AMD64
#ifndef _AMD64_
#define _AMD64_ // NOLINT(bugprone-reserved-identifier)
#endif
#endif
#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <psapi.h>
#include <winnt.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

#include <tier0/platform.h>
#include "metamod_oslink.h"
#ifdef snprintf
#undef snprintf
#endif

namespace cs2_sdk::modules {
namespace {
struct ModuleStorage
{
    std::unique_ptr<CModule> engine;
    std::unique_ptr<CModule> server;
};

// Releases remaining references on plugin unload.
ModuleStorage& Storage()
{
    static ModuleStorage storage;
    return storage;
}

#ifdef _WIN32
// Discovers data and executable sections.
ModuleInfo ModuleFromHandle(HINSTANCE handle)
{
    ModuleInfo out;
    if (!handle) return out;

    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), handle, &moduleInfo, sizeof(moduleInfo))) return out;

    out.base = static_cast<unsigned char*>(moduleInfo.lpBaseOfDll);
    out.size = static_cast<size_t>(moduleInfo.SizeOfImage);
    out.segments.push_back({ .base = out.base, .size = out.size });
    return out;
}

#else
// Adds one mapped ELF segment and updates the module bounds.
void AddSegment(ModuleInfo& module, uintptr_t address, size_t size)
{
    if (size == 0) return;

    auto* base = reinterpret_cast<unsigned char*>(address);
    module.segments.push_back({ .base = base, .size = size });

    if (!module.base || address < reinterpret_cast<uintptr_t>(module.base)) module.base = base;

    const uintptr_t end = address + size;
    const uintptr_t currentEnd = reinterpret_cast<uintptr_t>(module.base) + module.size;
    if (end > currentEnd) module.size = static_cast<size_t>(end - reinterpret_cast<uintptr_t>(module.base));
}

// Use the loader's view: the file at l_name may have been replaced since dlopen.
bool FillModuleFromHandle(HINSTANCE handle, ModuleInfo& image, ModuleInfo& code)
{
    link_map* map = nullptr;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0 || !map) return false;

    struct Views
    {
        ElfW(Addr) base;
        ModuleInfo& image;
        ModuleInfo& code;
    } views{ map->l_addr, image, code };
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* context) {
        auto& views = *static_cast<Views*>(context);
        if (info->dlpi_addr != views.base) return 0;
        for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i)
        {
            const auto& header = info->dlpi_phdr[i];
            if (header.p_type != PT_LOAD) continue;
            const uintptr_t address = info->dlpi_addr + header.p_vaddr;
            AddSegment(views.image, address, header.p_memsz);
            if (header.p_flags & PF_X) AddSegment(views.code, address, header.p_memsz);
        }
        return 1;
    }, &views);
    return static_cast<bool>(image);
}
#endif
} // namespace

CModule::CModule(const char* relativeDirectory, const char* moduleName)
{
    if (!relativeDirectory || !moduleName || !moduleName[0]) return;

    const char* gameDirectory = Plat_GetGameDirectory();
    if (!gameDirectory || !gameDirectory[0]) return;

    m_path = std::string(gameDirectory) + relativeDirectory + kModulePrefix + moduleName + kModuleExtension;
    m_hModule.reset(dlmount(m_path.c_str()));
    if (!m_hModule) return;

#ifdef _WIN32
    m_image = ModuleFromHandle(static_cast<HMODULE>(m_hModule.get()));
    m_code = ExecutableSections(m_image);
    if (!m_image) m_hModule.reset();
#else
    if (!FillModuleFromHandle(static_cast<HINSTANCE>(m_hModule.get()), m_image, m_code)) m_hModule.reset();
#endif
}

void CModule::HandleDeleter::operator()(void* handle) const { dlclose(static_cast<HINSTANCE>(handle)); }

CModule* engine = nullptr;
CModule* server = nullptr;

void Initialize()
{
    auto& storage = Storage();
    if (!storage.engine || !*storage.engine) storage.engine = std::make_unique<CModule>(kRootBin, "engine2");
    engine = storage.engine.get();
    if (!storage.server || !*storage.server) storage.server = std::make_unique<CModule>(kGameBin, "server");
    server = storage.server.get();
}

void Shutdown()
{
    engine = nullptr;
    server = nullptr;
    auto& storage = Storage();
    storage.server.reset();
    storage.engine.reset();
}

bool ParseSigString(const std::string& sigStr, std::vector<uint8_t>& outBytes, std::vector<bool>& outWild)
{
    outBytes.clear();
    outWild.clear();
    const char* p = sigStr.c_str();
    while (*p)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        if (*p == '?')
        {
            outBytes.push_back(0);
            outWild.push_back(true);
            ++p;
            if (*p == '?') ++p;
            continue;
        }
        char* end = nullptr;
        const uint64_t value = std::strtoull(p, &end, 16);
        if (end == p || end - p > 2 || value > 0xFF) return false;
        outBytes.push_back(static_cast<uint8_t>(value));
        outWild.push_back(false);
        p = end;
    }
    return !outBytes.empty();
}

void* FindPatternIn(const ModuleInfo& module, const std::vector<uint8_t>& pattern, const std::vector<bool>& wild)
{
    const auto matches = FindPatternMatchesIn(module, pattern, wild);
    return matches.size() == 1 ? matches.front() : nullptr;
}

std::vector<void*>
FindPatternMatchesIn(const ModuleInfo& module, const std::vector<uint8_t>& pattern, const std::vector<bool>& wild, size_t maxMatches)
{
    return TryFindPatternMatchesIn(module, pattern, wild, maxMatches).value_or(std::vector<void*>{});
}

std::optional<std::vector<void*>>
TryFindPatternMatchesIn(const ModuleInfo& module, const std::vector<uint8_t>& pattern, const std::vector<bool>& wild, size_t maxMatches)
{
    if (!KHook::__exported__khook) return std::nullopt;
    std::vector<void*> matches;
    if (!module || pattern.empty() || pattern.size() != wild.size()) return matches;
    constexpr char hex[] = "0123456789ABCDEF";
    std::string signature;
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (i) signature += ' ';
        if (wild[i]) signature += '?';
        else
        {
            signature += hex[pattern[i] >> 4];
            signature += hex[pattern[i] & 15];
        }
    }
    for (const ModuleSegment& segment : module.segments)
    {
        if (!segment.base || segment.size < pattern.size()) continue;
        size_t offset = 0;
        while (offset <= segment.size - pattern.size())
        {
            // KHook's size counts candidate starts, so exclude starts crossing the segment end.
            auto* match = static_cast<unsigned char*>(
                KHook::LookupSignature(segment.base + offset, segment.size - offset - pattern.size() + 1, signature.c_str()));
            if (!match) break;
            matches.push_back(match);
            if (maxMatches && matches.size() == maxMatches) return matches;
            offset = static_cast<size_t>(match - segment.base) + 1;
        }
    }
    return matches;
}

} // namespace cs2_sdk::modules
