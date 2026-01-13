cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME "test")

project(${PROJECT_NAME})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

#add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
#add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.0
)
FetchContent_MakeAvailable(SDL3)

add_executable(${PROJECT_NAME} entry.cpp)

target_link_libraries(${PROJECT_NAME} PUBLIC SDL3::SDL3)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

#target_include_directories(fuel_frac_calculator PRIVATE shaders)
file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY audio DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY image DESTINATION ${CMAKE_BINARY_DIR}/)