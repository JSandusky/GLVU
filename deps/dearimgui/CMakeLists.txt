
project(DearImgui)
file(GLOB_RECURSE IMGUI_SOURCES *.h *.cpp *.c)

include_directories(.)
add_library(DearImgui STATIC ${IMGUI_SOURCES})