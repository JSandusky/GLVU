
project(EnkiTS)

file(GLOB_RECURSE ENKI_SOURCES *.h *.cpp *.c)
add_library(EnkiTS STATIC ${ENKI_SOURCES})