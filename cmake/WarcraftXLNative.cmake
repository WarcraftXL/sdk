# SPDX-License-Identifier: GPL-3.0-or-later
# Native client functions: baselines held under their native copies, and stock tests against Wow.exe.

include_guard(GLOBAL)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/baseline/baseline.lua")
    set(_WXL_BASELINE_TOOL "${CMAKE_CURRENT_LIST_DIR}/baseline/baseline.lua" CACHE INTERNAL "")
    set(_WXL_STOCK_DIR "${CMAKE_CURRENT_LIST_DIR}/stock" CACHE INTERNAL "")
else()
    set(_WXL_BASELINE_TOOL "${CMAKE_CURRENT_LIST_DIR}/../tools/baseline/baseline.lua" CACHE INTERNAL "")
    set(_WXL_STOCK_DIR "${CMAKE_CURRENT_LIST_DIR}/../tools/stock" CACHE INTERNAL "")
endif()

set(WXL_WOW_EXE "$ENV{WXL_WOW_EXE}" CACHE FILEPATH "A stock Wow.exe 12340 (read only)")

# wxl_add_baseline_check(<name> SOURCE <src dir>): a ctest `<name>-baseline` failing when a native file no
# longer names the bytes of the generated baseline it started from (src/gen/).
function(wxl_add_baseline_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "")
    find_program(WXL_LUAJIT NAMES luajit luajit-2.1 DOC "LuaJIT, for the settings, ABI and baseline tools")
    if(NOT WXL_LUAJIT)
        message(WARNING "wxl_add_baseline_check(${name}): no LuaJIT, the check is not added")
        return()
    endif()
    file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${arg_SOURCE}/*.cpp" "${arg_SOURCE}/*.hpp")
    list(SORT _files)
    add_test(NAME ${name}-baseline COMMAND "${WXL_LUAJIT}" "${_WXL_BASELINE_TOOL}" "${arg_SOURCE}" ${_files})
endfunction()

# wxl_add_stock_test(<name> SOURCES <src>... [LINK <target>...]): a test executable that lays the stock
# Wow.exe at 0x00400000 (tools/stock/StockImage.hpp) to run its functions beside native ones; skipped (77)
# without a Wow.exe.
function(wxl_add_stock_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LINK")
    add_executable(${name} ${arg_SOURCES} "${_WXL_STOCK_DIR}/StockImage.cpp")
    target_include_directories(${name} PRIVATE "${_WXL_STOCK_DIR}")
    target_link_libraries(${name} PRIVATE WarcraftXL::offsets ${arg_LINK})
    target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Linked low and fixed: its .bss room then covers Wow.exe's own addresses.
    target_link_options(${name} PRIVATE /BASE:0x10000 /DYNAMICBASE:NO /FIXED /ALIGN:65536)
    add_test(NAME ${name} COMMAND ${name} "${WXL_WOW_EXE}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()
