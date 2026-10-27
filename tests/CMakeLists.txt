# The conformance checks. A runtime passes them by building contract.cpp against its own host in
# place of fake_host.hpp.
add_executable(wxl-contract contract.cpp)
target_link_libraries(wxl-contract PRIVATE wxl::sdk wxl::engine)
target_include_directories(wxl-contract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include)
# C++20, as wxl_add_mod builds a mod: wxl::Log's formatting is std::format.
set_target_properties(wxl-contract PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

if(MSVC)
    target_compile_options(wxl-contract PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-contract PRIVATE -Wall -Wextra)
endif()

add_test(NAME contract COMMAND wxl-contract)

# The terrain's grid (wxl/terrain/grid.hpp) against the client's own arithmetic.
add_executable(wxl-terrain-grid terrain_grid.cpp)
target_link_libraries(wxl-terrain-grid PRIVATE wxl::sdk)
add_test(NAME terrain-grid COMMAND wxl-terrain-grid)

# wxl/services/imgui.hpp with Dear ImGui and ImPlot compiled in, as a mod builds it.
add_executable(wxl-imgui-facade imgui_facade.cpp)
target_link_libraries(wxl-imgui-facade PRIVATE wxl::sdk wxl::imgui)
target_include_directories(wxl-imgui-facade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(wxl-imgui-facade PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME imgui-facade COMMAND wxl-imgui-facade)

# The field table against the args structs it describes.
add_executable(wxl-reflect reflect.cpp)
target_link_libraries(wxl-reflect PRIVATE wxl::sdk)

if(MSVC)
    target_compile_options(wxl-reflect PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-reflect PRIVATE -Wall -Wextra)
endif()

add_test(NAME reflect COMMAND wxl-reflect)

# The engine headers on this architecture: every layout's static_asserts, every layout hash
# against the xxHash library (deps/xxhash, BSD-2-Clause, its own license file), and the table the
# runtimes answer wxl.engine from against the headers.
add_executable(wxl-engine-tests engine.cpp)
target_link_libraries(wxl-engine-tests PRIVATE wxl::engine)
target_include_directories(wxl-engine-tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/xxhash)
set_target_properties(wxl-engine-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(wxl-engine-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-engine-tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME engine COMMAND wxl-engine-tests)

# The shared runtime scaffolding, against a binding backed by a table instead of a client.
add_executable(wxl-runtime-tests runtime.cpp)
target_link_libraries(wxl-runtime-tests PRIVATE wxl::runtime)

if(MSVC)
    target_compile_options(wxl-runtime-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-runtime-tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME runtime COMMAND wxl-runtime-tests)

# wxl.lua against the mod host: Lua mods written into a scratch tree, the lua-hello example,
# the events raised from both threads, a handler that errors, a reload.
if(TARGET wxl-lua)
    add_executable(wxl-lua-tests lua_tests.cpp)
    target_link_libraries(wxl-lua-tests PRIVATE wxl::host)
    target_include_directories(wxl-lua-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include)
    add_dependencies(wxl-lua-tests wxl-lua)

    if(MSVC)
        target_compile_options(wxl-lua-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wxl-lua-tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME lua
        COMMAND wxl-lua-tests $<TARGET_FILE:wxl-lua>
                ${CMAKE_CURRENT_SOURCE_DIR}/../examples/lua-hello
                ${CMAKE_CURRENT_BINARY_DIR}/lua-scratch)

    # The committed editor definitions against what the tables say now.
    add_test(NAME lua-types
        COMMAND wxl-lua-types --check ${CMAKE_CURRENT_SOURCE_DIR}/../lua/types/wxl.d.lua)
endif()

# Every public header in one mod, at the strictest warning level with warnings as errors, as
# C++17 and as C++20: a mod built against the SDK compiles clean. Building it is the check.
foreach(standard 17 20)
    add_library(wxl-headers-cxx${standard} MODULE headers.cpp)
    target_link_libraries(wxl-headers-cxx${standard} PRIVATE wxl::sdk-mod)
    set_target_properties(wxl-headers-cxx${standard} PROPERTIES
        CXX_STANDARD ${standard} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF PREFIX "")
    if(MSVC)
        target_compile_options(wxl-headers-cxx${standard} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(wxl-headers-cxx${standard} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# The guide's example, built as a mod is, with warnings as errors, then loaded through the mod
# host and driven with the fake services. WXL_MOD_ARCH makes wxl_add_mod declare the library
# target of this tree's architecture.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WXL_MOD_ARCH win-x64)
else()
    set(WXL_MOD_ARCH win-x86)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../examples/scout ${CMAKE_CURRENT_BINARY_DIR}/scout)
if(MSVC)
    target_compile_options(scout PRIVATE /WX)
else()
    target_compile_options(scout PRIVATE -Werror)
endif()

# The in-game test suite, assembled as a Mods folder for a client (ingame/README.md).
add_subdirectory(ingame)

# The points example: built, warnings as errors, so the typed point facade compiles as a mod sees it.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../examples/model-swap ${CMAKE_CURRENT_BINARY_DIR}/model-swap)
if(MSVC)
    target_compile_options(model-swap PRIVATE /WX)
else()
    target_compile_options(model-swap PRIVATE -Werror)
endif()
# The models example, the same way: the wxl.m2 facade and the model events as a mod sees them.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../examples/murloc-hat ${CMAKE_CURRENT_BINARY_DIR}/murloc-hat)
if(MSVC)
    target_compile_options(murloc-hat PRIVATE /WX)
else()
    target_compile_options(murloc-hat PRIVATE -Werror)
endif()
# The render example, the same way: wxl.render's facade, a batch kept and drawn again, as a mod sees them.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../examples/outline ${CMAKE_CURRENT_BINARY_DIR}/outline)
if(MSVC)
    target_compile_options(outline PRIVATE /WX)
else()
    target_compile_options(outline PRIVATE -Werror)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../examples/asset-provider ${CMAKE_CURRENT_BINARY_DIR}/asset-provider)
if(MSVC)
    target_compile_options(asset-provider PRIVATE /WX)
else()
    target_compile_options(asset-provider PRIVATE -Werror)
endif()

add_executable(wxl-scout-tests scout_tests.cpp)
target_link_libraries(wxl-scout-tests PRIVATE wxl::host)
target_include_directories(wxl-scout-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include)
add_dependencies(wxl-scout-tests scout)
if(MSVC)
    target_compile_options(wxl-scout-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-scout-tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME examples
    COMMAND wxl-scout-tests $<TARGET_FILE:scout>
            ${CMAKE_CURRENT_SOURCE_DIR}/../examples/scout/mod.json
            ${CMAKE_CURRENT_BINARY_DIR}/scout-scratch)

# The documents against the example they quote. A tutorial whose snippets have drifted still
# builds and still reads well; only this says it is wrong.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME docs COMMAND "${Python3_EXECUTABLE}" -I
             "${CMAKE_CURRENT_SOURCE_DIR}/../examples/check_docs.py")
else()
    message(WARNING "Python 3 was not found, so the documentation check will not run. "
        "Run sdk/examples/check_docs.py by hand.")
endif()

# The object facades' members and the guides' tables of them, against object_reads.def: what
# wxl-object-members writes (sdk/tools/objects) must be what is committed.
add_executable(wxl-object-members ${CMAKE_CURRENT_SOURCE_DIR}/../tools/objects/gen_members.cpp)
target_link_libraries(wxl-object-members PRIVATE wxl::sdk)
target_include_directories(wxl-object-members PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lua/src)
if(MSVC)
    target_compile_options(wxl-object-members PRIVATE /W4 /permissive-)
else()
    target_compile_options(wxl-object-members PRIVATE -Wall -Wextra)
endif()
add_test(NAME object-members
    COMMAND wxl-object-members --check ${CMAKE_CURRENT_SOURCE_DIR}/..)
