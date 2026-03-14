file(GLOB_RECURSE SERVER_SOURCES "src/*.cpp" "src/*.h")

add_executable(Lastand-Server ${SERVER_SOURCES})

if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")
endif()
target_compile_definitions(Lastand-Server PRIVATE $<$<CONFIG:Debug>:DEBUG>)

# Include directories
target_include_directories(Lastand-Server PRIVATE 
    src 
    ../Lastand-Core/src
    ../ext/enet/include
)

# Link libraries
target_link_libraries(Lastand-Server PRIVATE Lastand-Core enet)

if(MSVC)
    target_link_libraries(Lastand-Server PRIVATE ws2_32 winmm)
    set(SUBSYSTEM_LINKER_OPTIONS "/SUBSYSTEM:WINDOWS")
endif()


# Link enet
target_link_directories(Lastand-Server PRIVATE ../ext/ENet/lib)

# Copy resources folder
add_custom_command(
    TARGET Lastand-Server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:Lastand-Server>
)

