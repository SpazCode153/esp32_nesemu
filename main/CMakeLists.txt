# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE menu_sources "../lib/menu/src/*.c")
FILE(GLOB_RECURSE nofrendo_sources "../lib/nofrendo/src/*.c")

idf_component_register(SRCS "main.c"
    ${menu_sources} 
    ${nofrendo_sources}
    EMBED_FILES "../lib/menu/data/image.jpg")
