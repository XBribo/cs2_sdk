# Generates only from the pinned s2sdk definitions, using its host protoc.
if(CMAKE_HOST_WIN32)
    set(_protoc "${S2SDK_DIR}/devtools/bin/protoc.exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_protoc "${S2SDK_DIR}/devtools/bin/linux/protoc")
else()
    message(FATAL_ERROR "Unsupported protoc host: ${CMAKE_HOST_SYSTEM_NAME}")
endif()
execute_process(COMMAND "${_protoc}" --version OUTPUT_VARIABLE _protoc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _protoc_result)
if(NOT _protoc_result EQUAL 0 OR NOT _protoc_version MATCHES "libprotoc 3\\.21\\.")
    message(FATAL_ERROR "Pinned s2sdk protoc must be runnable and version 3.21.x: ${_protoc_version}")
endif()
set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_proto_inputs
    "${S2SDK_DIR}/common/network_connection.proto"
    "${S2SDK_DIR}/common/networkbasetypes.proto"
    "${S2SDK_DIR}/common/source2_steam_stats.proto"
    "${S2SDK_DIR}/common/netmessages.proto"
    "${S2SDK_DIR}/common/valveextensions.proto"
    "${S2SDK_DIR}/game/shared/usercmd.proto"
    "${S2SDK_DIR}/game/shared/cs/cs_usercmd.proto"
)
set(_proto_outputs "")
foreach(input IN LISTS _proto_inputs)
    get_filename_component(name "${input}" NAME_WE)
    list(APPEND _proto_outputs "${_generated_dir}/${name}.pb.cc" "${_generated_dir}/${name}.pb.h")
endforeach()
add_custom_command(OUTPUT ${_proto_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
    COMMAND "${_protoc}" "--cpp_out=${_generated_dir}"
        "--proto_path=${S2SDK_DIR}/common"
        "--proto_path=${S2SDK_DIR}/game/shared/cs"
        "--proto_path=${S2SDK_DIR}/game/shared"
        "--proto_path=${S2SDK_DIR}/thirdparty/protobuf-3.21.8/src"
        ${_proto_inputs}
    COMMAND "${CMAKE_COMMAND}" "-DGENERATED_DIR=${_generated_dir}" -P "${CMAKE_CURRENT_LIST_DIR}/strip_proto_final.cmake"
    DEPENDS "${_protoc}" ${_proto_inputs} "${CMAKE_CURRENT_LIST_DIR}/strip_proto_final.cmake"
    COMMENT "Generating pinned s2sdk protobuf messages"
    VERBATIM
)

target_sources(cs2_sdk PRIVATE ${_proto_outputs})
target_include_directories(cs2_sdk PUBLIC "${_generated_dir}")
