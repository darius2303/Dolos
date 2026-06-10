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
    ./build/Release/client -f demo/stack_original.c -f demo/stack_palg.c -f demo/other_stack.c

python-client:
    python3 python-client/client.py ../demo/stack_original.c ../demo/stack_palg.c ../demo/other_stack.c

admin:
    ./build/Release/admin
