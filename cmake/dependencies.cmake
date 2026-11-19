set(S2SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/s2sdk")
set(METAMOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/metamod-source")
foreach(header "${S2SDK_DIR}/public/tier0/platform.h"
               "${METAMOD_DIR}/third_party/khook/include/khook.hpp"
               "${CMAKE_CURRENT_SOURCE_DIR}/libraries/json/include/nlohmann/json.hpp")
    if(NOT EXISTS "${header}")
        message(FATAL_ERROR "Missing SDK dependency: ${header}. Run git submodule update --init --recursive")
    endif()
endforeach()

if(TARGET spdlog::spdlog)
    # Do not change targets owned by the parent project.
    get_target_property(_type spdlog::spdlog TYPE)
    if(NOT _type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "cs2_sdk requires a static spdlog::spdlog for plugin isolation")
    endif()
    if(MSVC)
        get_target_property(_crt spdlog::spdlog MSVC_RUNTIME_LIBRARY)
        get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(_multi_config)
            string(TOUPPER "${CMAKE_CONFIGURATION_TYPES}" _configs)
        else()
            string(TOUPPER "${CMAKE_BUILD_TYPE}" _configs)
        endif()
        if(NOT _crt STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>"
           AND NOT (_crt STREQUAL "MultiThreadedDebug" AND _configs STREQUAL "DEBUG")
           AND NOT (_crt STREQUAL "MultiThreaded" AND NOT "DEBUG" IN_LIST _configs))
            message(FATAL_ERROR "Configure the parent spdlog with MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug> (/MT, /MTd)")
        endif()
    else()
        get_target_property(_pic spdlog::spdlog POSITION_INDEPENDENT_CODE)
        get_target_property(_defs spdlog::spdlog INTERFACE_COMPILE_DEFINITIONS)
        if(NOT _pic OR NOT "_GLIBCXX_USE_CXX11_ABI=0" IN_LIST _defs)
            message(FATAL_ERROR "Configure the parent spdlog with PIC and PUBLIC _GLIBCXX_USE_CXX11_ABI=0 for cs2_sdk")
        endif()
    endif()
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/spdlog/CMakeLists.txt")
        message(FATAL_ERROR "Missing bundled spdlog. Run git submodule update --init --recursive or provide spdlog::spdlog")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(SPDLOG_BUILD_SHARED OFF)
    set(SPDLOG_BUILD_TESTS OFF)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libraries/spdlog" "${CMAKE_CURRENT_BINARY_DIR}/spdlog")
    set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(spdlog PUBLIC _GLIBCXX_USE_CXX11_ABI=0)
    endif()
endif()

add_library(cs2_sdk_dependencies INTERFACE)
target_compile_definitions(cs2_sdk_dependencies INTERFACE META_IS_SOURCE2 PLATFORM_64BITS X64BITS GOOGLE_PROTOBUF_NO_RTTI)
target_include_directories(cs2_sdk_dependencies INTERFACE
    "${S2SDK_DIR}/thirdparty/protobuf-3.21.8/src"
    "${METAMOD_DIR}/core"
    "${METAMOD_DIR}/core/sourcemm"
    "${METAMOD_DIR}/third_party/khook/include"
    "${S2SDK_DIR}/public"
    "${S2SDK_DIR}/public/mathlib"
    "${S2SDK_DIR}/public/tier0"
    "${S2SDK_DIR}/public/entity2"
    "${S2SDK_DIR}/game/shared"
    "${S2SDK_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/libraries/json/include"
)
if(WIN32)
    target_compile_definitions(cs2_sdk_dependencies INTERFACE _CRT_SECURE_NO_WARNINGS WIN32 _WIN32 _WINDOWS
        COMPILER_MSVC COMPILER_MSVC64 NOMINMAX _ALLOW_KEYWORD_MACROS)
    target_include_directories(cs2_sdk_dependencies INTERFACE "${S2SDK_DIR}/public/tier1")
    target_link_libraries(cs2_sdk_dependencies INTERFACE psapi
        "${S2SDK_DIR}/lib/public/win64/tier0.lib"
        "${S2SDK_DIR}/lib/public/win64/mathlib.lib"
        "${S2SDK_DIR}/lib/public/win64/2015/libprotobuf.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(cs2_sdk_dependencies INTERFACE _GNU_SOURCE POSIX _LINUX LINUX COMPILER_GCC
        _GLIBCXX_USE_CXX11_ABI=0 stricmp=strcasecmp _stricmp=strcasecmp _strnicmp=strncasecmp
        strnicmp=strncasecmp _snprintf=snprintf _vsnprintf=vsnprintf _alloca=alloca strcmpi=strcasecmp)
    target_compile_options(cs2_sdk_dependencies INTERFACE "SHELL:-idirafter ${S2SDK_DIR}/public/tier1")
    target_link_libraries(cs2_sdk_dependencies INTERFACE
        "${S2SDK_DIR}/lib/linux64/libtier0.so"
        "${S2SDK_DIR}/lib/linux64/mathlib.a"
        "${S2SDK_DIR}/lib/linux64/release/libprotobuf.a" pthread dl)
endif()
