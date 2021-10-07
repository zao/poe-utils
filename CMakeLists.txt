project(poe-utils CXX)
cmake_minimum_required(VERSION 3.18)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/glm)

add_library(gli INTERFACE)
target_include_directories(gli INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/gli)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/stb)

target_link_libraries(gli INTERFACE glm)

add_executable(process-image src/process_image_main.cpp src/lv_bptc.cpp src/lv_bptc.h)
target_compile_features(process-image PRIVATE cxx_std_17)
target_include_directories(process-image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dep)
target_link_libraries(process-image gli stb)