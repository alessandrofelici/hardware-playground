# rootclock host tests: the SDK-free core/ compiled for the PC and run with
# CTest. No Pico SDK, no BSP library, no board.
#
# Standalone on purpose: the firmware build never adds this directory, and
# this project never touches the SDK. It needs a host C compiler (gcc or
# clang). From the repository root, for example:
#
#   cmake -S apps/rootclock/tests -B build/rootclock-tests -G Ninja \
#         -DCMAKE_C_COMPILER=gcc
#   cmake --build build/rootclock-tests
#   ctest --test-dir build/rootclock-tests --output-on-failure
#
# (build/ is git-ignored and fw never deletes it.)
cmake_minimum_required(VERSION 3.13)
project(rootclock_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(RC_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
# ASSERT_EQ / ASSERT_TRUE / TEST_RETURN and the shared failure counter.
set(BSP_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/wiliOGbsp/tests)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

add_library(rc_core STATIC
    ${RC_CORE}/rc_stopwatch.c
    ${RC_CORE}/rc_player.c
    ${RC_CORE}/rc_catalog.c
    ${RC_CORE}/rc_settings.c
    ${RC_CORE}/rc_alarm.c
    ${RC_CORE}/rc_input.c
    ${RC_CORE}/rc_game.c
    ${RC_CORE}/rc_result.c
    ${RC_CORE}/rc_format.c
    ${RC_CORE}/rc_battery.c
    ${RC_CORE}/rc_uplink.c)
target_include_directories(rc_core PUBLIC ${RC_CORE})
target_compile_definitions(rc_core PUBLIC HOST_TEST=1)

function(rc_add_test name)
    add_executable(${name} ${name}.c ${BSP_TESTS}/test_util.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${BSP_TESTS})
    target_link_libraries(${name} PRIVATE rc_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rc_add_test(test_rc_clock)      # Stopwatch, TurnLog, Player
rc_add_test(test_rc_settings)   # catalog, Settings, SettingsMenu, alarm
rc_add_test(test_rc_format)     # screen time strings
rc_add_test(test_rc_input)      # button map, holds
rc_add_test(test_rc_game)       # state machine, invariants, AT script
rc_add_test(test_rc_result)     # GameResult + contract v1 JSON
rc_add_test(test_rc_battery)    # battery percent, drain rate, time left
rc_add_test(test_rc_uplink)     # upload protocol, outbox, sender, main's bridge
target_sources(test_rc_uplink PRIVATE ${RC_CORE}/../main/bridge.c)
target_include_directories(test_rc_uplink PRIVATE ${RC_CORE}/../main)
