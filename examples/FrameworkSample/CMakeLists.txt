
set(FRAMEWORK_SAMPLE_SRC 
    main.cpp
    FrameworkApp.h
    FrameworkApp.cpp
    GameWorld.h
    GameWorld.cpp
    GameLogic.h
    GameLogic.cpp
)

# cluster all source together, header/source splits are stupid
source_group("" FILES ${FRAMEWORK_SAMPLE_SRC})

include_directories(${GLFW_INCLUDE_DIR})
add_executable(FrameworkExample ${FRAMEWORK_SAMPLE_SRC})

target_compile_features(FrameworkExample PRIVATE cxx_std_14)
add_dependencies(FrameworkExample GLVU)

# link GLEW or VEZ based on need
if (GLVU_VK)
    add_dependencies(FrameworkExample VEZ)
elseif (GLVU_GL OR GLVU_GLES)
    target_link_libraries(FrameworkExample GLEW)
    add_dependencies(FrameworkExample GLEW)
endif()

target_link_libraries(FrameworkExample GLVU GLVUHelpers ${GLFW_LIBRARIES})
add_dependencies(FrameworkExample GLVUHelpers)
include_directories(FrameworkExample "../../helpers")