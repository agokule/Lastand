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

# Unix: Zip files in ./build/bin using zip command
[script("sh")]
[unix]
zip-build:
    echo "Zipping build/bin files on Unix..."
    mkdir -p ./build
    if [ ! -d "./build/bin" ]; then echo "Error: ./build/bin does not exist"; exit 1; fi
    if [ -f "./build.zip" ]; then rm ./build.zip; fi
    cd ./build/bin && zip -r ../../build.zip . && cd -
    echo "✅ Build zip created: ./build.zip"

# Windows: Zip files in ./build/bin using PowerShell
[script("powershell")]
[windows]
zip-build:
    echo "Zipping build/bin files on Windows..."
    if (Test-Path ./build/bin) {
        if (Test-Path ./build.zip) { Remove-Item ./build.zip }
        Compress-Archive -Path ./build/bin/* -DestinationPath ./build.zip -Force
        Write-Output "✅ Build zip created: ./build.zip"
    } else {
        Write-Error "Error: ./build/bin does not exist"
        exit 1
    }

