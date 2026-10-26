cmake_minimum_required(VERSION 3.20)
project(cs2_sdk LANGUAGES CXX)

include(cmake/platform.cmake)
include(cmake/dependencies.cmake)

add_library(cs2_sdk OBJECT
    src/core/schema.cpp
    src/core/memory_module.cpp
    src/core/module_view.cpp
    src/core/gameconfig.cpp
    src/core/log.cpp
    src/utils/memory.cpp
    src/utils/rtti.cpp
    src/utils/platform.cpp
    # Compile CreateInterface directly to preserve its export with --exclude-libs.
    libraries/s2sdk/interfaces/interfaces.cpp
    libraries/s2sdk/public/tier0/memoverride.cpp
    libraries/s2sdk/tier0_perproject/keyvalues3.cpp
    libraries/s2sdk/tier0_perproject/convar.cpp
)
add_library(cs2_sdk::sdk ALIAS cs2_sdk)
cs2_sdk_configure_target(cs2_sdk)
target_link_libraries(cs2_sdk PUBLIC cs2_sdk_dependencies spdlog::spdlog)
target_include_directories(cs2_sdk PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/metamod"
)

include(cmake/protobuf.cmake)

function(cs2_sdk_configure_plugin target)
    cs2_sdk_configure_target(${target})
    if(MSVC)
        target_link_options(${target} PRIVATE /NODEFAULTLIB:LIBCMTD)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Isolate static protobuf and logging symbols between plugins.
        target_link_options(${target} PRIVATE "LINKER:-z,defs" "LINKER:--exclude-libs,ALL")
        set_target_properties(${target} PROPERTIES SKIP_BUILD_RPATH TRUE BUILD_RPATH "" INSTALL_RPATH "")
    endif()
endfunction()
