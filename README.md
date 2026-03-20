
# Lastand

A very basic 2D multiplayer battle royale game made using SDL3, ENet, and Dear ImGui

## Features

- Choosing a color
- Play with more than 10 people
- Works on linux


## Demo (please watch in fullscreen)



https://github.com/user-attachments/assets/769dfa41-18c7-47d8-a1b8-bdd76e1a9fc7




## How to run locally

You will need CMake and a C++ compiler to build 

Clone the project

```
git clone https://github.com/username121546434/Lastand.git --recurse-submodules
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

<details>
  <summary>
    In build/bin make a new file called imgui.ini and paste the following into it:
  </summary>

```
[Window][Debug##Default]
Pos=120,48
Size=400,400

[Window][Dear ImGui Demo]
Pos=27,27
Size=550,680

[Window][Dear ImGui Demo/ResizableChild_478B81A3]
IsChild=1
Size=499,136

[Window][Dear ImGui Demo/Red_BEEF922B]
IsChild=1
Size=200,100

[Window][Lastand]
Pos=60,60
Size=435,165

[Window][Enter your details]
Pos=57,61
Size=450,205

[Window][Game]
Pos=10,608
Size=278,75

[Window][Events]
Pos=292,608
Size=297,74

[Window][#1 Victory Royale]
Pos=195,3
Size=197,70
```

</details>


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
