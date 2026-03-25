file(GLOB_RECURSE CLIENT_SOURCES "src/*.cpp" "src/*.h")

add_executable(Lastand-Client ${CLIENT_SOURCES}
    ../ext/ImGui/imgui/imgui.cpp
    ../ext/ImGui/imgui/imgui_draw.cpp
    ../ext/ImGui/imgui/imgui_tables.cpp
    ../ext/ImGui/imgui/imgui_widgets.cpp
    ../ext/ImGui/imgui/imgui_demo.cpp
    ../ext/ImGui/imgui/backends/imgui_impl_sdl3.cpp
    ../ext/ImGui/imgui/backends/imgui_impl_sdlrenderer3.cpp
    ../ext/ImGui/imgui/misc/cpp/imgui_stdlib.cpp
)
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")
    if(WIN32 AND DISTRIBUTION_MODE)
        # disable the console window
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
    endif()
endif()
target_compile_definitions(Lastand-Client PRIVATE $<$<CONFIG:Debug>:DEBUG>)

add_subdirectory(../ext/SDL3 EXLUDE_FROM_ALL)

# Include directories
target_include_directories(Lastand-Client PRIVATE 
    src 
    ../Lastand-Core/src
    ../ext/SDL3/include
    ../ext/enet/include
    ../ext/ImGui/imgui
    ../ext/ImGui/imgui/misc/cpp
    ../ext/ImGui/imgui/backends/
)

# Link libraries
target_link_libraries(Lastand-Client PRIVATE 
    Lastand-Core 
    SDL3::SDL3
    enet
)

# Handle copying SDL3 DLLs
if(WIN32)
    add_custom_command(
        TARGET Lastand-Client POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:Lastand-Client>
        VERBATIM
    )
endif()


# Copy font folder
add_custom_command(
    TARGET Lastand-Client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:Lastand-Client>/fonts
)

