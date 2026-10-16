#pragma once

#include "../core/memory_module.h"

namespace cs2_sdk::rtti {
struct VirtualTable
{
    void** address = nullptr;
    ptrdiff_t offsetToTop = 0;
    // MSVC vtordisp displacement; zero on Itanium. Not an automatic this adjustment.
    uint32_t constructionDisplacement = 0;
};

// Enumerates deduplicated callable table candidates by exact ABI type name:
// MSVC .?AVClass@@ / .?AUStruct@@, or Itanium type_info::name() encoding.
// May include construction tables; metadata alone does not certify object lifetime.
// Null/non-executable first slots are omitted. Addresses are borrowed from the loaded module.
std::vector<VirtualTable>
FindVirtualTables(const modules::ModuleInfo& module, const char* typeName, char* error = nullptr, size_t maxlen = 0);
// Resolves exactly one zero-offset candidate for an unqualified class/struct name.
// Qualified/template names must use FindVirtualTables with their ABI encoding.
void** ResolveVirtualTable(const modules::ModuleInfo& module, const char* className, char* error = nullptr, size_t maxlen = 0);
} // namespace cs2_sdk::rtti
