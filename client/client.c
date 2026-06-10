// includerea headerului clientului si a bibliotecilor necesare
#include "client.h"
#include "soapH.h"
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENDPOINT_SIZE 512
#define DEFAULT_CLIENT_PORT 8080
// endpoint-ul global al serverului, construit din host si port
char g_endpoint[ENDPOINT_SIZE];

// functia care incarca configuratia clientului dintr-un fisier cfg
// si construieste endpoint-ul global
void config_load(const char *path, ClientConfig *cfg) {
  config_t lib_cfg;
  config_init(&lib_cfg);

  // setam valorile implicite pentru port si host
  cfg->port = DEFAULT_CLIENT_PORT;
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(cfg->host, sizeof(cfg->host), "%s", "localhost") < 0) {
    perror("snprintf");
  }

  // incercam sa citim fisierul de configurare
  // daca nu reusim, afisam eroare si pastram valorile default

  if (!config_read_file(&lib_cfg, path)) {
    //NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (fprintf(stderr,
                "Eroare la configurare %s:%d - %s, folosim valorile default\n",
                config_error_file(&lib_cfg), config_error_line(&lib_cfg),
                config_error_text(&lib_cfg)) < 0) {
      perror("fprintf");
                }

    config_destroy(&lib_cfg);

    //NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (snprintf(g_endpoint, sizeof(g_endpoint), "http://%s:%d", cfg->host,
                 cfg->port) < 0) {
      perror("snprintf");
                 }

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
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (snprintf(cfg->host, sizeof(cfg->host), "%s", host) < 0) {
      perror("snprintf");
    }
  }

  // eliberam resursele libconfig si construim endpoint-ul
  config_destroy(&lib_cfg);
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(g_endpoint, sizeof(g_endpoint), "http://%s:%d", cfg->host,
               cfg->port) < 0) {
    perror("snprintf");
               }
}

// trimite toate fisierele la server intr-un singur apel soap
// si afiseaza raportul de plagiat primit
char* client_send_files(const char **filepaths, int count,
                       const char *endpoint) {
  // alocam array-ul de fisiere si bufferele pentru continut
  struct ns__FileItem *items = malloc(count * sizeof(struct ns__FileItem));
  unsigned char **bufs = malloc(count * sizeof(unsigned char *));
  int actual = 0;

  // citim fiecare fisier in memorie si il adaugam in array
  for (int i = 0; i < count; i++) {
    FILE *f = fopen(filepaths[i], "rb");
    if (!f) {
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
      fprintf(stderr, "[CLIENT] Nu pot deschide fisierul: %s\n", filepaths[i]);
      bufs[i] = NULL;
      continue;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    bufs[actual] = malloc(size);
    if (!bufs[actual]) {
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
      fprintf(stderr, "[CLIENT] Memorie insuficienta pentru: %s\n",
              filepaths[i]);
      fclose(f);
      continue;
    }
    size_t bytes_read = fread(bufs[actual], 1, (size_t)size, f);
    if (bytes_read != (size_t)size) {
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
      if (fprintf(stderr, "[CLIENT] Eroare la citirea fisierului: %s\n",
                  filepaths[i]) < 0) {
        perror("fprintf");
                  }
      free(bufs[actual]);
      fclose(f);
      continue;
    }
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
    for (int i = 0; i < actual; i++){
      free(bufs[i]);
    }
    free(items);
    free(bufs);
    return strdup("[CLIENT] Sunt necesare cel putin 2 fisiere valide.");
  }

  // construim array-ul de fisiere pentru apelul soap
  struct ns__ArrayOfFiles files;
  files.__ptr = items;
  files.__size = actual;

  // initializam soap si facem apelul catre server
  struct soap soap;
  soap_init(&soap);
  char *report = NULL;

  char *final_report = NULL;
  int status =
      soap_call_ns__analyzeFiles(&soap, endpoint, NULL, files, &report);
  if (status == SOAP_OK && report) {
    final_report = strdup(report);
  } else {
    final_report = strdup("[CLIENT] Eroare la analiza fisierelor sau server inaccesibil.");
  }

  // eliberam resursele soap si memoria alocata
  soap_destroy(&soap);
  soap_end(&soap);
  soap_done(&soap);

  for (int i = 0; i < actual; i++){
    free(bufs[i]);
  }
  free(items);
  free(bufs);

  return final_report;
}
