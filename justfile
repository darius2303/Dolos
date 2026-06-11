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
    ./build/Release/client -f demo/stack_original.c -f demo/stack_plag.c -f demo/other_stack.c

python-client:
    python3 python-client/client.py ../demo/stack_original.c ../demo/stack_plag.c ../demo/other_stack.c

admin:
    ./build/Release/admin

tui:
    ./build/Release/client --tui

test-alg:
	gcc -Wall -Wextra -O2 server/test-alg.c -lm -o build/test_alg
	./build/test_alg