project(hjson)

file(GLOB_RECURSE HJSON_SOURCES *.hpp *.h *.cpp *.c)
#add_definitions(-DMATH_SSE2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND ${HJSON_SOURCES})
add_library(hjson STATIC ${HJSON_SOURCES})