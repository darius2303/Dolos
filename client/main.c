#include "client.h"
#include "ns.nsmap"
#include <argtable2.h>
#include <stdio.h>
#include <stdlib.h>
#include "tui.h"

#define ENDPOINT_SIZE 512

int main(int argc, char *argv[]) {
  // incarcam configuratia clientului din fisierul cfg
  // config_load construieste si endpoint-ul global
  ClientConfig cfg;
  config_load("config/client.cfg", &cfg);

  // construim adresa endpoint-ului serverului
  char endpoint[ENDPOINT_SIZE];
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(endpoint, sizeof(endpoint), "http://%s:%d", cfg.host, cfg.port) < 0) {
    perror("snprintf");
    return 1;
  }

  // definim argumentele acceptate din linia de comanda
  // -f pentru fisiere, -h pentru ajutor
  struct arg_file *files =
      arg_filen("f", "files", "<file>", 0, 100, "fisiere de trimis la server");
  struct arg_lit *tui =
    arg_lit0(NULL, "tui", "launch ncurses interface");
  struct arg_lit *help = arg_lit0("h", "help", "afiseaza mesajul de ajutor");
  struct arg_end *end = arg_end(20);
  void *argtable[] = {files, help, tui, end};

  // verificam daca tabelul de argumente a fost alocat corect
  if (arg_nullcheck(argtable) != 0) {

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (fprintf(stderr, "error: insufficient memory\n") < 0) {
      perror("fprintf");
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 1;
  }

  // parsam argumentele primite din linia de comanda
  int nerrors = arg_parse(argc, argv, argtable);

  if (tui->count > 0) {
    launch_tui(endpoint);

    arg_freetable(argtable,
        sizeof(argtable) / sizeof(argtable[0]));
    return 0;
  }

  // daca utilizatorul a cerut ajutor, afisam modul de utilizare
  if (help->count > 0) {
    printf("Usage: client [OPTIONS]\n\n");
    printf("Options:\n");
    arg_print_glossary(stdout, argtable, "  %-25s %s\n");
    printf("\nExamples:\n");
    printf("  client -f file1.txt -f file2.txt   Trimite fisiere la server\n");
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
  }

  // daca sunt erori de parsare, afisam ce a fost gresit
  if (nerrors > 0) {
    arg_print_errors(stderr, end, "client");
    printf("Usage: client");
    arg_print_syntax(stdout, argtable, "\n");
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 1;
  }

  // daca au fost specificate fisiere, le trimitem la server pentru analiza
  if (files->count > 0) {
    char *report = client_send_files(files->filename, files->count, endpoint);
    if (report) {
        printf("%s\n", report);
        free(report);
    }
  } else {
    printf("Folositi -f pentru a specifica fisierele de analizat.\n");
    printf("Folositi -h pentru ajutor.\n");
  }

  // eliberam memoria alocata pentru tabelul de argumente
  arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
  return 0;
}
