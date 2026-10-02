cmake_minimum_required(VERSION 3.21)

project(DAOTurnBased VERSION 0.1.0 LANGUAGES CXX)

option(
    TB_BUILD_PLUGIN
    "Build turnbased.dll (needs DAOLib and the game's 32-bit architecture)"
    ON
)
option(TB_BUILD_TESTS "Build the unit tests (no game required)" ON)
option(TB_SANITIZE "Build with AddressSanitizer (for the tests)" OFF)
option(TB_ANALYZE "MSVC static analysis (/analyze) on our own targets" OFF)
option(
    TB_COVERAGE
    "Instrument for test coverage (clang-cl only; the core-cov preset)"
    OFF
)

# Dragon Age: Origins is a 32-bit x86 executable, so the plugin the loader
# loads into it must be too. CMakePresets.json selects the x86 toolset and
# the x86-windows-static-md vcpkg triplet; this catches a configure from a
# 64-bit shell with a sentence rather than a DLL the game cannot load. The
# core alone may be built either way (core-cov is 64-bit).
if(TB_BUILD_PLUGIN AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(
        FATAL_ERROR
        "turnbased.dll must be built for 32-bit x86 (the game's architecture); "
        "configure with a preset from CMakePresets.json, through tools/build.ps1"
    )
endif()

# Our own targets: max warnings, warnings fail the build, conformance, and
# third-party headers (DAOLib's included, through <angle brackets>) kept
# quiet. The same flags DAOLib builds itself with.
function(tb_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${target} PRIVATE /Zc:preprocessor)
            target_compile_options(
                ${target}
                PRIVATE /external:anglebrackets /external:W0
            )
        endif()
        if(TB_ANALYZE AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(
                ${target}
                PRIVATE /analyze /analyze:external-
            )
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        )
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
endfunction()

# AddressSanitizer covers every translation unit or none, so it is global
# and set before any target. Only the tests run under it; a sanitized DLL in
# the game would need the ASan runtime beside the exe. vcpkg's libraries are
# built without the STL's container annotations, so they are off (DAOLib's
# CMakeLists has the LNK2038 this avoids).
if(TB_SANITIZE)
    add_compile_options(/fsanitize=address /Oy-)
    add_compile_definitions(
        _DISABLE_STRING_ANNOTATION
        _DISABLE_VECTOR_ANNOTATION
    )
    add_link_options(/INCREMENTAL:NO)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# Test coverage, clang's source-based kind, as SkyrimFollowerTactics measures
# it: every region of tb_core gets a counter, tb_tests runs once, and
# llvm-cov reports (the `coverage` target, tests/CMakeLists.txt). clang-cl
# only; the objects ask for clang_rt.profile themselves, and link.exe is
# told where clang keeps it. LLVM ships that runtime for x64 alone, so the
# core-cov preset is 64-bit.
if(TB_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(
            FATAL_ERROR
            "TB_COVERAGE needs clang-cl (the core-cov preset); the compiler is ${CMAKE_CXX_COMPILER_ID}"
        )
    endif()
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
        OUTPUT_VARIABLE _clang_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    link_directories("${_clang_resource_dir}/lib/windows")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    get_filename_component(_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(TB_LLVM_PROFDATA llvm-profdata HINTS "${_llvm_bin}")
    find_program(TB_LLVM_COV llvm-cov HINTS "${_llvm_bin}")
endif()

# ---------------------------------------------------------------------------
# tb_core -- the turn-based rules and the state machine that runs a fight.
#
# It never includes <Windows.h>, never links DAOLib and never sees a dao::
# type: it takes what the game layer observed each frame and answers with
# what the game layer should do. That is what makes it testable without the
# game, the only kind of test there is (CLAUDE.md, "Layout").
# ---------------------------------------------------------------------------
add_library(
    tb_core
    STATIC
    src/core/Bar.cpp
    src/core/Events.cpp
    src/core/Json.cpp
    src/core/Keys.cpp
    src/core/Portraits.cpp
    src/core/Preview.cpp
    src/core/Rules.cpp
    src/core/Settings.cpp
    src/core/State.cpp
    src/core/TurnMachine.cpp
    src/core/TurnOrder.cpp
)
tb_apply_warnings(tb_core)
target_include_directories(tb_core PUBLIC "${PROJECT_SOURCE_DIR}/src")

if(TB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The core presets stop here: no DAOLib, no plugin, and no format or lint
# targets, which read the plugin's compile database (run them on debug).
if(NOT TB_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# DAOLib, built with us the way Skyrim plugins build CommonLibSSE: a source
# tree added as a subdirectory, which gives DAOLib::daolib and
# add_daolib_plugin. Its dependencies (detours, spdlog) come through our
# vcpkg.json, since a manifest build reads only the top-level manifest.
#
# The tree is extern/DAOLib, a git submodule, once there is one; until then,
# and whenever DAOLib is being changed alongside this, the sibling checkout
# ..\DAOLib. DAOLIB_DIR names another.
# ---------------------------------------------------------------------------
set(DAOLIB_DIR
    ""
    CACHE PATH
    "DAOLib's source tree (default extern/DAOLib, else ../DAOLib)"
)
if(DAOLIB_DIR)
    set(TB_DAOLIB "${DAOLIB_DIR}")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/extern/DAOLib/CMakeLists.txt")
    set(TB_DAOLIB "${PROJECT_SOURCE_DIR}/extern/DAOLib")
else()
    set(TB_DAOLIB "${PROJECT_SOURCE_DIR}/../DAOLib")
endif()
if(NOT EXISTS "${TB_DAOLIB}/cmake/DAOLib.cmake")
    message(
        FATAL_ERROR
        "No DAOLib with add_daolib_plugin at ${TB_DAOLIB}. Add the submodule "
        "(git submodule add <DAOLib's url> extern/DAOLib) or set DAOLIB_DIR."
    )
endif()
message(STATUS "DAOLib: ${TB_DAOLIB}")
add_subdirectory(
    "${TB_DAOLIB}"
    "${CMAKE_BINARY_DIR}/extern/DAOLib"
    EXCLUDE_FROM_ALL
)

# The format and lint targets, DAOLib's own module run over this tree: it
# reads the top-level source directory, our .clang-format and .clang-tidy,
# and tools/check-powershell.ps1.
include("${TB_DAOLIB}/cmake/Quality.cmake")

# ---------------------------------------------------------------------------
# turnbased.dll -- the plugin. src/game is every call into the engine,
# through DAOLib; it is verified by playing (dev/TESTING.md).
# ---------------------------------------------------------------------------
add_daolib_plugin(
    turnbased
    SOURCES
        src/game/Bar.cpp
        src/game/Bridge.cpp
        src/game/Camera.cpp
        src/game/Gate.cpp
        src/game/Preview.cpp
        src/game/SettingsFile.cpp
        src/game/Targeting.cpp
        src/plugin/Plugin.cpp
)
tb_apply_warnings(turnbased)
target_link_libraries(turnbased PRIVATE tb_core)
target_compile_definitions(
    turnbased
    PRIVATE TB_VERSION_STRING="${PROJECT_VERSION}"
)
# DAOLib's address library packs its files, one per build of the game, into
# build\<preset>\daolib\addresses: the loader and the plugin read the one
# for the running game, and tools/deploy.ps1 copies them beside the loader.
# daolib depends on them itself (DAOLib 8ecb9ac), so linking it makes them.

# `cmake --build --preset debug` builds the loader too, so build\<preset>\
# is a game folder's worth of files: version.dll, daolib\plugins\*.dll.
add_custom_target(game_folder ALL DEPENDS version turnbased)
