// includerea headerului clientului si a bibliotecilor necesare
#include "client.h"
#include "soapH.h"
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// endpoint-ul global al serverului, construit din host si port
char g_endpoint[512];

// functia care incarca configuratia clientului dintr-un fisier cfg
// si construieste endpoint-ul global
void config_load(const char *path, ClientConfig *cfg) {
  config_t lib_cfg;
  config_init(&lib_cfg);

  // setam valorile implicite pentru port si host
  cfg->port = 8080;
  strncpy(cfg->host, "localhost", sizeof(cfg->host) - 1);
  cfg->host[sizeof(cfg->host) - 1] = '\0';

  // incercam sa citim fisierul de configurare
  // daca nu reusim, afisam eroare si pastram valorile default
  if (!config_read_file(&lib_cfg, path)) {
    fprintf(stderr,
            "Eroare la configurare %s:%d - %s, folosim valorile default\n",
            config_error_file(&lib_cfg), config_error_line(&lib_cfg),
            config_error_text(&lib_cfg));
    config_destroy(&lib_cfg);
    snprintf(g_endpoint, sizeof(g_endpoint), "http://%s:%d", cfg->host,
             cfg->port);
    return;
  }

  // citim portul din configurare, daca exista
  int port = 0;
  if (config_lookup_int(&lib_cfg, "port", &port)) {
    cfg->port = port;
  }

  // citim adresa host-ului din configurare, daca exista
  const char *host = NULL;
  if (config_lookup_string(&lib_cfg, "host", &host)) {
    strncpy(cfg->host, host, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = '\0';
  }

  // eliberam resursele libconfig si construim endpoint-ul
  config_destroy(&lib_cfg);
  snprintf(g_endpoint, sizeof(g_endpoint), "http://%s:%d", cfg->host,
           cfg->port);
}

// trimite toate fisierele la server intr-un singur apel soap
// si afiseaza raportul de plagiat primit
void client_send_files(const char **filepaths, int count,
                       const char *endpoint) {
  // alocam array-ul de fisiere si bufferele pentru continut
  struct ns__FileItem *items = malloc(count * sizeof(struct ns__FileItem));
  unsigned char **bufs = malloc(count * sizeof(unsigned char *));
  int actual = 0;

  // citim fiecare fisier in memorie si il adaugam in array
  for (int i = 0; i < count; i++) {
    FILE *f = fopen(filepaths[i], "rb");
    if (!f) {
      fprintf(stderr, "[CLIENT] Nu pot deschide fisierul: %s\n", filepaths[i]);
      bufs[i] = NULL;
      continue;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    bufs[actual] = malloc(size);
    if (!bufs[actual]) {
      fprintf(stderr, "[CLIENT] Memorie insuficienta pentru: %s\n",
              filepaths[i]);
      fclose(f);
      continue;
    }
    fread(bufs[actual], 1, size, f);
    fclose(f);

    // extragem doar numele fisierului fara cale
    const char *name = strrchr(filepaths[i], '/');
    name = name ? name + 1 : filepaths[i];

    // populam structura FileItem pentru fisierul curent
    items[actual].filename = (char *)name;
    items[actual].data.__ptr = bufs[actual];
    items[actual].data.__size = (int)size;
    items[actual].data.id = NULL;
    items[actual].data.type = NULL;
    items[actual].data.options = NULL;
    actual++;
  }

  // verificam ca avem cel putin 2 fisiere valide
  if (actual < 2) {
    fprintf(stderr, "[CLIENT] Sunt necesare cel putin 2 fisiere valide.\n");
    for (int i = 0; i < actual; i++)
      free(bufs[i]);
    free(items);
    free(bufs);
    return;
  }

  // construim array-ul de fisiere pentru apelul soap
  struct ns__ArrayOfFiles files;
  files.__ptr = items;
  files.__size = actual;

  // initializam soap si facem apelul catre server
  struct soap soap;
  soap_init(&soap);
  char *report = NULL;

  int status =
      soap_call_ns__analyzeFiles(&soap, endpoint, NULL, files, &report);
  if (status == SOAP_OK) {
    // afisam raportul primit de la server
    printf("\n%s\n", report);
  } else {
    fprintf(stderr, "[CLIENT] Eroare la analiza fisierelor\n");
    soap_print_fault(&soap, stderr);
  }

  // eliberam resursele soap si memoria alocata
  soap_destroy(&soap);
  soap_end(&soap);
  soap_done(&soap);

  for (int i = 0; i < actual; i++)
    free(bufs[i]);
  free(items);
  free(bufs);
}
