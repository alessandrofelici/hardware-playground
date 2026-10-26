cmake_minimum_required(VERSION 3.21)

# Multi-app firmware for the FreeWili OG (two RP2040s: display CPU + main CPU),
# built on the FreeWili OG board support package in external/wiliOGbsp.
# Every apps/<name>/ folder is one app; see apps/CMakeLists.txt and README.md.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(FWOG_BSP_DIR "${CMAKE_CURRENT_LIST_DIR}/external/wiliOGbsp" CACHE PATH
    "FreeWili OG BSP checkout (git submodule)")
if(NOT EXISTS "${FWOG_BSP_DIR}/bsp/CMakeLists.txt")
    message(FATAL_ERROR
        "FreeWili OG BSP not found at ${FWOG_BSP_DIR}. "
        "Run: git submodule update --init")
endif()

# The BSP's board header. Never pass -DPICO_BOARD on the command line: it
# would override this and silently build for the wrong board.
set(PICO_BOARD freewili_og CACHE STRING "Board type" FORCE)
list(APPEND PICO_BOARD_HEADER_DIRS "${FWOG_BSP_DIR}/bsp/boards")

# UART0 is the inter-CPU link on both CPUs and must never carry stdio.
set(PICO_STDIO_UART 0 CACHE BOOL "stdio over UART (forced off: UART0 is the link)" FORCE)

include(${FWOG_BSP_DIR}/pico_sdk_import.cmake)
project(hardware_playground C CXX ASM)
# The BSP runs Python at build time (UF2 info records, display image embed).
find_package(Python3 COMPONENTS Interpreter REQUIRED)

pico_sdk_init()

add_subdirectory(${FWOG_BSP_DIR}/bsp fwog_bsp)

# The BSP's own top-level CMakeLists.txt defines these two and its app helpers
# read them, so a project consuming bsp/ has to define them as well. They
# describe THIS repository: main images record which commit built them.
# Display images deliberately never see them (see the BSP's fwog_add_uf2_info).
execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE FWOG_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _git_rc)
if(NOT _git_rc EQUAL 0 OR FWOG_GIT_DESCRIBE STREQUAL "")
    set(FWOG_GIT_DESCRIBE "dev")
endif()
execute_process(
    COMMAND git log -1 --format=%ct
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE FWOG_GIT_COMMIT_TS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _ts_rc)
if(NOT _ts_rc EQUAL 0 OR NOT FWOG_GIT_COMMIT_TS MATCHES "^[0-9]+$")
    set(FWOG_GIT_COMMIT_TS 0)
endif()
message(STATUS "hardware_playground version: ${FWOG_GIT_DESCRIBE}")

add_subdirectory(apps)
