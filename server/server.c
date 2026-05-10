#include "server.h"
#include "soapStub.h"
#include "../server_state.h"

#include <ctype.h>
#include <fcntl.h>
#include <libconfig.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SIZE_64 64
#define SHM_PERMISSIONS 0666
#define DEFAULT_PORT 8080
#define MSG_SIZE 128
#define MAX_TOKEN_LENGTH 63
#define INITIAL_HASH 5381
#define DJB2_SHIFT 5
#define MAX_SHINGLE_K 10
#define REPORT_BUFFER_SIZE 65536
#define LOG_DIR_PERMISSIONS 0755
#define SOAP_BACKLOG 100
// descriptorul de fisier  pentru scrierea in pipe-ul de logging
int log_fd = -1;

// variablia glocala in care retinem configurarea server-ului (cea care poate fi
// schimbata de catre admin) modificata de admin la runtime modificarile sunt
// vizibile real-time
AdminState *g_admin_state = NULL;
int shm_fd_server = -1;

// NOLINTNEXTLINE(misc-include-cleaner)
pthread_mutex_t thread_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

// variabila conditie, permite thread-ului sa:
// - fie in sleep pana cand o conditie devine true
// - se trezeasca la momentul in care conditia devine true

// NOLINTNEXTLINE(misc-include-cleaner)
pthread_cond_t thread_limit_cond = PTHREAD_COND_INITIALIZER;

int server_shm_init(void) {
  // creeaza conexiunea dintre shared memory object si un file descritor
  // SHM_NAME -> variabila importata din server_state.h
  // O_CREAT | ORDWR -> deschidem obiectul de shared memory pentru scris si
  // citit, il creem daca nu exista
  shm_fd_server = shm_open(SHM_NAME, O_CREAT | O_RDWR, SHM_PERMISSIONS);

  // tratam cazul de eroare
  if (shm_fd_server < 0) {
    perror("shm_open");
    return -1;
  }

  // alocam memorie suficienta pentru a putea mentine o structura de tipul
  // AdminState
  if (ftruncate(shm_fd_server, sizeof(AdminState)) < 0) {
    // tratam cazul de eroare
    perror("ftruncate");
    close(shm_fd_server);
    return -1;
  }

  // facem maparea dintre obiectul de shared memory si address space-ul
  // prcocesului MAP_SHARED -> schimbarile sunt share-uite cu toate celelalte
  // procese care folosesc spatiul de memorie shared
  g_admin_state = mmap(NULL, sizeof(AdminState), PROT_READ | PROT_WRITE,
                       MAP_SHARED, shm_fd_server, 0);

  // tratam cazul de eroare
  if (g_admin_state == MAP_FAILED) {
    perror("mmap");
    close(shm_fd_server);
    return -1;
  }

  // majoritatea campurilor sunt atomice pentru acces concurent sigur intre
  // procese status_msg este best-effort
  atomic_store(&g_admin_state->paused, 0);
  atomic_store(&g_admin_state->log_level, LOG_LEVEL_NORMAL);
  atomic_store(&g_admin_state->max_threads, 8);
  atomic_store(&g_admin_state->requests_done, 0);
  atomic_store(&g_admin_state->requests_active, 0);
  atomic_store(&g_admin_state->shutdown, 0);
  atomic_store(&g_admin_state->shingle_k, SHINGLE_K_DEFAULT);

  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(g_admin_state->status_msg, sizeof(g_admin_state->status_msg),
         "%s", "Server pornit.") <0){
    perror("snprintf");
  }

  printf("[SERVER] Memorie partajata initiata: %s\n", SHM_NAME);
  return 0;
}

// functie de cleanup pentru memoria partajata
void server_shm_cleanup(void) {
  if (g_admin_state && g_admin_state != MAP_FAILED) {
    // sterge maparea din address space
    munmap(g_admin_state, sizeof(AdminState));
    g_admin_state = NULL;
  }
  if (shm_fd_server >= 0) {
    // inchide file descriptor-ul din shared memory file
    close(shm_fd_server);
    shm_fd_server = -1;
  }
  shm_unlink(SHM_NAME);
}

// funcite care seteaza mesajul de status
void shm_set_status(const char *msg) {
  if (!g_admin_state){
    return;
  }
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(g_admin_state->status_msg, sizeof(g_admin_state->status_msg),
         "%s", msg) <0){
    perror("snprintf");
  }
}

