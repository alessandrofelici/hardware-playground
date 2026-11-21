# rootclock simulator: the display app (display/main.c, the S1/S2 renderers
# and core/) built for the PC, against tools/sim/include/fwog_display.h
# instead of the BSP. For working on rootclock with no board on the desk:
#
#   cmake -S apps/rootclock/tools/sim -B build/rootclock-sim -G Ninja \
#         -DCMAKE_C_COMPILER=gcc
#   cmake --build build/rootclock-sim
#   python3 apps/rootclock/tools/sim/shots.py build/rootclock-sim/rootclock_sim \
#           build/rootclock-shots           # PNG of every screen, one sheet
#   SIM_PTY=/tmp/rootclock-tty build/rootclock-sim/rootclock_sim &
#   python3 apps/rootclock/tools/hw_check.py --port /tmp/rootclock-tty
#
# The main CPU's relay (main/bridge.c) runs inside the simulator too, joined
# to the display by the BSP's real link framing. With SIM_C6="<command>" it
# talks to that command's stdin/stdout as the ESP32-C6 -- fake_c6.py plays the
# Bottlenose sketch and can upload to a real root-metrics:
#   SIM_C6="python3 apps/rootclock/tools/sim/fake_c6.py --url \
#           http://127.0.0.1:8080/api/results --token dev" rootclock_sim
# Without SIM_C6 there is no WiFi module, as on a board with no Bottlenose.
#
# Linux/macOS only (posix_openpt, CLOCK_MONOTONIC). Standalone like tests/:
# the firmware build never adds this directory. What it cannot show: the
# panel's real colours and timing, the beep, the LED bar, power-off.
cmake_minimum_required(VERSION 3.13)
project(rootclock_sim C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(RC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(BSP ${RC}/../../external/wiliOGbsp/bsp)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# The BSP's pure halves, exactly as its own host tests build them.
add_library(sim_bsp_pure STATIC
    ${BSP}/display_cpu/lcd/lcd_text.c
    ${BSP}/common/crc.c
    ${BSP}/common/link/link_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sim_buttons.c
    ${BSP}/display_cpu/input/buttons.c)
target_include_directories(sim_bsp_pure PRIVATE ${BSP}/display_cpu ${BSP})
target_compile_definitions(sim_bsp_pure PRIVATE HOST_TEST=1)

add_executable(rootclock_sim
    ${RC}/display/main.c
    ${RC}/display/ui_bigdigits.c
    ${RC}/display/ui_clock.c
    ${RC}/display/ui_settings.c
    ${RC}/display/assets/faction_icons.c
    ${RC}/core/rc_stopwatch.c
    ${RC}/core/rc_player.c
    ${RC}/core/rc_catalog.c
    ${RC}/core/rc_settings.c
    ${RC}/core/rc_alarm.c
    ${RC}/core/rc_input.c
    ${RC}/core/rc_game.c
    ${RC}/core/rc_result.c
    ${RC}/core/rc_format.c
    ${RC}/core/rc_battery.c
    ${RC}/core/rc_uplink.c
    ${RC}/display/battery.c
    ${RC}/display/export.c
    ${RC}/main/bridge.c
    sim_bsp.c
    sim_link.c
    sim_beeper.c)
# include/ first: it shadows fwog_display.h and the SDK headers.
target_include_directories(rootclock_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BSP}
    ${RC}/common
    ${RC}/main
    ${RC}/core
    ${RC}/display
    ${RC}/display/assets)
target_link_libraries(rootclock_sim PRIVATE sim_bsp_pure)
