#pragma once

#include <cstddef>
#include <span>

class ISchemaSystem;

namespace cs2_sdk::schema {
struct Field
{
    const char* className;
    const char* fieldName;
};

struct FieldBinding
{
    int* target;
    const char* className;
    const char* fieldName;
};

// Borrows the engine interface; the server type scope must be ready.
bool Init(ISchemaSystem* system, char* error = nullptr, size_t maxlen = 0);
// Includes base-class offsets; returns -1 if missing or ambiguous. Caches successes.
int GetFieldOffset(const char* className, const char* fieldName);
// Resolves every binding, writes -1 for missing fields, and reports the first failure.
bool ResolveFields(std::span<const FieldBinding> fields, char* error = nullptr, size_t maxlen = 0);
// Sums inline embedded-member offsets; pointer/handle indirections must be read separately.
int GetEmbeddedFieldOffset(std::span<const Field> fields, char* error = nullptr, size_t maxlen = 0);
// Clears borrowed interfaces and cached offsets on plugin unload.
void Reset();
} // namespace cs2_sdk::schema