// functia care incarca configuratia serverului dintr-un fisier cfg
void config_load(const char *path, ServerConfig *cfg) {
  config_t lib_cfg;
  config_init(&lib_cfg);

  // setam valorile implicite pentru port si directorul de loguri
  cfg->port = DEFAULT_PORT;
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(cfg->log_dir, sizeof(cfg->log_dir), "%s", "logs") <0){
    perror("snprintf");
  }

  // incercam sa citim fisierul de configurare
  // daca nu reusim, afisam eroare si pastram valorile default
  if (!config_read_file(&lib_cfg, path)) {
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (fprintf(stderr,
            "Eroare la configurare %s:%d - %s, folosim valorile default\n",
            config_error_file(&lib_cfg), config_error_line(&lib_cfg),
            config_error_text(&lib_cfg)) <0){
      perror("config_read_file");
            }
    config_destroy(&lib_cfg);
    return;
  }

  // citim portul din configurare, daca exista
  int port = 0;
  if (config_lookup_int(&lib_cfg, "port", &port)) {
    cfg->port = port;
  }

  // citim directorul de loguri din configurare, daca exista
  const char *log_dir = NULL;
  if (config_lookup_string(&lib_cfg, "log_dir", &log_dir)) {
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (snprintf(cfg->log_dir, sizeof(cfg->log_dir), "%s", log_dir) <0){
      perror("snprintf");
    }
  }

  // eliberam resursele libconfig
  config_destroy(&lib_cfg);
}

static unsigned char to_lowercase(unsigned char character) {
  return (unsigned char)tolower(character);
}

// tokenizeaza si stemeaza continutul unui fisier
// returneaza numarul de tokeni gasiti
int tokenize_and_stem(const unsigned char *data, int size,
                      char stemmed_tokens[][SIZE_64], int max_tokens) {
  // pentru fisiere de cod nu aplicam stemming
  // doar extragem tokenii si ii convertim la lowercase
  int count = 0;
  char *buf = malloc(size + 1);
  memcpy(buf, data, size); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  buf[size] = '\0';

  char *cursor = buf;
  while (*cursor && count < max_tokens) {
    // sari peste caractere care nu sunt alfanumerice sau underscore
    while (*cursor && !isalnum((unsigned char)*cursor) && *cursor != '_') {
      cursor++;
    }

    if (!*cursor) {
      break;
    }

    char word[SIZE_64];
    int wlen = 0;
    while (*cursor &&
           (isalnum((unsigned char)*cursor) || *cursor == '_') &&
           wlen < MAX_TOKEN_LENGTH) {
      word[wlen++] = (char)to_lowercase((unsigned char)*cursor++);
           }
    word[wlen] = '\0';
    if (wlen == 0){
      continue;
    }


    (void) snprintf(stemmed_tokens[count], SIZE_64, "%s", word); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    count++;
  }

  free(buf);
  return count;
}

// construieste shingle-uri (ferestre de k tokeni) si le hashuieste
// returneaza numarul de shingle-uri unice
int build_shingles(char tokens[][SIZE_64], int token_count, unsigned long *shingles,
                   int max_shingles, int shingle_size) {
  int count = 0;
  for (int i = 0; i <= token_count - shingle_size && count < max_shingles; i++) {
    unsigned long hash_value = INITIAL_HASH;
    for (int j = 0; j < shingle_size; j++) {
      for (const char *character = tokens[i + j]; *character; character++) {
        hash_value = ((hash_value << DJB2_SHIFT) + hash_value) +
                     (unsigned char)*character;
      }
    }
    // sem deduplicare - pastram toate shingle-urile
    shingles[count++] = hash_value;
  }
  return count;
}

