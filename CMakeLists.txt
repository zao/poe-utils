project(poe-utils CXX)
cmake_minimum_required(VERSION 3.18)

add_subdirectory(dep/cmp_core)
target_include_directories(CMP_Core INTERFACE dep/cmp_core/source)

add_subdirectory(dep/fmt)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/glm)

add_library(gli INTERFACE)
target_include_directories(gli INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/gli)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dep/stb)

target_link_libraries(gli INTERFACE glm)

add_library(lv-bptc STATIC src/lv_bptc.cpp src/lv_bptc.h)

add_executable(process-image src/process_image_main.cpp)
target_compile_features(process-image PRIVATE cxx_std_17)
target_include_directories(process-image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dep)
target_link_libraries(process-image PRIVATE gli stb CMP_Core)

if (BUILD_TESTBEDS)
    add_executable(testbed-bptc src/testbed_bptc.cpp src/lv_bptc.cpp src/lv_bptc.h)
    target_compile_features(testbed-bptc PRIVATE cxx_std_20)
    target_link_libraries(testbed-bptc PRIVATE gli stb lv-bptc CMP_Core)
endif()