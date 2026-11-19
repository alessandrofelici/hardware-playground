# One app = a display-CPU half and a main-CPU half, declared display first.
# The main UF2 carries the display image, so bottlenose_main.uf2 is the single
# file that flashes both CPUs.
#
# c6/ holds the ESP32-C6 sketch for the Bottlenose itself. It is built and
# flashed with the Arduino IDE, not by this file; see README.md.
#
# VERSION is exactly three digits, bumped by hand. DESCRIPTION is what the
# FreeWili OG App Explorer shows when choosing what to flash.

add_executable(bottlenose_display display/main.c)
target_include_directories(bottlenose_display PRIVATE ${CMAKE_CURRENT_LIST_DIR}/common)
target_link_libraries(bottlenose_display fwog_display_bsp)
fwog_display_app(bottlenose_display
    VERSION 001
    DESCRIPTION "bottlenose: Wi-Fi status from the Bottlenose ESP32-C6 Orca (network, IP, signal, NTP time, HTTP fetch) on LCD and LEDs")

add_executable(bottlenose_main main/main.c)
target_include_directories(bottlenose_main PRIVATE ${CMAKE_CURRENT_LIST_DIR}/common)
target_link_libraries(bottlenose_main fwog_main_bsp hardware_uart hardware_irq)
# Embeds bottlenose_display. Must come after target_link_libraries.
fwog_embed_display_image(bottlenose_main)
fwog_main_app(bottlenose_main
    VERSION 001
    DESCRIPTION "bottlenose: bridges the Bottlenose ESP32-C6 on breakout UART1 to the USB console and the display")
