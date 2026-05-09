configure:
    conan install . --output-folder=. --build=missing
    cmake --preset conan-release
    ln -sf build/Release/compile_commands.json ./compile_commands.json

build:
    cmake --build build/Release

cbuild:
    just clear
    mkdir generated
    just configure
    just build

clear:
    rm -rf logs build generated

server:
    ./build/Release/server

client:
    ./build/Release/client

admin:
    ./build/Release/admin
