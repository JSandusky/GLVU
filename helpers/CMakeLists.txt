
project(GLVUHelpers)
file(GLOB_RECURSE HELPER_SOURCES *.h *.cpp *.c)

add_library(GLVUHelpers STATIC ${HELPER_SOURCES})

include_directories(${GLFW_INCLUDE_DIR})

target_compile_features(GLVUHelpers PRIVATE cxx_std_14)
if (GLVU_VK)
    add_dependencies(GLVUHelpers VEZ)
elseif (GLVU_GL OR GLVU_GLES3)
    target_link_libraries(GLVUHelpers GLEW)
    add_dependencies(GLVUHelpers GLEW)
endif()

target_link_libraries(GLVUHelpers GLVU DearImgui ${GLFW_LIBRARIES})
add_dependencies(GLVUHelpers GLVU DearImgui ${GLFW_LIBRARIES})