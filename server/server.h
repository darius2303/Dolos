#ifndef SERVER_H
#define SERVER_H


// Includem starea partajata cu admin clientul
#include "../server_state.h"

// dimensiunea bufferului folosit pentru mesaje si logging
#define BUFFER_SIZE 1024
// numarul maxim de tokeni per fisier
#define MAX_TOKENS 65536
// dimensiunea ferestrei de shingle-uri (k-shingles) — valoare implicita
#define SHINGLE_K_DEFAULT 3
// numarul maxim de shingle-uri unice per fisier
#define MAX_SHINGLES 65536
#define LOG_DIR_SIZE 256
#define TOKEN_SIZE 64
// structura care retine configuratia serverului
typedef struct {
  int port;          // portul pe care asculta serverul
  char log_dir[LOG_DIR_SIZE]; // directorul unde se salveaza log-urile
} ServerConfig;

// datele de intrare ale fiecarui thread folosite mai departe pentru a realiza
// token-izarea
typedef struct {
  const unsigned char *data; // continutul fisierului
  int size;                  // dimensiunea fisierului
  char (*tokens)[TOKEN_SIZE];        // array de tokeni stemati
  int max_tokens;            // limita de tokeni
  int token_count;           // rezultat: cati tokeni s-au gasit
  unsigned long *shingles;   // array de shingle-uri
  int max_shingles;          // limita de shingle-uri
  int shingle_count;         // rezultat: cate shingle-uri s-au gasit
  const char *filename;      // pentru logging
  int shingle_k;             // dimensiunea ferestrei (citita din shm)
} TokenizeArgs;

// pointerul global catre starea administrativa (mapat in shm)
extern AdminState *g_admin_state;

// initializeaza / creeaza segmentul shm si il mapeaza
int server_shm_init(void);
// elibereaza segmentul shm (apelata la shutdown)
void server_shm_cleanup(void);
// incarca configuratia serverului din fisierul cfg
void config_load(const char *path, ServerConfig *cfg);
// porneste serverul pe portul si directorul de loguri configurate
void start_server(int port, const char *log_dir);
// initializeaza sistemul de logging cu un proces dedicat
void logger_init(const char *log_dir);
// scrie un mesaj in log cu timestamp
void logger_log(const char *msg);
// functie de tokenize rulata de fiecare thread pe fisierul sau
void *tokenize_thread(void *arg);

#endif