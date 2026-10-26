#include "memory.h"
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace cs2_sdk::memory {
namespace {
// Validates the address range before OS access.
bool ValidRange(const void* address, size_t size) { return address && size && size <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(address); }

// Checks current mappings across the complete range.
bool HasAccess(const void* address, size_t size, bool executable)
{
    if (!ValidRange(address, size)) return false;
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = cursor + size;
#ifdef _WIN32
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = info.Protect & 0xFF;
        const bool allowed = executable ? protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                                              protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY
                                        : protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                                              protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                                              protection == PAGE_EXECUTE_WRITECOPY;
        const auto begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (!allowed || info.RegionSize > UINTPTR_MAX - begin || begin + info.RegionSize <= cursor) return false;
        cursor = begin + info.RegionSize;
    }
    return true;
#else
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return false;
    char line[512];
    while (cursor < end && std::fgets(line, sizeof(line), maps))
    {
        unsigned long begin = 0;
        unsigned long limit = 0;
        char permissions[5]{};
        if (std::sscanf(line, "%lx-%lx %4s", &begin, &limit, permissions) != 3 || limit <= cursor) continue;
        if (begin > cursor || permissions[executable ? 2 : 0] != (executable ? 'x' : 'r')) break;
        cursor = limit;
    }
    std::fclose(maps);
    return cursor >= end;
#endif
}
} // namespace

bool ReadBytes(const void* address, void* output, size_t size)
{
    if (!ValidRange(address, size) || !output) return false;
#ifdef _WIN32
    __try
    {
        std::memcpy(output, address, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    const iovec local{ output, size };
    const iovec remote{ const_cast<void*>(address), size };
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#endif
}

bool WriteBytes(void* address, const void* value, size_t size)
{
    if (!ValidRange(address, size) || !value) return false;
#ifdef _WIN32
    __try
    {
        std::memcpy(address, value, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    const iovec local{ const_cast<void*>(value), size };
    const iovec remote{ address, size };
    return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#endif
}

bool IsReadable(const void* address, size_t size) { return HasAccess(address, size, false); }

bool IsExecutableAddress(const void* address) { return HasAccess(address, 1, true); }

void* ResolveRelativeAddress(const void* instruction, size_t displacementOffset, size_t instructionLength)
{
    if (!instruction || instructionLength < sizeof(int32_t) || displacementOffset > instructionLength - sizeof(int32_t)) return nullptr;
    const auto begin = reinterpret_cast<uintptr_t>(instruction);
    if (instructionLength > UINTPTR_MAX - begin) return nullptr;
    int32_t displacement = 0;
    if (!Read(instruction, displacementOffset, displacement)) return nullptr;
    const uintptr_t end = begin + instructionLength;
    if (displacement >= 0)
    {
        const auto distance = static_cast<uintptr_t>(displacement);
        return distance <= UINTPTR_MAX - end ? reinterpret_cast<void*>(end + distance) : nullptr;
    }
    const auto distance = static_cast<uintptr_t>(-static_cast<int64_t>(displacement));
    return distance <= end ? reinterpret_cast<void*>(end - distance) : nullptr;
}
} // namespace cs2_sdk::memory
