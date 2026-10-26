#pragma once

#include "../core/memory_module.h"

namespace cs2_sdk::rtti {
struct VirtualTable
{
    void** address = nullptr;
    ptrdiff_t offsetToTop = 0;
    // MSVC vtordisp displacement, separate from this adjustment; zero on Itanium.
    uint32_t constructionDisplacement = 0;
};

// Enumerates unique callable vtable candidates by exact ABI type name:
// MSVC .?AVClass@@ / .?AUStruct@@, or Itanium type_info::name() encoding.
// Borrows module addresses; may include construction tables. Validate object lifetime separately.
std::vector<VirtualTable>
FindVirtualTables(const modules::ModuleInfo& module, const char* typeName, char* error = nullptr, size_t maxlen = 0);
// Resolves exactly one zero-offset candidate for an unqualified class/struct name.
// Qualified/template names must use FindVirtualTables with their ABI encoding.
void** ResolveVirtualTable(const modules::ModuleInfo& module, const char* className, char* error = nullptr, size_t maxlen = 0);
} // namespace cs2_sdk::rtti
