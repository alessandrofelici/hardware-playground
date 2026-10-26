# Every folder here with a CMakeLists.txt is an app: no list to maintain.
# `fw build` reconfigures on every run, so a new folder is picked up by itself.
#
# Each app folder declares <name>_display and <name>_main (display first);
# the main UF2 carries the display image, so <name>_main.uf2 is the one file
# that flashes both CPUs.
file(GLOB _app_dirs LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_LIST_DIR}
     ${CMAKE_CURRENT_LIST_DIR}/*)
list(SORT _app_dirs)
foreach(_app IN LISTS _app_dirs)
    if(IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/${_app}
       AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/${_app}/CMakeLists.txt)
        add_subdirectory(${_app})
    endif()
endforeach()
