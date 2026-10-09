# Catch2 comes from vcpkg in the x86 presets (the manifest's `tests`
# feature). core-cov builds with no vcpkg at all, 64-bit with clang-cl, so
# there it is fetched, as SkyrimFollowerTactics' core presets fetch it.
find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found through find_package; fetching it.")
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    # Built as the tests are: at Catch2's own default standard its library has
    # no StringMaker for a std::wstring_view, and a CHECK of one
    # (test_givenback.cpp, issue 21) did not link where Catch2 is fetched
    # (2026-10-03: the core preset in a tree whose configure had not found
    # vcpkg's, and so the mutants there; core-cov always).
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# Over tb_core only: the rules and the state machine, with no game.
add_executable(
    tb_tests
    quiet_crt.cpp
    test_actiontime.cpp
    test_bar.cpp
    test_barwindow.cpp
    test_cooldowns.cpp
    test_delay.cpp
    test_pause.cpp
    test_events.cpp
    test_facing.cpp
    test_fights.cpp
    test_givenback.cpp
    test_inactive.cpp
    test_json.cpp
    test_keys.cpp
    test_machine.cpp
    test_noattack.cpp
    test_nowhere.cpp
    test_longaction.cpp
    test_ownmode.cpp
    test_pace.cpp
    test_partytactics.cpp
    test_perception.cpp
    test_portraits.cpp
    test_preview.cpp
    test_rotation.cpp
    test_rules.cpp
    test_settings.cpp
    test_text.cpp
    test_time.cpp
    test_turnorder.cpp
    test_turnbudget.cpp
    test_turnend.cpp
    test_turntime.cpp
    test_wait.cpp
)
tb_apply_warnings(tb_tests)
target_link_libraries(tb_tests PRIVATE tb_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tb_tests)

# The Python tools' own tests (tools/test_*.py): the turn bar movie's SWF
# shell, where there is a Python.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME python_tools
        COMMAND
            "${Python3_EXECUTABLE}" -m unittest discover -s
            "${PROJECT_SOURCE_DIR}/tools" -p "test_*.py"
    )
endif()

# Coverage, for the core-cov preset:
#
#   .\tools\build.ps1 -Preset core-cov -Coverage
#
# Runs tb_tests once as one process (ctest would run each case in its own
# and leave a .profraw per case), and reports src/core, file by file, with
# the line-by-line view as HTML in build\core-cov\coverage\html\. The tests
# and Catch2 are the means, not what is measured.
if(TB_COVERAGE AND TB_LLVM_PROFDATA AND TB_LLVM_COV)
    set(_cov_dir "${CMAKE_BINARY_DIR}/coverage")
    set(_cov_sources "${PROJECT_SOURCE_DIR}/src/core")
    add_custom_target(
        coverage
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cov_dir}"
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "LLVM_PROFILE_FILE=${_cov_dir}/tb_tests.profraw"
            "$<TARGET_FILE:tb_tests>"
        COMMAND
            "${TB_LLVM_PROFDATA}" merge -sparse "${_cov_dir}/tb_tests.profraw"
            -o "${_cov_dir}/tests.profdata"
        COMMAND
            "${TB_LLVM_COV}" show "$<TARGET_FILE:tb_tests>"
            "-instr-profile=${_cov_dir}/tests.profdata" -format=html
            "-output-dir=${_cov_dir}/html" ${_cov_sources}
        COMMAND
            "${TB_LLVM_COV}" report "$<TARGET_FILE:tb_tests>"
            "-instr-profile=${_cov_dir}/tests.profdata" ${_cov_sources}
        DEPENDS tb_tests
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Coverage of src/core by the tests; HTML in ${_cov_dir}/html"
        VERBATIM
        USES_TERMINAL
    )
elseif(TB_COVERAGE)
    message(
        WARNING
        "TB_COVERAGE is ON but llvm-profdata or llvm-cov was not found; no coverage target"
    )
endif()
