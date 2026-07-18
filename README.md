# Plagiarism Detection (MOSS-like)

## Project Topic

A large number of files, not necessarily source code files, are submitted and compared based on their content in order to detect plagiarism. The application can be customized to work in different usage scenarios.

The output is a report containing all the comparisons performed, similar to systems such as MOSS.

## Running the Project

To run the project as easily as possible, the `just` command runner is required:

[Just GitHub Link](https://github.com/casey/just)

Configure and build the project:

```sh
just configure
just build
```

Start the server:

```sh
just server
```

Run the client directly from the command line:

```sh
./build/Release/client -f demo/stack_original.c -f demo/stack_palg.c -f demo/other_stack.c
```

Alternatively:

```sh
just client
```

Run the client using the TUI:

```sh
./build/Release/client --tui
```

Alternatively:

```sh
just tui
```

The TUI client automatically detects any files added to the `demo` folder and displays them in the menu.

After making changes, rebuild the project using:

```sh
just cbuild
```

Example report:

```text
=== Plagiarism Detection Report ===
Files analyzed: 3

stack_original.c <-> stack_palg.c : 59.9%
stack_original.c <-> other_stack.c : 4.0%
stack_palg.c <-> other_stack.c : 1.0%
```

## Checks

### Level A

Compilation flags:

```sh
gcc -Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L server.c -o server
```

### Level B

Compilation flags:

```sh
gcc -Wall -Wextra -Wpedantic -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread server.c -o server
```

Memory check:

```sh
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server
```

Helgrind:

```sh
valgrind --tool=helgrind ./server
```

Address, Leak, and Undefined Behavior Sanitizers:

```sh
gcc -g -O0 -fno-omit-frame-pointer -fsanitize=address,leak,undefined server.c -o server
```

Thread Sanitizer:

```sh
gcc -g -O0 -fno-omit-frame-pointer -fsanitize=thread -pthread server.c -o server
```
