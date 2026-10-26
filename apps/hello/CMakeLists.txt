# hello -- first app: LCD text, the five buttons and the LED bar.

add_executable(hello_display display/main.c)
target_link_libraries(hello_display fwog_display_bsp)
fwog_display_app(hello_display
    VERSION 001
    DESCRIPTION "hello: press a front-panel button to paint the LCD and LED bar its colour")

add_executable(hello_main main/main.c)
target_link_libraries(hello_main fwog_main_bsp)
fwog_embed_display_image(hello_main)
fwog_main_app(hello_main
    VERSION 001
    DESCRIPTION "hello: main CPU heartbeat, carries the hello display firmware")
