#include "schema.h"
#include "memory_module.h"

#include <schemasystem/schemasystem.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

namespace cs2_sdk::schema {
namespace {
ISchemaSystem* g_system = nullptr;
CSchemaSystemTypeScope* g_scope = nullptr;
using FieldMap = std::unordered_map<std::string, int>;
std::unordered_map<std::string, FieldMap> g_cache;
// Keep invalid branches distinct from missing fields.
constexpr int kMissing = -1;
constexpr int kInvalid = -2;

// Each base offset is relative to its immediate derived class.
int FindField(const CSchemaClassInfo* info, const char* name, unsigned depth)
{
    if (!info || info->m_nSize <= 0 || depth > 32) return kInvalid;
    if (info->m_nFieldCount && !info->m_pFields) return kInvalid;
    for (uint16_t i = 0; i < info->m_nFieldCount; ++i)
    {
        const auto& field = info->m_pFields[i];
        if (!field.m_pszName || std::strcmp(field.m_pszName, name) != 0) continue;
        return field.m_nSingleInheritanceOffset >= 0 && field.m_nSingleInheritanceOffset < info->m_nSize ? field.m_nSingleInheritanceOffset
                                                                                                         : kInvalid;
    }
    if (info->m_nBaseClassCount && !info->m_pBaseClasses) return kInvalid;
    int result = kMissing;
    for (uint8_t i = 0; i < info->m_nBaseClassCount; ++i)
    {
        const auto& base = info->m_pBaseClasses[i];
        const int offset = FindField(base.m_pClass, name, depth + 1);
        if (offset == kInvalid) return kInvalid;
        if (offset == kMissing) continue;
        if (base.m_nOffset > static_cast<unsigned>(std::numeric_limits<int>::max() - offset)) return kInvalid;
        const int total = static_cast<int>(base.m_nOffset) + offset;
        if (total >= info->m_nSize || result >= 0) return kInvalid;
        result = total;
    }
    return result;
}

// The caller may omit the diagnostic buffer.
bool Fail(char* error, size_t maxlen, const char* message)
{
    if (error && maxlen) std::snprintf(error, maxlen, "%s", message);
    return false;
}
} // namespace

bool Init(ISchemaSystem* system, char* error, size_t maxlen)
{
    if (system && system == g_system && g_scope) return true;
    Reset();
    if (!system) return Fail(error, maxlen, "SchemaSystem interface is unavailable");
    if (!system->SchemaSystemIsReady()) return Fail(error, maxlen, "SchemaSystem is not ready");
    g_scope = system->FindTypeScopeForModule(modules::kSchemaServerTypeScope, nullptr);
    if (!g_scope) return Fail(error, maxlen, "server Schema type scope is unavailable");
    g_system = system;
    return true;
}

int GetFieldOffset(const char* className, const char* fieldName)
{
    if (!g_scope || !className || !fieldName) return -1;
    const auto table = g_cache.find(className);
    if (table != g_cache.end())
    {
        const auto field = table->second.find(fieldName);
        if (field != table->second.end()) return field->second;
    }
    auto* info = g_scope->FindDeclaredClass(className).Get();
    if (!info)
    {
        auto* global = g_system->GlobalTypeScope();
        if (global) info = global->FindDeclaredClass(className).Get();
    }
    const int offset = FindField(info, fieldName, 0);
    if (offset >= 0) g_cache[className].emplace(fieldName, offset);
    return offset >= 0 ? offset : -1;
}

bool ResolveFields(std::span<const FieldBinding> fields, char* error, size_t maxlen)
{
    bool success = true;
    for (const FieldBinding& field : fields)
    {
        *field.target = GetFieldOffset(field.className, field.fieldName);
        if (*field.target >= 0) continue;
        if (success && error && maxlen)
            std::snprintf(error, maxlen, "Required Schema field missing: %s::%s", field.className, field.fieldName);
        success = false;
    }
    return success;
}

int GetEmbeddedFieldOffset(std::span<const Field> fields, char* error, size_t maxlen)
{
    if (fields.empty())
    {
        Fail(error, maxlen, "Embedded Schema field chain is empty");
        return -1;
    }
    int total = 0;
    for (const Field& field : fields)
    {
        const int offset = GetFieldOffset(field.className, field.fieldName);
        if (offset < 0)
        {
            if (error && maxlen) std::snprintf(error, maxlen, "Required Schema field missing: %s::%s", field.className, field.fieldName);
            return -1;
        }
        if (offset > std::numeric_limits<int>::max() - total)
        {
            Fail(error, maxlen, "Embedded Schema field offset overflow");
            return -1;
        }
        total += offset;
    }
    return total;
}

void Reset()
{
    g_cache.clear();
    g_scope = nullptr;
    g_system = nullptr;
}
} // namespace cs2_sdk::schema