// functia executata de fiecare thread in parte.
// thread-ul main v-a creea cate un thread nou de token-izare per fisier si va
// astepta rezultatul obtinut de la fiecare inainte de computarea report-ului
void *tokenize_thread(void *arg) {
  TokenizeArgs *thread_args = (TokenizeArgs *)arg;

  if (g_admin_state) {
    // ne vom asigura ca nu depasim numarul maxim de threads pentru task ul de
    // tokenize
    pthread_mutex_lock(&thread_limit_mutex);

    // asteptam intr-un loop daca am ajuns la numarul maxim de threads
    // pthread cond wait v-a da release la mutex
    while (atomic_load(&g_admin_state->requests_active) >=
           atomic_load(&g_admin_state->max_threads)) {

      // pthread_cond_wait:
      // - eliberează mutex-ul automat
      // - pune thread-ul în sleep
      // - re-acquire mutex-ul la trezire
      pthread_cond_wait(&thread_limit_cond, &thread_limit_mutex);
    }

    // rezervam un slot pentru acest thread
    atomic_fetch_add(&g_admin_state->requests_active, 1);

    // iesim din sectiunea critica permitand altor thread uri sa verifice limita
    pthread_mutex_unlock(&thread_limit_mutex);
  }

  int shingle_size = thread_args->shingle_k;
  if (g_admin_state) {
    int live_shingle_size = atomic_load(&g_admin_state->shingle_k);
    if (live_shingle_size >= 1 && live_shingle_size <= MAX_SHINGLE_K) {
      shingle_size = live_shingle_size;
    }
  }

  thread_args->token_count =
      tokenize_and_stem(thread_args->data, thread_args->size,
                        thread_args->tokens, thread_args->max_tokens);

  thread_args->shingle_count =
      build_shingles(thread_args->tokens, thread_args->token_count,
                     thread_args->shingles, thread_args->max_shingles,
                     shingle_size);

  // citim log level si in functie de el aratam un mesaj mai detaliat
  int log_lv =
      g_admin_state ? atomic_load(&g_admin_state->log_level) : LOG_LEVEL_NORMAL;
  if (log_lv >= LOG_LEVEL_VERBOSE) {
    printf(
        "[SERVER] [thread] Fisier analizat: %s (%d tokeni, %d shingle-uri)\n",
        thread_args->filename, thread_args->token_count, thread_args->shingle_count);
  }

  // finalul executiei thread-ului:
  // eliberam un slot în limita de thread-uri active și notificam
  // thread-urile care aateapta ca exista spatiu liber.
  if (g_admin_state) {
    pthread_mutex_lock(&thread_limit_mutex);

    // reducem numarul de thread-uri active (un slot devine disponibil)
    atomic_fetch_sub(&g_admin_state->requests_active, 1);

    // trezeste thread-urile blocate în pthread_cond_wait()
    // pentru a le permite sa re-verifice conditia de executie
    pthread_cond_broadcast(&thread_limit_cond);

    pthread_mutex_unlock(&thread_limit_mutex);
  }

  return NULL;
}

// calculeaza similaritatea Jaccard intre doua seturi de shingle-uri
// rezultatul este un numar intre 0.0 (complet diferite) si 1.0 (identice)
double jaccard(const unsigned long *first_set, int first_count,
               const unsigned long *second_set, int second_count) {
  int intersect = 0;
  char *used = calloc(second_count, sizeof(char));

  for (int i = 0; i < first_count; i++) {
    for (int j = 0; j < second_count; j++) {
      if (!used[j] && first_set[i] == second_set[j]) {
        intersect++;
        used[j] = 1;
        break;
      }
    }
  }

  free(used);

  int union_count = first_count + second_count - intersect;
  if (union_count == 0) {
    return 0.0;
  }

  return (double)intersect / (double)union_count;
}

