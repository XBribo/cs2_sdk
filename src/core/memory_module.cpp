#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "memory_module.h"
#include <khook.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <tier0/platform.h>
#include "metamod_oslink.h"
#ifdef snprintf
#undef snprintf
#endif

namespace cs2_sdk::modules {
namespace {
#ifdef _WIN32
// Includes data as well as executable sections.
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

// Resolves ELF load segments directly from the handle's link_map.
bool FillModuleFromHandle(HINSTANCE handle, ModuleInfo& image, ModuleInfo& code)
{
    if (!handle) return false;

    link_map* linkMap = nullptr;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &linkMap) != 0 || !linkMap || !linkMap->l_name || !linkMap->l_name[0]) return false;

    const int fileDescriptor = open(linkMap->l_name, O_RDONLY);
    if (fileDescriptor == -1) return false;

    struct stat fileStatus{};
    if (fstat(fileDescriptor, &fileStatus) != 0 || fileStatus.st_size <= 0)
    {
        close(fileDescriptor);
        return false;
    }

    const size_t fileSize = static_cast<size_t>(fileStatus.st_size);
    void* mappedFile = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
    if (mappedFile == MAP_FAILED)
    {
        close(fileDescriptor);
        return false;
    }

    if (fileSize < sizeof(ElfW(Ehdr)))
    {
        munmap(mappedFile, fileSize);
        close(fileDescriptor);
        return false;
    }

    auto* elfHeader = static_cast<ElfW(Ehdr)*>(mappedFile);
    const size_t programHeaderOffset = static_cast<size_t>(elfHeader->e_phoff);
    const size_t programHeaderSize = static_cast<size_t>(elfHeader->e_phnum) * elfHeader->e_phentsize;
    const bool validElf = std::memcmp(elfHeader->e_ident, ELFMAG, SELFMAG) == 0 && elfHeader->e_ident[EI_CLASS] == ELFCLASS64 &&
                          programHeaderOffset <= fileSize && programHeaderSize <= fileSize - programHeaderOffset;
    if (!validElf)
    {
        munmap(mappedFile, fileSize);
        close(fileDescriptor);
        return false;
    }

    auto* programHeaders = reinterpret_cast<ElfW(Phdr)*>(static_cast<unsigned char*>(mappedFile) + programHeaderOffset);
    for (int i = 0; i < elfHeader->e_phnum; ++i)
    {
        const ElfW(Phdr) & programHeader = programHeaders[i];
        if (programHeader.p_type != PT_LOAD || programHeader.p_memsz == 0) continue;

        const uintptr_t address = static_cast<uintptr_t>(linkMap->l_addr + programHeader.p_vaddr);
        const size_t size = static_cast<size_t>(programHeader.p_memsz);
        AddSegment(image, address, size);
        if ((programHeader.p_flags & PF_X) != 0) AddSegment(code, address, size);
    }

    munmap(mappedFile, fileSize);
    close(fileDescriptor);
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
    m_hModule = dlmount(m_path.c_str());
    if (!m_hModule) return;

#ifdef _WIN32
    m_image = ModuleFromHandle(reinterpret_cast<HMODULE>(m_hModule));
    m_code = ExecutableSections(m_image);
#else
    if (!FillModuleFromHandle(static_cast<HINSTANCE>(m_hModule), m_image, m_code))
    {
        dlclose(m_hModule);
        m_hModule = nullptr;
    }
#endif
}

CModule* engine = nullptr;
CModule* server = nullptr;

void Initialize()
{
    if (!engine) engine = new CModule(kRootBin, "engine2");
    if (!server) server = new CModule(kGameBin, "server");
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
    std::vector<void*> matches;
    if (!module || pattern.empty() || pattern.size() != wild.size() || !KHook::__exported__khook) return matches;
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
