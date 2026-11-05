# SPDX-License-Identifier: GPL-3.0-or-later
# Test helpers: wxl_add_load_test (the fake host), wxl_add_unit_test (a test program), wxl_add_abi_lock.

include_guard(GLOBAL)

# wxl_add_load_test(<name> [ARGS --world <mapId> --expect-log "<line>"...]): a ctest loading the
# extension in the SDK's fake host.
function(wxl_add_load_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "ARGS")
    if(NOT TARGET ${name})
        message(FATAL_ERROR "wxl_add_load_test(${name}): no such extension; call wxl_add_extension first")
    endif()
    get_property(manifest TARGET ${name} PROPERTY WXL_MANIFEST)
    if(NOT manifest)
        message(FATAL_ERROR "wxl_add_load_test(${name}): ${name} was not made by wxl_add_extension")
    endif()
    if(NOT TARGET wxl-fake-host)
        add_executable(wxl-fake-host EXCLUDE_FROM_ALL "${_WXL_FAKE_HOST_DIR}/FakeHost.cpp")
        target_include_directories(wxl-fake-host PRIVATE "${_WXL_FAKE_HOST_DIR}")
        target_link_libraries(wxl-fake-host PRIVATE WarcraftXL::sdk)
        target_compile_definitions(wxl-fake-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
        set_target_properties(wxl-fake-host PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    # Built with the extension, so `cmake --build` then `ctest` is enough.
    add_dependencies(${name} wxl-fake-host)
    add_test(NAME ${name}-load COMMAND wxl-fake-host "$<TARGET_FILE:${name}>" "${manifest}" ${arg_ARGS})
endfunction()

# wxl_add_unit_test(<name> SOURCES <file>... [LINK <target>...] [INCLUDE <dir>...] [ARGS <arg>...]): a test
# program <name>, built like an extension (C++20, the same CRT and definitions), run by ctest as <name>.
function(wxl_add_unit_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINK;INCLUDE;ARGS")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_SOURCES)
        message(FATAL_ERROR "wxl_add_unit_test(${name}): SOURCES <file>... [LINK ...] [INCLUDE ...] [ARGS ...]")
    endif()
    add_executable(${name} ${arg_SOURCES})
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_include_directories(${name} PRIVATE ${arg_INCLUDE})
    target_link_libraries(${name} PRIVATE WarcraftXL::sdk ${arg_LINK})
    if(MINGW)
        target_compile_options(${name} PRIVATE -fms-extensions)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${arg_ARGS})
endfunction()

# wxl_add_abi_lock(<name> LOCK <file> INCLUDE <dir>... HEADERS <path>... [EVENTS <def>] [MAJOR ..] [MINOR ..]): a ctest
# failing when the headers' C ABI is not the lock's; `<name>-update` rewrites it once the version moved, `<name>-rebaseline`
# rewrites it as it is (a line not yet released).
function(wxl_add_abi_lock name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LOCK;EVENTS" "INCLUDE;HEADERS;MAJOR;MINOR")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_LOCK OR NOT arg_INCLUDE OR NOT arg_HEADERS)
        message(FATAL_ERROR "wxl_add_abi_lock(${name}): LOCK <file> INCLUDE <dir>... HEADERS <path>... are required")
    endif()
    find_program(WXL_LUAJIT NAMES luajit luajit-2.1 DOC "LuaJIT, for the settings and ABI tools")
    if(NOT WXL_LUAJIT OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "wxl_add_abi_lock(${name}): needs LuaJIT and clang; the ABI check is not added")
        return()
    endif()
    get_filename_component(lock "${arg_LOCK}" ABSOLUTE)
    set(work "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    file(MAKE_DIRECTORY "${work}")
    separate_arguments(flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
    if(CMAKE_CXX_COMPILER_TARGET)
        list(PREPEND flags "--target=${CMAKE_CXX_COMPILER_TARGET}")
    endif()
    list(APPEND flags $<IF:$<BOOL:${MSVC}>,/std:c++20,-std=c++20>)
    set(command "${WXL_LUAJIT}" "${_WXL_ABI_TOOL}" --compiler "${CMAKE_CXX_COMPILER}" --lock "${lock}" --work "${work}"
                --regenerate "cmake --build <tree> --target ${name}-update")
    foreach(flag IN LISTS flags)
        list(APPEND command --flag "${flag}")
    endforeach()
    foreach(dir IN LISTS arg_INCLUDE)
        get_filename_component(dir "${dir}" ABSOLUTE)
        list(APPEND command --include "${dir}")
    endforeach()
    foreach(header IN LISTS arg_HEADERS)
        list(APPEND command --header "${header}")
    endforeach()
    if(arg_EVENTS)
        get_filename_component(events "${arg_EVENTS}" ABSOLUTE)
        list(APPEND command --events "${events}")
    endif()
    foreach(macro IN LISTS arg_MAJOR)
        list(APPEND command --major "${macro}")
    endforeach()
    foreach(macro IN LISTS arg_MINOR)
        list(APPEND command --minor "${macro}")
    endforeach()
    add_test(NAME ${name} COMMAND ${command} --check)
    add_custom_target(${name}-update COMMAND ${command} --write VERBATIM)
    add_custom_target(${name}-rebaseline COMMAND ${command} --rebaseline VERBATIM)
endfunction()