// implementarea serviciului soap analyzeFiles
// primeste un array de fisiere, le analizeaza si returneaza raportul de plagiat
// fiecare fisier este token-izat intr-un thread separat, in paralel
int ns__analyzeFiles(struct soap *soap, struct ns__ArrayOfFiles files,
                     char **report) {
  int file_count = files.__size;

  if (g_admin_state && atomic_load(&g_admin_state->paused)) {
    *report = soap_strdup(
        soap, "Serverul este in pauza. Incercati din nou mai tarziu.\n");
    return SOAP_OK;
  }

  if (g_admin_state && atomic_load(&g_admin_state->shutdown)) {
    *report = soap_strdup(soap, "Serverul se opreste. Conexiune refuzata.\n");
    return SOAP_OK;
  }

  if (file_count < 2) {
    *report = soap_strdup(
        soap, "Sunt necesare cel putin 2 fisiere pentru comparatie.\n");
    return SOAP_OK;
  }

  // alocam memoria pentru tokeni, shingle-uri, threaduri si argumente
  char (*tokens)[MAX_TOKENS][SIZE_64] = malloc(file_count * sizeof(*tokens));
  int *token_counts = malloc(file_count * sizeof(int));
  unsigned long (*shingles)[MAX_SHINGLES] = malloc(file_count * sizeof(*shingles));
  int *shingle_counts = malloc(file_count * sizeof(int));

  // avem un array de file_count thread-uri, cate unul pentru fiecare fisier
  // NOLINTNEXTLINE(misc-include-cleaner)
  pthread_t *threads = malloc(file_count * sizeof(pthread_t));
  TokenizeArgs *args = malloc(file_count * sizeof(TokenizeArgs));

  if (!tokens || !token_counts || !shingles || !shingle_counts || !threads ||
      !args) {
    free(tokens);
    free(token_counts);
    free(shingles);
    free(shingle_counts);
    free(threads);
    free(args);
    return soap_receiver_fault(soap, "Memorie insuficienta pe server", NULL);
  }

  int current_shingle_size = SHINGLE_K_DEFAULT;
  if (g_admin_state) {
    // update la k-value daca aceasta a fost configurata de admin
    int live_shingle_size = atomic_load(&g_admin_state->shingle_k);
    if (live_shingle_size >= 1 && live_shingle_size <= MAX_SHINGLE_K) {
      current_shingle_size = live_shingle_size;
    }
  }

  // pornim cate un thread pentru fiecare fisier
  for (int i = 0; i < file_count; i++) {
    args[i].data = files.__ptr[i].data.__ptr;
    args[i].size = files.__ptr[i].data.__size;
    args[i].tokens = (char (*)[SIZE_64])tokens[i];
    args[i].max_tokens = MAX_TOKENS;
    args[i].shingles = shingles[i];
    args[i].max_shingles = MAX_SHINGLES;
    args[i].filename = files.__ptr[i].filename;
    args[i].shingle_k = current_shingle_size;
    pthread_create(&threads[i], NULL, tokenize_thread, &args[i]);
  }

  // asteptam ca toate thread-urile sa termine
  for (int i = 0; i < file_count; i++) {
    pthread_join(threads[i], NULL);
    token_counts[i] = args[i].token_count;
    shingle_counts[i] = args[i].shingle_count;
  }

  // construim raportul cu similaritatea Jaccard pentru fiecare pereche
  int bufsize = REPORT_BUFFER_SIZE;
  char *buf = malloc(bufsize);
  int pos = 0;
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  pos += snprintf(buf + pos, bufsize - pos,
                  "=== Raport Detectare Plagiat ===\n"
                  "Fisiere analizate: %d\n\n",
                  file_count);

  for (int i = 0; i < file_count; i++) {
    for (int j = i + 1; j < file_count; j++) {
      double sim = jaccard(shingles[i], shingle_counts[i], shingles[j],
                           shingle_counts[j]);
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
      pos += snprintf(buf + pos, bufsize - pos, "%s <-> %s : %.1f%%\n",
                      files.__ptr[i].filename, files.__ptr[j].filename,
                      sim * SOAP_BACKLOG);
    }
  }

  if (g_admin_state) {
    atomic_fetch_add(&g_admin_state->requests_done, 1);
    char smsg[MSG_SIZE];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (snprintf(smsg, sizeof(smsg), "Ultima cerere: %d fisiere analizate", file_count) <0){
      perror("snprintf");
    }
    shm_set_status(smsg);
  }

  char logbuf[BUFFER_SIZE];
  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  if (snprintf(logbuf, sizeof(logbuf), "Raport generat pentru %d fisiere", file_count) <0){
    perror("snprintf");
  }
  logger_log(logbuf);

  *report = soap_strdup(soap, buf);
  free(buf);
  free(tokens);
  free(token_counts);
  free(shingles);
  free(shingle_counts);
  free(threads);
  free(args);

  return SOAP_OK;
}

