include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "cs2_sdk requires an x64 build")
endif()

# Applies the same ABI and visibility rules to SDK and plugin translation units.
function(cs2_sdk_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3 /permissive- /wd4267 /wd4244 /wd4146 /wd4996 /wd4661)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(${target} PRIVATE -fno-rtti -Wno-non-virtual-dtor -Wno-overloaded-virtual
            -Wno-register -Wno-narrowing -Wno-deprecated-copy)
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
    endif()
endfunction()
