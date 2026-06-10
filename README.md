# Detectare Plagiat (MOSS-like)

## Tema proiect

Se trimit un număr mare de fișiere (nu neapărat de cod sursă) și se vor compara din punct de vedere al conținutului, în scopul detectării plagiatului; aplicația va fi customized pentru a funcționa în diverse scenarii de utilizare. Outputul primit va fi un report conținând toate comparațiile efectuate (vezi spre exemplu MOSS). 

## Rulare proiect

Pentru a rula proiectul cat mai usor este nevoie de utilitara `just`:

[Just Github Link](https://github.com/casey/just)

Build proiect:

```sh
just configure
just build
```

Rulare server:

```sh
just server
```

Rulare client: direct din linia de comanda

```sh
./build/Release/client -f demo/stack_original.c -f demo/stack_palg.c -f demo/other_stack.c
```

alternativ:
```sh
just client
```

Rulare client folosind TUI:

```sh
./build/Release/client --tui
```

alternativ:
```sh
just tui
```

Clientul tui va detecta automat orice adaugare de fisier in folder-ul demo si il va afisa in meniu

In cazul in care se fac schimbari se ruleaza din nou pasul de build cu:

```
just cbuild
```

Exemplu de raport:

```
=== Raport Detectare Plagiat ===
Fisiere analizate: 3

stack_original.c <-> stack_palg.c : 59.9%
stack_original.c <-> other_stack.c : 4.0%
stack_palg.c <-> other_stack.c : 1.0%
```

## Checks

### Nivel A

Falg-uri compilare:

```sh
gcc -Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L server.c -o server
```

### Nivel B

Flag-uri compilare

```sh
gcc -Wall -Wextra -Wpedantic -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread server.c -o server
```

Memcheck

```sh
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./server
```

Helgrind 

```sh
valgrind --tool=helgrind ./server
```

Address & Leak Sanitizer

```sh
gcc -g -O0 -fno-omit-frame-pointer -fsanitize=address,leak,undefined server.c -o server 
```
Thread Sanitizer

```sh
gcc -g -O0 -fno-omit-frame-pointer -fsanitize=thread -pthread server.c -o server 
```