// initializeaza sistemul de logging folosind fork si pipe
// creeaza un proces copil dedicat scrierii in fisierul de log
void logger_init(const char *log_dir) {
  // cream directorul de loguri daca nu exista
  mkdir(log_dir, LOG_DIR_PERMISSIONS);

  // cream un pipe pentru comunicarea intre procesul principal si cel de logging
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0){
    return;
  }

  // facem fork: procesul copil va scrie in fisierul de log
  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return;
  }

  if (pid == 0) {
    // procesul copil: inchidem capatul de scriere
    close(pipe_fds[1]);

    // construim numele fisierului de log pe baza datei si orei curente
    char filename[BUFFER_SIZE];


    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char time_str[SIZE_64];
    if (strftime(time_str, sizeof(time_str),
                 "%Y-%m-%d_%H-%M-%S.log", &tm_info) == 0) {
      perror("strftime");
                 }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (snprintf(filename, sizeof(filename), "%s/%s", log_dir, time_str) <0){
      perror("snprintf");
    }

    // deschidem fisierul de log pentru scriere
    FILE *log_file = fopen(filename, "w");
    if (!log_file) {
      close(pipe_fds[0]);
      _exit(1);
    }

    // citim in bucla din pipe si scriem in fisierul de log
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
      buffer[bytes_read] = '\0';
      // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
      if (fprintf(log_file, "%s\n", buffer) < 0) {
        perror("fprintf");
      }

      if (fflush(log_file) != 0) {
        perror("fflush");
      }
    }

    // inchidem fisierul si iesim din procesul copil
    if (fclose(log_file) != 0) {
      perror("fclose");
    }
    close(pipe_fds[0]);
    _exit(0);
  }

  // procesul parinte: inchidem capatul de citire si salvam cel de scriere
  close(pipe_fds[0]);
  log_fd = pipe_fds[1];
}

// scrie un mesaj in log cu timestamp-ul curent
void logger_log(const char *msg) {
  // daca pipe-ul nu e initializat, nu facem nimic
  if (log_fd < 0){
    return;
  }

  if (g_admin_state &&
      atomic_load(&g_admin_state->log_level) == LOG_LEVEL_QUIET){ return; }

  // generam timestamp-ul curent
  char timestamp[BUFFER_SIZE];
  time_t now = time(NULL);
  struct tm tm_info;
  localtime_r(&now, &tm_info);

  if (strftime(timestamp, sizeof(timestamp),
               "%Y-%m-%d %H:%M:%S", &tm_info) == 0) {
    perror("strftime");
               }

  // formatam mesajul final cu timestamp si il scriem in pipe
  char final_buf[BUFFER_SIZE];

  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  int len = snprintf(final_buf, sizeof(final_buf), "[%s] %s", timestamp, msg);
  if (len > 0) {
    ssize_t written = write(log_fd, final_buf, (size_t)len);
    (void)written;
  }
}

// functia principala care porneste serverul soap
void start_server(int port, const char *log_dir) {
  // initializam structura soap
  struct soap soap;
  soap_init(&soap);

  if (server_shm_init() < 0) {
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (fprintf(stderr, "[SERVER] Avertisment: nu s-a putut initializa shm. "
                    "Admin panel-ul nu va functiona.\n") <0){
      perror("soap_init"); }
  }

  // pornim sistemul de logging
  logger_init(log_dir);

  // facem bind pe portul configurat
  if (soap_bind(&soap, NULL, port, SOAP_BACKLOG) < 0) {
    soap_print_fault(&soap, stderr);
    server_shm_cleanup();
    return;
  }

  printf("[SERVER] Server ruleaza pe port %d...\n", port);
  printf(
      "[SERVER] Memorie partajata: %s  (porniti admin/admin pentru control)\n",
      SHM_NAME);
  logger_log("Server initializat.");
  shm_set_status("Server activ.");

  // bucla principala: asteptam si procesam cereri soap
  while (1) {
    if (g_admin_state && atomic_load(&g_admin_state->shutdown)) {
      printf("[SERVER] Semnal de shutdown primit din admin panel. Oprire...\n");
      logger_log("Server oprit prin admin panel.");
      shm_set_status("Shutdown in curs...");
      break;
    }

    if (g_admin_state && atomic_load(&g_admin_state->paused)) {
      shm_set_status("Server in pauza.");
      struct timespec pause_duration = {
        .tv_sec = 1,
        .tv_nsec = 0,
    };

      if (nanosleep(&pause_duration, NULL) < 0) {
        perror("nanosleep");
      }
      continue;
    }

    if (soap_accept(&soap) < 0){
      break;
    }

    soap_serve(&soap);
    soap_end(&soap);
  }

  // inchidem pipe-ul de logging si eliberam resursele soap
  close(log_fd);
  soap_done(&soap);
  server_shm_cleanup();
}