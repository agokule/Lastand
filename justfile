set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

# Build the game with either Release or Debug mode
[arg('type', pattern='(?i)Release|Debug')]
build type='Debug':
    cmake -DCMAKE_BUILD_TYPE={{type}} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build
    cmake --build build --config {{type}}


recurse_delete := if os_family() == "windows" { "Remove-Item -Recurse -Force" } else { "rm -rf " }

# Delete the build folder
clean:
    {{ recurse_delete }} ./build/


