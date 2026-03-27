# Lastand

A very basic 2D multiplayer battle royale game made using SDL3, ENet, and Dear ImGui!

## Features

- Powerups! Including one which lets you phase through walls!
- Choosing a color
- Play with more than 10 people
- Multiplayer!
- Works on linux

## Demo (please watch in fullscreen)



https://github.com/user-attachments/assets/769dfa41-18c7-47d8-a1b8-bdd76e1a9fc7



## How to download

Navigate to the latest [release](https://github.com/agokule/Lastand/releases) and follow instructions given there.

## How to build locally and develop

You will need CMake and a C++ compiler to build 

Clone the project

```
git clone https://github.com/agokule/Lastand.git --recurse-submodules
```

Go to the project directory

```
cd Lastand
```

If you are on linux and want to distribute your binary to others, it is recommended to install [dependencies using the command for your distro from here](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md#build-dependencies)

Run the following commands to build it with release mode:

```
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build -G "Ninja"
cmake --build build --config Release
```

Alternatively, if you have the [just command runner](https://github.com/casey/just), then you can simply run the following:

```
just build
# add "release" above to build with Release mode
```

Start the server

```
cd build/bin
./Lastand-Server (if on windows, add .exe)
```

Then start 2 clients like this:

```
cd build/bin
./Lastand-Client (if on windows add .exe)
```
