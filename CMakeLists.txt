cmake_minimum_required(VERSION 3.21)

project(wxl-sdk
    VERSION 0.1.0
    DESCRIPTION "The WarcraftXL mod contract"
    LANGUAGES CXX)

# Header-only: nothing here is compiled into the runtime or into a mod as a library. A mod links
# the interface target and gets the include path, the language level and the mod-build define.
add_library(wxl-sdk INTERFACE)
add_library(wxl::sdk ALIAS wxl-sdk)

target_include_directories(wxl-sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# ankerl::unordered_dense (MIT, deps/unordered_dense, its own license file): the hash tables the
# runtimes and wxl::ObjectMap look objects up in. One header, reached as <ankerl/unordered_dense.h>.
target_include_directories(wxl-sdk SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/unordered_dense/include>)

target_compile_features(wxl-sdk INTERFACE cxx_std_17)

# The engine headers (engine/, under their own NOTICE): the client's structures for a mod that
# reads or replaces engine code, laid out for x86 and x64. A mod links it besides wxl::sdk.
add_library(wxl-engine INTERFACE)
add_library(wxl::engine ALIAS wxl-engine)
target_include_directories(wxl-engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(wxl-engine INTERFACE wxl-sdk)

# Declares the entry points in mod.h. A runtime includes the same headers without it, so that it
# does not export the symbols it goes looking for.
add_library(wxl-sdk-mod INTERFACE)
add_library(wxl::sdk-mod ALIAS wxl-sdk-mod)
target_link_libraries(wxl-sdk-mod INTERFACE wxl-sdk)
target_compile_definitions(wxl-sdk-mod INTERFACE WXL_BUILDING_MOD)

# Dear ImGui and ImPlot (MIT, deps/imgui and deps/implot, their own license files), compiled into
# whatever links wxl::imgui: a mod drawing windows through wxl.imgui, and the runtime, whose copy
# owns the contexts. Built only for what links it.
set(WXL_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui" CACHE INTERNAL "Dear ImGui's sources, backends included")
add_library(wxl-imgui STATIC EXCLUDE_FROM_ALL
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/implot/implot.cpp
    deps/implot/implot_items.cpp)
add_library(wxl::imgui ALIAS wxl-imgui)
target_include_directories(wxl-imgui SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/implot>)
target_link_libraries(wxl-imgui PUBLIC wxl-sdk)
set_target_properties(wxl-imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(WXL_SDK_TESTS "Build the contract tests" ${PROJECT_IS_TOP_LEVEL})
option(WXL_SDK_LUA "Build wxl.lua, the Lua mod runtime, for this tree's architecture" ${PROJECT_IS_TOP_LEVEL})

if(WXL_SDK_TESTS)
    enable_testing()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(runtime)
add_subdirectory(host)

# wxl.lua is a mod: from here it is built for the one architecture this tree targets, as a
# child of wxl_add_mod would be. Its own directory builds it for both.
if(WXL_SDK_LUA)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WXL_MOD_ARCH win-x64)
    else()
        set(WXL_MOD_ARCH win-x86)
    endif()
    add_subdirectory(lua)
endif()

if(WXL_SDK_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS wxl-sdk wxl-sdk-mod wxl-engine EXPORT wxl-sdk-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY engine/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES engine/NOTICE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/wxl-sdk RENAME NOTICE-engine.md)
install(DIRECTORY deps/unordered_dense/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES deps/unordered_dense/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/wxl-sdk RENAME LICENSE-unordered_dense)
install(EXPORT wxl-sdk-targets
    FILE wxl-sdk-targets.cmake
    NAMESPACE wxl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wxl-sdk)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/wxl-sdk-config-version.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wxl-sdk-config.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/wxl-sdk-targets.cmake\")\n")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/wxl-sdk-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/wxl-sdk-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wxl-sdk)
