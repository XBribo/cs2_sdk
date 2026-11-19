#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cs2_sdk::memory {
// Reads process memory; failure may partially fill output.
bool ReadBytes(const void* address, void* output, size_t size);
// Writes with existing page permissions; failure may partially update memory.
bool WriteBytes(void* address, const void* value, size_t size);
// Checks current readability; object lifetime is caller-managed.
bool IsReadable(const void* address, size_t size);
// Checks current execution permissions.
bool IsExecutableAddress(const void* address);
// Reads a signed rel32 and resolves it relative to the instruction end.
void* ResolveRelativeAddress(const void* instruction, size_t displacementOffset, size_t instructionLength);

// Reads a complete value, preserving output on failure.
template <typename T> bool Read(const void* base, size_t offset, T& output)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto address = reinterpret_cast<uintptr_t>(base);
    if (!base || offset > UINTPTR_MAX - address) return false;
    alignas(T) unsigned char buffer[sizeof(T)];
    if (!ReadBytes(reinterpret_cast<const void*>(address + offset), buffer, sizeof(buffer))) return false;
    std::memcpy(&output, buffer, sizeof(T));
    return true;
}
} // namespace cs2_sdk::memory
