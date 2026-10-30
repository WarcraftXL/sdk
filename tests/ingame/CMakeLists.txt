# The in-game test suite: the test mods and wxl.selftest, with the examples selftest calls,
# assembled as a Mods folder ready to copy into a client next to its wxl.lua (README.md).
#
#   <WXL_INGAME_DIR>/
#     wxl.selftest/  wxl.test-*/       the suite, Lua mods as they are in this folder
#     wxl.test-throw/bin/<arch>/...    the C++ test mods, built for this tree's architecture
#     wxl.scout/  wxl.lua-services/  wxl.memory-report/  wxl.probe/  wxl.ground-marks/  wxl.dig-dust/   the examples it calls
#     wxl.model-swap/                  the points example, which logs every model and swaps the murloc
#
# One tree is one architecture: configure the x64 tree and the x86 tree with the same
# WXL_INGAME_DIR and build both to fill bin/x64/ and bin/x86/ of each C++ mod.

set(WXL_INGAME_DIR "${CMAKE_BINARY_DIR}/ingame/Mods" CACHE PATH
    "Where the in-game test suite is assembled as a Mods folder")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ingame_arch x64)
else()
    set(ingame_arch x86)
endif()

# The Lua mods of the suite, copied as they are.
set(ingame_lua_mods
    wxl.selftest
    wxl.test-abi-old
    wxl.test-both
    wxl.test-events
    wxl.test-fields
    wxl.test-order-a
    wxl.test-order-b
    wxl.test-reload
    wxl.test-search
    wxl.test-watch-other)

# The C++ mods of the suite, wxl.<name>/ each, built as <name>.dll from their sources.
set(ingame_cpp_mods test-throw test-throw-ready test-manifest test-assets test-points test-bones)
set(ingame_test-throw_sources src/TestThrow.cpp)
set(ingame_test-throw-ready_sources src/TestThrowReady.cpp)
set(ingame_test-manifest_sources src/TestManifest.cpp)
set(ingame_test-assets_sources src/TestAssets.cpp)
set(ingame_test-points_sources src/TestPoints.cpp)
set(ingame_test-bones_sources src/TestBones.cpp)

# The Lua examples selftest calls into, copied from examples/ under their manifest ids.
set(ingame_examples lua-services memory-report probe ground-marks dig-dust)

set(ingame_commands)
set(ingame_targets)

foreach(mod IN LISTS ingame_lua_mods)
    list(APPEND ingame_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${mod}" "${WXL_INGAME_DIR}/${mod}")
endforeach()

foreach(mod IN LISTS ingame_cpp_mods)
    set(target wxl-ingame-${mod})
    set(folder "${CMAKE_CURRENT_SOURCE_DIR}/wxl.${mod}")
    set(sources)
    foreach(source IN LISTS ingame_${mod}_sources)
        list(APPEND sources "${folder}/${source}")
    endforeach()
    # WXL_MOD_ARCH is this tree's (tests/CMakeLists.txt): the library target alone.
    wxl_add_mod(${target} SOURCES ${sources} MANIFEST "${folder}/mod.json" OUTPUT_NAME ${mod})
    # The engine headers: test-points reads the model through them.
    target_link_libraries(${target} PRIVATE wxl::engine)
    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    list(APPEND ingame_targets ${target})
    list(APPEND ingame_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${folder}/mod.json" "${WXL_INGAME_DIR}/wxl.${mod}/mod.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${target}>" "${WXL_INGAME_DIR}/wxl.${mod}/bin/${ingame_arch}/${mod}.dll")
endforeach()

# The scout and model-swap examples are the tree's own builds of them (tests/CMakeLists.txt).
list(APPEND ingame_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/model-swap/mod.json" "${WXL_INGAME_DIR}/wxl.model-swap/mod.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:model-swap>" "${WXL_INGAME_DIR}/wxl.model-swap/bin/${ingame_arch}/model-swap.dll")
list(APPEND ingame_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/scout/mod.json" "${WXL_INGAME_DIR}/wxl.scout/mod.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:scout>" "${WXL_INGAME_DIR}/wxl.scout/bin/${ingame_arch}/scout.dll")

foreach(example IN LISTS ingame_examples)
    list(APPEND ingame_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/${example}" "${WXL_INGAME_DIR}/wxl.${example}")
endforeach()

# The probe's plan for a test session with the suite: the few steps a person still plays.
list(APPEND ingame_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/steps.txt" "${WXL_INGAME_DIR}/wxl.probe/steps.txt")

add_custom_target(wxl-ingame ALL
    ${ingame_commands}
    COMMENT "In-game test suite -> ${WXL_INGAME_DIR}"
    VERBATIM)
add_dependencies(wxl-ingame ${ingame_targets} scout model-swap)
