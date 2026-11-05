# SPDX-License-Identifier: GPL-3.0-or-later
# find_package(WarcraftXL CONFIG): the targets sdk, offsets and game, and wxl_add_extension.

get_filename_component(_wxl_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/WarcraftXLExtension.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WarcraftXLNative.cmake")
wxl_read_sdk_version("${_wxl_prefix}/include/wxl/abi/Version.h" _wxl_major _wxl_minor)
set_property(GLOBAL PROPERTY WXL_SDK_VERSION "${_wxl_major}.${_wxl_minor}")

if(NOT TARGET WarcraftXL::sdk)
    add_library(WarcraftXL::sdk INTERFACE IMPORTED)
    wxl_float_options(_wxl_float)
    set_target_properties(WarcraftXL::sdk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_wxl_prefix}/include"
        INTERFACE_COMPILE_FEATURES cxx_std_20
        INTERFACE_COMPILE_OPTIONS "${_wxl_float}")
    unset(_wxl_float)

    add_library(WarcraftXL::offsets INTERFACE IMPORTED)
    set_target_properties(WarcraftXL::offsets PROPERTIES INTERFACE_LINK_LIBRARIES WarcraftXL::sdk)

    add_library(WarcraftXL::game INTERFACE IMPORTED)
    set_target_properties(WarcraftXL::game PROPERTIES INTERFACE_LINK_LIBRARIES WarcraftXL::offsets)
endif()

foreach(_wxl_c IN LISTS WarcraftXL_FIND_COMPONENTS)
    if(_wxl_c MATCHES "^(sdk|offsets|game)$")
        set(WarcraftXL_${_wxl_c}_FOUND TRUE)
    else()
        set(WarcraftXL_${_wxl_c}_FOUND FALSE)
        if(WarcraftXL_FIND_REQUIRED_${_wxl_c})
            set(WarcraftXL_FOUND FALSE)
            set(WarcraftXL_NOT_FOUND_MESSAGE "unknown component '${_wxl_c}' (sdk, offsets, game)")
        endif()
    endif()
endforeach()
unset(_wxl_c)

set(WarcraftXL_SDK_VERSION "${_wxl_major}.${_wxl_minor}")
unset(_wxl_prefix)
unset(_wxl_major)
unset(_wxl_minor)
