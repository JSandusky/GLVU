
set(${EXAMPLE_BASE_SRCS} main.cpp)

include_directories(${GLFW_INCLUDE_DIR})
add_executable(Example main.cpp)

target_compile_features(Example PRIVATE cxx_std_14)
add_dependencies(Example GLVU)
if (GLVU_VK)
    add_dependencies(Example VEZ)
elseif (GLVU_GL OR GLVU_GLES3)
    target_link_libraries(Example GLEW)
    add_dependencies(Example GLEW)
endif()

#add_definitions(-DMATH_SSE2)
target_link_libraries(Example GLVU ${GLFW_LIBRARIES} GLVUHelpers)
add_dependencies(Example GLVUHelpers)
include_directories(Example "../../helpers")