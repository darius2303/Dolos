#ifndef CLIENT_H
#define CLIENT_H

// dimensiunea bufferului folosit pentru mesaje
#define BUFFER_SIZE 1024

// structura care retine configuratia clientului
typedef struct {
  int port;       // portul serverului
  char host[256]; // adresa host-ului serverului
} ClientConfig;

// incarca configuratia clientului din fisierul cfg
void config_load(const char *path, ClientConfig *cfg);
// trimite fisierele la server si afiseaza raportul primit
void client_send_files(const char **filepaths, int count, const char *endpoint);

#endif
