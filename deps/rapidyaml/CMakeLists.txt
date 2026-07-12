
project(rapidyaml)

file(GLOB_RECURSE YAML_SOURCES *.hpp *.h *.cpp *.c)
#add_definitions(-DMATH_SSE2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND ${YAML_SOURCES})
add_library(rapidyaml STATIC ${YAML_SOURCES})