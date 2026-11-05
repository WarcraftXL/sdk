# SPDX-License-Identifier: GPL-3.0-or-later
# ctest: the headers, the checks against the fake core, the package and its examples, the offsets.

set(wxl_test_defs WXL_EXTENSION WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
include_directories("${PROJECT_SOURCE_DIR}/tools/fake-host")

# --- every public header compiles on its own ----------------------------------------------------
file(GLOB_RECURSE wxl_headers RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/include/wxl/*.h" "${PROJECT_SOURCE_DIR}/include/wxl/*.hpp")
set(wxl_header_tus "")
foreach(header IN LISTS wxl_headers)
    string(REGEX REPLACE "^.*include/" "" inc "${header}")
    string(MAKE_C_IDENTIFIER "${inc}" id)
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/headers/${id}.cpp")
    file(CONFIGURE OUTPUT "${tu}" CONTENT "#include \"${inc}\"\n#include \"${inc}\"\n")
    list(APPEND wxl_header_tus "${tu}")
    if(header MATCHES "\\.h$")
        set(tu "${CMAKE_CURRENT_BINARY_DIR}/headers/${id}.c")
        file(CONFIGURE OUTPUT "${tu}" CONTENT "#include \"${inc}\"\n")
        list(APPEND wxl_header_tus "${tu}")
    endif()
endforeach()
add_library(wxl-sdk-headers OBJECT ${wxl_header_tus})
target_link_libraries(wxl-sdk-headers PRIVATE wxl_game)
target_compile_definitions(wxl-sdk-headers PRIVATE ${wxl_test_defs})
add_test(NAME headers COMMAND ${CMAKE_COMMAND} --build "${PROJECT_BINARY_DIR}" --target wxl-sdk-headers)

# --- the checks: Windows executables against the fake core (tools/fake-host/FakeCore.hpp) -----------------
function(wxl_add_check name source link)
    add_executable(wxl-${name} ${source})
    target_link_libraries(wxl-${name} PRIVATE ${link})
    target_compile_definitions(wxl-${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(wxl-${name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_test(NAME ${name} COMMAND wxl-${name})
endfunction()

wxl_add_check(extension-check ExtensionCheck.cpp wxl_game)
wxl_add_check(objects-check ObjectsCheck.cpp wxl_game)
wxl_add_check(intercept-check InterceptCheck.cpp wxl_sdk)
wxl_add_check(task-check TaskCheck.cpp wxl_game)
wxl_add_check(hot-path-check HotPathCheck.cpp wxl_game)
wxl_add_check(seh-check SehCheck.cpp wxl_sdk)
wxl_add_check(settings-check SettingsCheck.cpp wxl_game)
target_include_directories(wxl-settings-check PRIVATE "${PROJECT_SOURCE_DIR}/examples/settings/src")

# The settings tool: the committed header is what it writes, and a broken header is refused.
find_program(WXL_LUAJIT NAMES luajit luajit-2.1)
if(WXL_LUAJIT AND EXISTS "/usr/lib/llvm-19/lib/libclang-19.so.1")
    add_test(NAME settings-tool
             COMMAND "${WXL_LUAJIT}" "${PROJECT_SOURCE_DIR}/tools/settings/settings.lua" --check -q
                     "${PROJECT_SOURCE_DIR}/examples/settings/src/GreeterSettings.hpp")
    add_test(NAME settings-tool-refuses
             COMMAND "${WXL_LUAJIT}" "${PROJECT_SOURCE_DIR}/tools/settings/settings.lua" --check -q
                     "${CMAKE_CURRENT_SOURCE_DIR}/settings/Broken.hpp")
    set_tests_properties(settings-tool-refuses PROPERTIES WILL_FAIL TRUE)
endif()

# --- the example, out of tree, against the installed package ----------------------------------
set(prefix "${CMAKE_CURRENT_BINARY_DIR}/package")
set(hello "${CMAKE_CURRENT_BINARY_DIR}/hello")
set(hello_args -S "${PROJECT_SOURCE_DIR}/examples" -B "${hello}" -G "${CMAKE_GENERATOR}"
               "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_BUILD_TYPE=$<CONFIG>")
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND hello_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND hello_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_CROSSCOMPILING_EMULATOR)
    list(APPEND hello_args "-DCMAKE_CROSSCOMPILING_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}")
endif()

add_test(NAME package-install
         COMMAND ${CMAKE_COMMAND} --install "${PROJECT_BINARY_DIR}" --prefix "${prefix}" --config $<CONFIG>)
add_test(NAME example-configure COMMAND ${CMAKE_COMMAND} ${hello_args})
add_test(NAME example-build COMMAND ${CMAKE_COMMAND} --build "${hello}" --config $<CONFIG>)
set_tests_properties(package-install PROPERTIES FIXTURES_SETUP sdk_package)
set_tests_properties(example-configure PROPERTIES FIXTURES_REQUIRED sdk_package FIXTURES_SETUP hello_configured)
set_tests_properties(example-build PROPERTIES FIXTURES_REQUIRED hello_configured FIXTURES_SETUP hello_built)

# A package that asks for another major is refused by find_package.
add_test(NAME package-version
         COMMAND ${CMAKE_COMMAND} "-DPREFIX=${prefix}" -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageVersion.cmake")
set_tests_properties(package-version PROPERTIES FIXTURES_REQUIRED sdk_package)

# --- the examples loaded by the installed fake host (wxl_add_load_test) -------------------------
add_test(NAME examples-load COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${hello}" -C $<CONFIG> --output-on-failure)
set_tests_properties(examples-load PROPERTIES FIXTURES_REQUIRED hello_built)

# --- the code the docs quote is the examples' code ----------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME docs-quotes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_doc_quotes.py"
                                       "${PROJECT_SOURCE_DIR}")
endif()

# --- the generated offsets: what the atlas gives, every address verified against Wow.exe ---------
set(WXL_ATLAS_DIR "${PROJECT_SOURCE_DIR}/../atlas" CACHE PATH "The atlas checkout the offsets are generated from")
if(WXL_LUAJIT AND EXISTS "${WXL_ATLAS_DIR}/lib/atlas/load.lua")
    add_test(NAME offsets-generated
             COMMAND ${WXL_LUAJIT} "${PROJECT_SOURCE_DIR}/tools/offsets/gen.lua" --atlas "${WXL_ATLAS_DIR}" --check)
    # Reads Wow.exe from $WXL_WOW_EXE; skipped without one.
    add_test(NAME offsets-verified
             COMMAND ${WXL_LUAJIT} "${PROJECT_SOURCE_DIR}/tools/offsets/check_verified.lua" --atlas "${WXL_ATLAS_DIR}")
    set_tests_properties(offsets-verified PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS "wxl-sdk: no luajit or no atlas at ${WXL_ATLAS_DIR}: the offsets tests are not added")
endif()

# --- the API reference, generated into the build tree, then checked for what it must list --------
find_program(WXL_PYTHON3 NAMES python3 python)
if(WXL_PYTHON3)
    add_test(NAME docgen
             COMMAND "${WXL_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/check_docgen.py"
                     "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/docs-site")
endif()

# --- the C ABI: struct layouts, status codes, versions, enumerators and event rows, held to abi.lock ---
file(GLOB abi_headers RELATIVE "${PROJECT_SOURCE_DIR}/include" CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/include/wxl/abi/*.h" "${PROJECT_SOURCE_DIR}/include/wxl/events/*.hpp")
wxl_add_abi_lock(abi-lock
    LOCK "${PROJECT_SOURCE_DIR}/abi.lock"
    INCLUDE "${PROJECT_SOURCE_DIR}/include"
    HEADERS ${abi_headers}
    EVENTS "${PROJECT_SOURCE_DIR}/include/wxl/events/Events.def"
    MAJOR WXL_SDK_VERSION_MAJOR WXL_API_VERSION
    MINOR WXL_SDK_VERSION_MINOR)
if(WXL_LUAJIT)
    add_test(NAME abi-rules COMMAND "${WXL_LUAJIT}" "${CMAKE_CURRENT_SOURCE_DIR}/check_abi_rules.lua" "${PROJECT_SOURCE_DIR}")
endif()
add_subdirectory(native)
