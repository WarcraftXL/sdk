cmake_minimum_required(VERSION 3.21)

# No language here: wxl_add_mod enables C++ in each per-architecture build.
project(outline VERSION 1.0.0 LANGUAGES NONE)

# Where the SDK is. The default is this checkout; pass -DWXL_SDK_DIR=<path> for another one.
set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the WarcraftXL SDK")
include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")

wxl_add_mod(outline
    SOURCES src/Outline.cpp
    MANIFEST mod.json
    TARGETS win-x64 win-x86)

# The programs, SPIR-V compiled from shaders/ with the Vulkan SDK's compiler (glslangValidator or
# glslang) when one is found, into the build tree; without one, the copies in generated/, made the
# same way:
#   glslangValidator -V --target-env vulkan1.3 -I<sdk>/include --vn kOutlineMaskFrag -o generated/mask_frag.h shaders/mask.frag
find_program(WXL_OUTLINE_GLSLANG NAMES glslangValidator glslang)
if(WXL_OUTLINE_GLSLANG AND TARGET outline)
    set(spirv "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    foreach(pair "mask_frag=kOutlineMaskFrag=mask.frag" "edge_frag=kOutlineEdgeFrag=edge.frag")
        string(REPLACE "=" ";" parts "${pair}")
        list(GET parts 0 header)
        list(GET parts 1 array)
        list(GET parts 2 source)
        add_custom_command(OUTPUT "${spirv}/${header}.h"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${spirv}"
            COMMAND "${WXL_OUTLINE_GLSLANG}" -V --target-env vulkan1.3 "-I${WXL_SDK_DIR}/include" --vn ${array}
                    -o "${spirv}/${header}.h" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}"
            VERBATIM)
        list(APPEND headers "${spirv}/${header}.h")
    endforeach()
    add_custom_target(outline-spirv DEPENDS ${headers})
    add_dependencies(outline outline-spirv)
    target_include_directories(outline PRIVATE "${spirv}")
elseif(TARGET outline)
    target_include_directories(outline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
endif()
