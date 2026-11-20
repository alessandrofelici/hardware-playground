# rootclock -- a chess-clock-style turn timer for 3-4 players of Root.
# Planning documents: documents/ at the repository root.
#
# One app = a display-CPU half and a main-CPU half, declared display first.
# The main UF2 carries the display image, so rootclock_main.uf2 is the single
# file that flashes both CPUs.
#
# core/ is SDK-free (host-testable, see tests/); display/ uses the BSP.
# VERSION is exactly three digits, bumped by hand, and must match
# RC_APP_VERSION in core/rc_types.h.

add_executable(rootclock_display
    display/main.c
    display/ui_bigdigits.c
    display/ui_clock.c
    display/ui_settings.c
    display/beeper.c
    display/assets/faction_icons.c
    core/rc_stopwatch.c
    core/rc_player.c
    core/rc_catalog.c
    core/rc_settings.c
    core/rc_alarm.c
    core/rc_input.c
    core/rc_game.c
    core/rc_result.c
    core/rc_format.c)
target_include_directories(rootclock_display PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/core
    ${CMAKE_CURRENT_LIST_DIR}/display
    ${CMAKE_CURRENT_LIST_DIR}/display/assets)
target_compile_options(rootclock_display PRIVATE -Wall -Wextra)
target_link_libraries(rootclock_display fwog_display_bsp pico_rand)
fwog_display_app(rootclock_display
    VERSION 001
    DESCRIPTION "rootclock: Root board-game turn clock for 3-4 players, with per-turn limit and faction icons")

add_executable(rootclock_main main/main.c)
target_link_libraries(rootclock_main fwog_main_bsp)
# Embeds rootclock_display. Must come after target_link_libraries.
fwog_embed_display_image(rootclock_main)
fwog_main_app(rootclock_main
    VERSION 001
    DESCRIPTION "rootclock: main half (watchdog + display update), carries the rootclock display firmware")
