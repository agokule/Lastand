cmake_minimum_required(VERSION 3.20)

project(Lastand LANGUAGES CXX)

option(DISTRIBUTION_MODE "Enable distribution mode (optimized, no debug info)" OFF)

if (DISTRIBUTION_MODE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(SDL_TEST_LIBRARY OFF)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(ext/enet)

# Add subdirectories for core, client, and server
add_subdirectory(Lastand-Core)
add_subdirectory(Lastand-Client)
add_subdirectory(Lastand-Server)


