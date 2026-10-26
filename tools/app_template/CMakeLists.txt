# One app = a display-CPU half and a main-CPU half, declared display first.
# The main UF2 carries the display image, so template_main.uf2 is the single
# file that flashes both CPUs.
#
# VERSION is exactly three digits, bumped by hand. DESCRIPTION is what the
# FreeWili OG App Explorer shows when choosing what to flash.

add_executable(template_display display/main.c)
target_link_libraries(template_display fwog_display_bsp)
fwog_display_app(template_display
    VERSION 001
    DESCRIPTION "template: display half")

add_executable(template_main main/main.c)
target_link_libraries(template_main fwog_main_bsp)
# Embeds template_display. Must come after target_link_libraries.
fwog_embed_display_image(template_main)
fwog_main_app(template_main
    VERSION 001
    DESCRIPTION "template: main half, carries the display firmware")
