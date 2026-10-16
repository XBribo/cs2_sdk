#include "rtti.h"
#include "memory.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace cs2_sdk::rtti {
namespace {
using modules::ModuleInfo;

// The caller may omit the diagnostic buffer.
void SetError(char* error, size_t maxlen, const char* message, const char* name)
{
    if (error && maxlen) std::snprintf(error, maxlen, message, name);
}

// Uses KHook's original bytes, with no candidate limit.
std::vector<void*> FindExact(const ModuleInfo& module, const void* bytes, size_t size)
{
    const auto* first = static_cast<const uint8_t*>(bytes);
    return modules::FindPatternMatchesIn(module, std::vector<uint8_t>(first, first + size), std::vector<bool>(size, false), 0);
}

// Metadata must fit within the selected module segments.
template <typename T> bool ReadAt(const ModuleInfo& module, const void* address, T& value)
{
    return modules::ContainsRange(module, address, sizeof(T)) && memory::Read(address, 0, value);
}

// Checks subtraction before forming a metadata pointer.
const void* Before(const void* address, size_t distance)
{
    const auto value = reinterpret_cast<uintptr_t>(address);
    return value >= distance ? reinterpret_cast<const void*>(value - distance) : nullptr;
}

// Rejects unaligned/non-callable address points and deduplicates overlapping views.
void AddCandidate(
    const ModuleInfo& module, const void* header, ptrdiff_t offset, uint32_t construction, std::vector<VirtualTable>& candidates)
{
    const auto value = reinterpret_cast<uintptr_t>(header);
    if (value > UINTPTR_MAX - sizeof(void*)) return;
    const auto address = value + sizeof(void*);
    if (address % alignof(void*) != 0) return;
    auto** table = reinterpret_cast<void**>(address);
    void* first = nullptr;
    if (!ReadAt(module, table, first) || !memory::IsExecutableAddress(first)) return;
    if (std::ranges::any_of(candidates, [table](const VirtualTable& candidate) {
        return candidate.address == table;
    }))
        return;
    candidates.push_back({ table, offset, construction });
}

#ifdef _WIN32
struct Locator
{
    uint32_t signature;
    uint32_t offset;
    uint32_t construction;
    uint32_t type;
    uint32_t hierarchy;
    uint32_t self;
};
struct Hierarchy
{
    uint32_t signature;
    uint32_t attributes;
    uint32_t count;
    uint32_t bases;
};

// Resolves an image-relative metadata reference with overflow/bounds checks.
const void* FromRva(const ModuleInfo& module, uint32_t rva)
{
    const auto base = reinterpret_cast<uintptr_t>(module.base);
    return rva && rva < module.size && rva <= UINTPTR_MAX - base ? reinterpret_cast<const void*>(base + rva) : nullptr;
}

// Validates the complete-object locator's self reference and owning hierarchy.
bool ValidLocator(const ModuleInfo& image, const void* address, uint32_t descriptor, const Locator& locator)
{
    if (locator.signature != 1 || locator.type != descriptor || FromRva(image, locator.self) != address) return false;
    Hierarchy hierarchy{};
    if (!ReadAt(image, FromRva(image, locator.hierarchy), hierarchy) || hierarchy.signature != 0 || !hierarchy.count ||
        hierarchy.count > image.size / sizeof(uint32_t))
        return false;
    const auto* bases = FromRva(image, hierarchy.bases);
    if (!modules::ContainsRange(image, bases, static_cast<size_t>(hierarchy.count) * sizeof(uint32_t))) return false;
    uint32_t firstBase = 0;
    uint32_t firstType = 0;
    return ReadAt(image, bases, firstBase) && ReadAt(image, FromRva(image, firstBase), firstType) && firstType == descriptor;
}
#endif
} // namespace

