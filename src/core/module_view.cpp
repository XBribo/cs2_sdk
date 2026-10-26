#include "memory_module.h"
#include "../utils/memory.h"

#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cs2_sdk::modules {
bool ContainsRange(const ModuleInfo& module, const void* address, size_t size)
{
    if (!module || !address || !size) return false;
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    const auto imageBegin = reinterpret_cast<uintptr_t>(module.base);
    if (module.size > UINTPTR_MAX - imageBegin || cursor < imageBegin || cursor >= imageBegin + module.size ||
        size > imageBegin + module.size - cursor)
        return false;
    const uintptr_t end = cursor + size;
    while (cursor < end)
    {
        uintptr_t covered = cursor;
        for (const auto& segment : module.segments)
        {
            const auto begin = reinterpret_cast<uintptr_t>(segment.base);
            if (!begin || !segment.size || segment.size > UINTPTR_MAX - begin) continue;
            const uintptr_t limit = begin + segment.size;
            if (cursor >= begin && cursor < limit && limit > covered) covered = limit;
        }
        if (covered == cursor) return false;
        cursor = covered;
    }
    return true;
}

#ifdef _WIN32
namespace {
// Reads image bytes within the selected segments.
bool ReadImage(const ModuleInfo& image, size_t offset, void* output, size_t size)
{
    if (offset > image.size || size > image.size - offset) return false;
    const auto begin = reinterpret_cast<uintptr_t>(image.base);
    if (offset > UINTPTR_MAX - begin) return false;
    const auto* address = reinterpret_cast<const void*>(begin + offset);
    return ContainsRange(image, address, size) && memory::ReadBytes(address, output, size);
}

// A null name selects every section with the required flags.
ModuleInfo SelectSections(const ModuleInfo& image, const char* name, DWORD requiredFlags)
{
    ModuleInfo out;
    if (!image || (name && (!name[0] || std::strlen(name) > IMAGE_SIZEOF_SHORT_NAME))) return out;
    IMAGE_DOS_HEADER dos{};
    if (!ReadImage(image, 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) return out;
    const auto ntOffset = static_cast<size_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS64 nt{};
    if (!ReadImage(image, ntOffset, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
        return out;
    const size_t headerSize = offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    if (headerSize > image.size || ntOffset > image.size - headerSize) return out;
    const size_t sectionTable = ntOffset + headerSize;
    if (nt.FileHeader.NumberOfSections > (image.size - sectionTable) / sizeof(IMAGE_SECTION_HEADER)) return out;
    for (size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
    {
        IMAGE_SECTION_HEADER section{};
        if (!ReadImage(image, sectionTable + i * sizeof(section), &section, sizeof(section))) return {};
        char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        std::memcpy(sectionName, section.Name, IMAGE_SIZEOF_SHORT_NAME);
        if ((name && std::strcmp(name, sectionName) != 0) || (section.Characteristics & requiredFlags) != requiredFlags) continue;
        const size_t offset = section.VirtualAddress;
        const size_t size = section.Misc.VirtualSize;
        if (!size || offset >= image.size || size > image.size - offset) return {};
        const auto begin = reinterpret_cast<uintptr_t>(image.base);
        if (offset > UINTPTR_MAX - begin) return {};
        auto* address = reinterpret_cast<unsigned char*>(begin + offset);
        if (!ContainsRange(image, address, size)) return {};
        out.base = image.base;
        out.size = image.size;
        out.segments.push_back({ .base = address, .size = size });
        if (name) break;
    }
    return out;
}
} // namespace

ModuleInfo FindSection(const ModuleInfo& image, const char* name) { return name ? SelectSections(image, name, 0) : ModuleInfo{}; }

ModuleInfo ExecutableSections(const ModuleInfo& image)
{
    return SelectSections(image, nullptr, IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
}
#endif
} // namespace cs2_sdk::modules
