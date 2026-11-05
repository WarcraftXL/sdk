# SPDX-License-Identifier: GPL-3.0-or-later
# wxl_add_extension(): build an extension DLL with its manifest; the test helpers are in WarcraftXLTests.cmake.

include_guard(GLOBAL)

# The settings and ABI tools and the fake host: beside this file once installed, in the source tree otherwise.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/settings/settings.lua")
    set(_WXL_SETTINGS_TOOL "${CMAKE_CURRENT_LIST_DIR}/settings/settings.lua" CACHE INTERNAL "")
    set(_WXL_SDK_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../../include" CACHE INTERNAL "")
    set(_WXL_FAKE_HOST_DIR "${CMAKE_CURRENT_LIST_DIR}/fake-host" CACHE INTERNAL "")
    set(_WXL_ABI_TOOL "${CMAKE_CURRENT_LIST_DIR}/abi/abi_lock.lua" CACHE INTERNAL "")
else()
    set(_WXL_SETTINGS_TOOL "${CMAKE_CURRENT_LIST_DIR}/../tools/settings/settings.lua" CACHE INTERNAL "")
    set(_WXL_SDK_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../include" CACHE INTERNAL "")
    set(_WXL_FAKE_HOST_DIR "${CMAKE_CURRENT_LIST_DIR}/../tools/fake-host" CACHE INTERNAL "")
    set(_WXL_ABI_TOOL "${CMAKE_CURRENT_LIST_DIR}/../tools/abi/abi_lock.lua" CACHE INTERNAL "")
endif()

# SSE2 floats for everything built on the SDK, whatever x87 precision the client sets for Direct3D.
function(wxl_float_options out)
    if(MSVC)
        set(${out} /arch:SSE2 PARENT_SCOPE)
    else()
        set(${out} -msse2 -mfpmath=sse PARENT_SCOPE)
    endif()
endfunction()

# wxl_target_settings(<target> <header>...): the SETTINGS step of wxl_add_extension, for any target.
function(wxl_target_settings target)
    find_program(WXL_LUAJIT NAMES luajit luajit-2.1 DOC "LuaJIT, for the settings tool")
    set(libclang "$ENV{WXL_LIBCLANG}")
    if(NOT libclang)
        set(libclang "/usr/lib/llvm-19/lib/libclang-19.so.1")
    endif()
    set(tool_ok FALSE)
    if(WXL_LUAJIT AND EXISTS "${libclang}")
        set(tool_ok TRUE)
    endif()
    foreach(header IN LISTS ARGN)
        get_filename_component(header "${header}" ABSOLUTE)
        get_filename_component(dir "${header}" DIRECTORY)
        get_filename_component(stem "${header}" NAME_WE)
        set(generated "${dir}/${stem}.settings.hpp")
        if(tool_ok)
            set(stamp "${CMAKE_CURRENT_BINARY_DIR}/wxl-settings/${target}/${stem}.stamp")
            add_custom_command(OUTPUT "${stamp}"
                COMMAND "${WXL_LUAJIT}" "${_WXL_SETTINGS_TOOL}" "${header}" -q --libclang "${libclang}"
                        --sdk-include "${_WXL_SDK_INCLUDE}" -I "${dir}"
                COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
                DEPENDS "${header}" "${_WXL_SETTINGS_TOOL}"
                BYPRODUCTS "${generated}"
                COMMENT "settings: ${stem}"
                VERBATIM)
            target_sources(${target} PRIVATE "${stamp}")
        elseif(NOT EXISTS "${generated}")
            message(FATAL_ERROR "wxl_target_settings(${target}): ${generated} is missing and the settings tool cannot "
                                "run (it needs LuaJIT and libclang 19, or WXL_LIBCLANG). Generate it where they are "
                                "and commit it.")
        endif()
    endforeach()
endfunction()

# Reads WXL_SDK_VERSION_MAJOR and _MINOR out of wxl/Version.h.
function(wxl_read_sdk_version header out_major out_minor)
    file(STRINGS "${header}" lines REGEX "^#define WXL_SDK_VERSION_(MAJOR|MINOR) +[0-9]+")
    foreach(line IN LISTS lines)
        if(line MATCHES "MAJOR +([0-9]+)")
            set(major "${CMAKE_MATCH_1}")
        elseif(line MATCHES "MINOR +([0-9]+)")
            set(minor "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(major STREQUAL "" OR minor STREQUAL "")
        message(FATAL_ERROR "WarcraftXL: no SDK version in ${header}")
    endif()
    set(${out_major} "${major}" PARENT_SCOPE)
    set(${out_minor} "${minor}" PARENT_SCOPE)
endfunction()

# wxl_add_extension(<name> SOURCES <file>... [MANIFEST <wxl.json>] [VERSION <x.y.z>] [LINK <target>...]
#                   [SETTINGS <header>...] [OUTPUT_DIRECTORY <dir>]): <dir>/<name>.dll and <dir>/wxl.json,
# default <dir> <build>/Extensions/<name>; installed to Extensions/<name>/, copied to CLIENT_PATH when set.
function(wxl_add_extension name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MANIFEST;VERSION;OUTPUT_DIRECTORY" "SOURCES;LINK;SETTINGS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "wxl_add_extension(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "wxl_add_extension(${name}): SOURCES is required")
    endif()
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "wxl_add_extension(${name}): extensions are Win32 (x86) DLLs, the client is a "
                            "32-bit process. Configure with -A Win32 or an x86 Windows toolchain.")
    endif()
    if(NOT TARGET WarcraftXL::sdk)
        message(FATAL_ERROR "wxl_add_extension(${name}): WarcraftXL::sdk is missing; call "
                            "find_package(WarcraftXL 1.3 CONFIG REQUIRED) first")
    endif()

    get_property(sdk_version GLOBAL PROPERTY WXL_SDK_VERSION)
    set(out "${arg_OUTPUT_DIRECTORY}")
    if(NOT out)
        set(out "${CMAKE_BINARY_DIR}/Extensions/${name}")
    endif()

    add_library(${name} SHARED ${arg_SOURCES})
    # $<1:...> keeps multi-config generators from appending a per-config folder.
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${name}"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${out}>"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # WXL_EXTENSION makes PluginApi.h declare WXL_Query and WXL_Load as exports.
    target_compile_definitions(${name} PRIVATE WXL_EXTENSION WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${name} PRIVATE WarcraftXL::sdk ${arg_LINK})
    if(arg_SETTINGS)
        wxl_target_settings(${name} ${arg_SETTINGS})
    endif()
    if(MINGW)
        target_compile_options(${name} PRIVATE -fms-extensions)
        target_link_options(${name} PRIVATE -static)
    endif()

    # The manifest the core reads before loading the DLL: extension.abi is the SDK it builds against.
    if(arg_MANIFEST)
        get_filename_component(manifest "${arg_MANIFEST}" ABSOLUTE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
        file(READ "${manifest}" json)
        string(JSON kind ERROR_VARIABLE err TYPE "${json}" extension)
        if(err OR NOT kind STREQUAL "OBJECT")
            message(FATAL_ERROR "wxl_add_extension(${name}): ${manifest} has no \"extension\" object")
        endif()
        string(JSON id ERROR_VARIABLE err GET "${json}" extension id)
        if(NOT err AND NOT id STREQUAL name)
            message(WARNING "wxl_add_extension(${name}): ${manifest} names extension.id \"${id}\"; "
                            "the core loads Extensions/${name}/${name}.dll")
        endif()
        string(JSON entry ERROR_VARIABLE err GET "${json}" extension entry)
        if(NOT err AND NOT entry STREQUAL "${name}.dll")
            message(WARNING "wxl_add_extension(${name}): extension.entry \"${entry}\" is not ${name}.dll")
        endif()
    else()
        set(version "${arg_VERSION}")
        if(NOT version)
            set(version "0.1.0")
        endif()
        set(json "{ \"manifest\": 1, \"extension\": { \"id\": \"${name}\", \"version\": \"${version}\", \"entry\": \"${name}.dll\" } }")
    endif()
    string(JSON json SET "${json}" extension abi "\"${sdk_version}\"")
    file(GENERATE OUTPUT "${out}/wxl.json" CONTENT "${json}\n")
    set_property(TARGET ${name} PROPERTY WXL_MANIFEST "${out}/wxl.json")

    install(FILES "$<TARGET_FILE:${name}>" "${out}/wxl.json" DESTINATION "Extensions/${name}")

    if(CLIENT_PATH)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/Extensions/${name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${name}>" "${out}/wxl.json"
                    "${CLIENT_PATH}/Extensions/${name}/"
            COMMENT "Deploy ${name} -> ${CLIENT_PATH}/Extensions/${name}")
    endif()
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/WarcraftXLTests.cmake")