std::vector<VirtualTable> FindVirtualTables(const ModuleInfo& module, const char* typeName, char* error, size_t maxlen)
{
    std::vector<VirtualTable> candidates;
    if (!module || !typeName || !typeName[0])
    {
        SetError(error, maxlen, "%s", "invalid RTTI request");
        return candidates;
    }
    const size_t nameSize = std::strlen(typeName) + 1;
#ifdef _WIN32
    const ModuleInfo rdata = modules::FindSection(module, ".rdata");
    ModuleInfo metadata = rdata;
    const ModuleInfo data = modules::FindSection(module, ".data");
    if (data)
    {
        metadata.base = module.base;
        metadata.size = module.size;
        metadata.segments.insert(metadata.segments.end(), data.segments.begin(), data.segments.end());
    }
    for (void* match : FindExact(metadata, typeName, nameSize))
    {
        const auto* descriptor = Before(match, sizeof(void*) * 2);
        if (!modules::ContainsRange(metadata, descriptor, sizeof(void*) * 2 + nameSize)) continue;
        const auto descriptorAddress = reinterpret_cast<uintptr_t>(descriptor);
        const auto base = reinterpret_cast<uintptr_t>(module.base);
        if (descriptorAddress < base || descriptorAddress - base > UINT32_MAX) continue;
        const auto rva = static_cast<uint32_t>(descriptorAddress - base);
        for (void* reference : FindExact(rdata, &rva, sizeof(rva)))
        {
            const auto* locatorAddress = Before(reference, offsetof(Locator, type));
            Locator locator{};
            if (!ReadAt(rdata, locatorAddress, locator) || !ValidLocator(module, locatorAddress, rva, locator)) continue;
            const auto locatorPointer = reinterpret_cast<uintptr_t>(locatorAddress);
            for (void* tableHeader : FindExact(rdata, &locatorPointer, sizeof(locatorPointer)))
                AddCandidate(rdata, tableHeader, -static_cast<ptrdiff_t>(locator.offset), locator.construction, candidates);
        }
    }
#else
    for (void* match : FindExact(module, typeName, nameSize))
    {
        const auto nameAddress = reinterpret_cast<uintptr_t>(match);
        for (void* nameReference : FindExact(module, &nameAddress, sizeof(nameAddress)))
        {
            const auto* typeInfo = Before(nameReference, sizeof(void*));
            void** typeVtable = nullptr;
            void* destructor = nullptr;
            if (!ReadAt(module, typeInfo, typeVtable) || !memory::Read(typeVtable, 0, destructor) ||
                !memory::IsExecutableAddress(destructor))
                continue;
            const auto typeAddress = reinterpret_cast<uintptr_t>(typeInfo);
            for (void* tableHeader : FindExact(module, &typeAddress, sizeof(typeAddress)))
            {
                ptrdiff_t offset = 0;
                if (ReadAt(module, Before(tableHeader, sizeof(offset)), offset)) AddCandidate(module, tableHeader, offset, 0, candidates);
            }
        }
    }
#endif
    std::ranges::sort(candidates, [](const VirtualTable& a, const VirtualTable& b) {
        return reinterpret_cast<uintptr_t>(a.address) < reinterpret_cast<uintptr_t>(b.address);
    });
    if (candidates.empty()) SetError(error, maxlen, "RTTI tables unavailable for '%s'", typeName);
    return candidates;
}

void** ResolveVirtualTable(const ModuleInfo& module, const char* className, char* error, size_t maxlen)
{
    if (!className || !className[0])
    {
        SetError(error, maxlen, "%s", "invalid RTTI class name");
        return nullptr;
    }
    for (const char* p = className; *p; ++p)
    {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') continue;
        SetError(error, maxlen, "Use FindVirtualTables with an ABI type name for '%s'", className);
        return nullptr;
    }
    std::vector<VirtualTable> candidates;
#ifdef _WIN32
    for (const char* prefix : { ".?AV", ".?AU" })
    {
        const auto tables = FindVirtualTables(module, (std::string(prefix) + className + "@@").c_str());
        candidates.insert(candidates.end(), tables.begin(), tables.end());
    }
#else
    candidates = FindVirtualTables(module, (std::to_string(std::strlen(className)) + className).c_str());
#endif
    void** result = nullptr;
    for (const auto& candidate : candidates)
    {
        if (candidate.offsetToTop != 0 || candidate.constructionDisplacement != 0) continue;
        if (result && result != candidate.address)
        {
            SetError(error, maxlen, "ambiguous primary RTTI table for '%s'", className);
            return nullptr;
        }
        result = candidate.address;
    }
    if (!result) SetError(error, maxlen, "primary RTTI table unavailable for '%s'", className);
    return result;
}
} // namespace cs2_sdk::rtti
