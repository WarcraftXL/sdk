# SPDX-License-Identifier: GPL-3.0-or-later
# The WarcraftXL SDK: the targets an extension builds against, its tests and the installed package.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/WarcraftXLExtension.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/WarcraftXLNative.cmake")
wxl_read_sdk_version("${CMAKE_CURRENT_LIST_DIR}/include/wxl/abi/Version.h" WXL_SDK_MAJOR WXL_SDK_MINOR)
set(WXL_SDK_PATCH 0 CACHE STRING "Patch number of the package version (a release's build number)")

project(wxl-sdk VERSION ${WXL_SDK_MAJOR}.${WXL_SDK_MINOR}.${WXL_SDK_PATCH} LANGUAGES CXX C)
set_property(GLOBAL PROPERTY WXL_SDK_VERSION "${WXL_SDK_MAJOR}.${WXL_SDK_MINOR}")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(wxl_sdk_top_level ON)
else()
    set(wxl_sdk_top_level OFF)
endif()
option(WXL_SDK_TESTS "Build the SDK's tests" ${wxl_sdk_top_level})

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "wxl-sdk targets the 32-bit Windows client only: configure with -A Win32, or with "
                        "an x86 Windows toolchain file (CMAKE_TOOLCHAIN_FILE).")
endif()

# --- the targets: one include root; offsets and game name what an extension builds on ----------
add_library(wxl_sdk INTERFACE)
target_include_directories(wxl_sdk INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(wxl_sdk INTERFACE cxx_std_20)
wxl_float_options(WXL_FLOAT_OPTIONS)
target_compile_options(wxl_sdk INTERFACE ${WXL_FLOAT_OPTIONS})
add_library(WarcraftXL::sdk ALIAS wxl_sdk)

add_library(wxl_offsets INTERFACE)
target_link_libraries(wxl_offsets INTERFACE wxl_sdk)
add_library(WarcraftXL::offsets ALIAS wxl_offsets)

add_library(wxl_game INTERFACE)
target_link_libraries(wxl_game INTERFACE wxl_offsets)
add_library(WarcraftXL::game ALIAS wxl_game)

# --- the package --------------------------------------------------------------------------------
if(wxl_sdk_top_level)
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/WarcraftXLConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)

    install(DIRECTORY include/ DESTINATION include)
    install(DIRECTORY tools/settings tools/fake-host tools/abi tools/stock DESTINATION lib/cmake/WarcraftXL)
    install(DIRECTORY tools/baseline DESTINATION lib/cmake/WarcraftXL PATTERN "test_*.lua" EXCLUDE)
    install(FILES cmake/WarcraftXLConfig.cmake cmake/WarcraftXLExtension.cmake cmake/WarcraftXLTests.cmake cmake/WarcraftXLNative.cmake
                  "${CMAKE_CURRENT_BINARY_DIR}/WarcraftXLConfigVersion.cmake"
            DESTINATION lib/cmake/WarcraftXL)
    install(DIRECTORY docs/ examples DESTINATION share/doc/wxl-sdk)
    install(FILES README.md LICENSE COPYING DESTINATION share/doc/wxl-sdk)
endif()

if(WXL_SDK_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
