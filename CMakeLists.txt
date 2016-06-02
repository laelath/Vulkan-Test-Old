cmake_minimum_required(VERSION 3.4)
project(Vulkan-Test)

add_subdirectory(libraries/glfw-3.2)
include_directories(libraries/glfw/include libraries/linmath)

file(GLOB SOURCES src/*.c)
add_executable(vulkan-test ${SOURCES})

target_link_libraries(vulkan-test glfw ${GLFW_LIBRARIES} ${VULKAN_LIBRARY})
