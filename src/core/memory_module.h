#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cs2_sdk::modules {
#ifdef _WIN32
inline constexpr const char* kRootBin = "/bin/win64/";
inline constexpr const char* kGameBin = "/csgo/bin/win64/";
inline constexpr const char* kModulePrefix = "";
inline constexpr const char* kModuleExtension = ".dll";
inline constexpr const char* kServerModuleName = "server.dll";
inline constexpr const char* kEngineModuleName = "engine2.dll";
inline constexpr const char* kSchemaSystemModuleName = "schemasystem.dll";
#else
inline constexpr const char* kRootBin = "/bin/linuxsteamrt64/";
inline constexpr const char* kGameBin = "/csgo/bin/linuxsteamrt64/";
inline constexpr const char* kModulePrefix = "lib";
inline constexpr const char* kModuleExtension = ".so";
inline constexpr const char* kServerModuleName = "libserver.so";
inline constexpr const char* kEngineModuleName = "libengine2.so";
inline constexpr const char* kSchemaSystemModuleName = "libschemasystem.so";
#endif
inline constexpr const char* kSchemaServerTypeScope = kServerModuleName;

struct ModuleSegment
{
    unsigned char* base = nullptr;
    size_t size = 0;
};
struct ModuleInfo
{
    unsigned char* base = nullptr;
    size_t size = 0;
    std::vector<ModuleSegment> segments;
    // Checks base and size only.
    explicit operator bool() const { return base != nullptr && size != 0; }
};

class CModule
{
  public:
    // Adds the platform's module prefix and extension to moduleName.
    CModule(const char* relativeDirectory, const char* moduleName);

    // Releases the owned loader reference.
    ~CModule() = default;
    CModule(const CModule&) = delete;
    CModule& operator=(const CModule&) = delete;

    // Reports a loaded module image.
    explicit operator bool() const { return static_cast<bool>(m_image); }

    // Returns the full image, including data segments.
    const ModuleInfo& Image() const { return m_image; }

    // Returns executable segments for instruction signatures.
    const ModuleInfo& Code() const { return m_code; }

    // Returns the requested module path.
    const char* Path() const { return m_path.c_str(); }

  private:
    struct HandleDeleter
    {
        // Releases the owned loader reference.
        void operator()(void* handle) const;
    };
    std::string m_path;
    std::unique_ptr<void, HandleDeleter> m_hModule;
    ModuleInfo m_image;
    ModuleInfo m_code;
};

// Borrowed; valid until Shutdown or a failed-load retry.
extern CModule* engine;
extern CModule* server;

// Checks complete range coverage within the image segments.
bool ContainsRange(const ModuleInfo& module, const void* address, size_t size);
#ifdef _WIN32
// Selects a section while preserving the image base for PE RVAs.
ModuleInfo FindSection(const ModuleInfo& image, const char* name);
// Selects sections marked both readable and executable.
ModuleInfo ExecutableSections(const ModuleInfo& image);
#endif

// Keeps successful loads and retries failed ones.
void Initialize();
// Call after removing hooks and releasing borrowed module views.
void Shutdown();

// Accepts hexadecimal bytes and ? or ?? wildcards; rejects an empty signature.
bool ParseSigString(const std::string& signature, std::vector<uint8_t>& bytes, std::vector<bool>& wildcards);
// Returns nullptr for missing or ambiguous matches.
void* FindPatternIn(const ModuleInfo& module, const std::vector<uint8_t>& pattern, const std::vector<bool>& wildcards);
// Scans original bytes through KHook, including instructions replaced by hooks.
// Stops at maxMatches; zero collects all matches.
std::vector<void*> FindPatternMatchesIn(const ModuleInfo& module,
                                        const std::vector<uint8_t>& pattern,
                                        const std::vector<bool>& wildcards,
                                        size_t maxMatches = 2);
} // namespace cs2_sdk::modules
