#include "server.h"
#include "soapH.h"
#include "soapStub.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libconfig.h>
#include <libstemmer.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// descriptorul de fisier  pentru scrierea in pipe-ul de logging
int log_fd = -1;

// variablia glocala in care retinem configurarea server-ului (cea care poate fi
// schimbata de catre admin) modificata de admin la runtime modificarile sunt
// vizibile real-time
AdminState *g_admin_state = NULL;
int shm_fd_server = -1;

pthread_mutex_t thread_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

// variabila conditie, permite thread-ului sa:
// - fie in sleep pana cand o conditie devine true
// - se trezeasca la momentul in care conditia devine true
pthread_cond_t thread_limit_cond = PTHREAD_COND_INITIALIZER;

int server_shm_init(void) {
  // creeaza conexiunea dintre shared memory object si un file descritor
  // SHM_NAME -> variabila importata din server_state.h
  // O_CREAT | ORDWR -> deschidem obiectul de shared memory pentru scris si
  // citit, il creem daca nu exista
  shm_fd_server = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);

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
  strncpy(g_admin_state->status_msg, "Server pornit.", 127);
  g_admin_state->status_msg[127] = '\0';

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
  if (!g_admin_state)
    return;
  strncpy(g_admin_state->status_msg, msg, 127);
  g_admin_state->status_msg[127] = '\0';
}

// functia care incarca configuratia serverului dintr-un fisier cfg
void config_load(const char *path, ServerConfig *cfg) {
  config_t lib_cfg;
  config_init(&lib_cfg);

  // setam valorile implicite pentru port si directorul de loguri
  cfg->port = 8080;
  strncpy(cfg->log_dir, "logs", sizeof(cfg->log_dir) - 1);
  cfg->log_dir[sizeof(cfg->log_dir) - 1] = '\0';

  // incercam sa citim fisierul de configurare
  // daca nu reusim, afisam eroare si pastram valorile default
  if (!config_read_file(&lib_cfg, path)) {
    fprintf(stderr,
            "Eroare la configurare %s:%d - %s, folosim valorile default\n",
            config_error_file(&lib_cfg), config_error_line(&lib_cfg),
            config_error_text(&lib_cfg));
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
    strncpy(cfg->log_dir, log_dir, sizeof(cfg->log_dir) - 1);
    cfg->log_dir[sizeof(cfg->log_dir) - 1] = '\0';
  }

  // eliberam resursele libconfig
  config_destroy(&lib_cfg);
}

// tokenizeaza si stemeaza continutul unui fisier
// returneaza numarul de tokeni gasiti
int tokenize_and_stem(const unsigned char *data, int size,
                      char stemmed_tokens[][64], int max_tokens) {
  // pentru fisiere de cod nu aplicam stemming
  // doar extragem tokenii si ii convertim la lowercase
  int count = 0;
  char *buf = malloc(size + 1);
  memcpy(buf, data, size);
  buf[size] = '\0';

  char *p = buf;
  while (*p && count < max_tokens) {
    // sari peste caractere care nu sunt alfanumerice sau underscore
    while (*p && !isalnum((unsigned char)*p) && *p != '_')
      p++;
    if (!*p)
      break;

    char word[64];
    int wlen = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '_') && wlen < 63) {
      word[wlen++] = tolower((unsigned char)*p++);
    }
    word[wlen] = '\0';
    if (wlen == 0)
      continue;

    strncpy(stemmed_tokens[count], word, 63);
    stemmed_tokens[count][63] = '\0';
    count++;
  }

  free(buf);
  return count;
}

// construieste shingle-uri (ferestre de k tokeni) si le hashuieste
// returneaza numarul de shingle-uri unice
int build_shingles(char tokens[][64], int token_count, unsigned long *shingles,
                   int max_shingles, int k) {
  int count = 0;
  for (int i = 0; i <= token_count - k && count < max_shingles; i++) {
    unsigned long h = 5381;
    for (int j = 0; j < k; j++) {
      for (const char *c = tokens[i + j]; *c; c++) {
        h = ((h << 5) + h) + (unsigned char)*c;
      }
    }
    // sem deduplicare - pastram toate shingle-urile
    shingles[count++] = h;
  }
  return count;
}

// functia executata de fiecare thread in parte.
// thread-ul main v-a creea cate un thread nou de token-izare per fisier si va
// astepta rezultatul obtinut de la fiecare inainte de computarea report-ului
void *tokenize_thread(void *arg) {
  TokenizeArgs *a = (TokenizeArgs *)arg;

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

  int k = a->shingle_k;
  if (g_admin_state) {
    int live_k = atomic_load(&g_admin_state->shingle_k);
    if (live_k >= 1 && live_k <= 10)
      k = live_k;
  }

  a->token_count = tokenize_and_stem(a->data, a->size, (char (*)[64])a->tokens,
                                     a->max_tokens);
  a->shingle_count = build_shingles((char (*)[64])a->tokens, a->token_count,
                                    a->shingles, a->max_shingles, k);

  // citim log level si in functie de el aratam un mesaj mai detaliat
  int log_lv =
      g_admin_state ? atomic_load(&g_admin_state->log_level) : LOG_LEVEL_NORMAL;
  if (log_lv >= LOG_LEVEL_VERBOSE) {
    printf(
        "[SERVER] [thread] Fisier analizat: %s (%d tokeni, %d shingle-uri)\n",
        a->filename, a->token_count, a->shingle_count);
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
double jaccard(unsigned long *a, int na, unsigned long *b, int nb) {
  int intersect = 0;
  char *used = calloc(nb, sizeof(char));

  for (int i = 0; i < na; i++) {
    for (int j = 0; j < nb; j++) {
      if (!used[j] && a[i] == b[j]) {
        intersect++;
        used[j] = 1;
        break;
      }
    }
  }
  free(used);

  int uni = na + nb - intersect;
  if (uni == 0)
    return 0.0;
  return (double)intersect / (double)uni;
}

// implementarea serviciului soap analyzeFiles
// primeste un array de fisiere, le analizeaza si returneaza raportul de plagiat
// fiecare fisier este token-izat intr-un thread separat, in paralel
int ns__analyzeFiles(struct soap *soap, struct ns__ArrayOfFiles files,
                     char **report) {
  int n = files.__size;

  if (g_admin_state && atomic_load(&g_admin_state->paused)) {
    *report = soap_strdup(
        soap, "Serverul este in pauza. Incercati din nou mai tarziu.\n");
    return SOAP_OK;
  }

  if (g_admin_state && atomic_load(&g_admin_state->shutdown)) {
    *report = soap_strdup(soap, "Serverul se opreste. Conexiune refuzata.\n");
    return SOAP_OK;
  }

  if (n < 2) {
    *report = soap_strdup(
        soap, "Sunt necesare cel putin 2 fisiere pentru comparatie.\n");
    return SOAP_OK;
  }

  // alocam memoria pentru tokeni, shingle-uri, threaduri si argumente
  char (*tokens)[MAX_TOKENS][64] = malloc(n * sizeof(*tokens));
  int *token_counts = malloc(n * sizeof(int));
  unsigned long (*shingles)[MAX_SHINGLES] = malloc(n * sizeof(*shingles));
  int *shingle_counts = malloc(n * sizeof(int));

  // avem un array de n thread-uri, cate unul pentru fiecare fisier
  pthread_t *threads = malloc(n * sizeof(pthread_t));
  TokenizeArgs *args = malloc(n * sizeof(TokenizeArgs));

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

  int current_k = SHINGLE_K_DEFAULT;
  if (g_admin_state) {
    // update la k-value daca aceasta a fost configurata de admin
    int lk = atomic_load(&g_admin_state->shingle_k);
    if (lk >= 1 && lk <= 10)
      current_k = lk;
  }

  // pornim cate un thread pentru fiecare fisier
  for (int i = 0; i < n; i++) {
    args[i].data = files.__ptr[i].data.__ptr;
    args[i].size = files.__ptr[i].data.__size;
    args[i].tokens = (char (*)[64])tokens[i];
    args[i].max_tokens = MAX_TOKENS;
    args[i].shingles = shingles[i];
    args[i].max_shingles = MAX_SHINGLES;
    args[i].filename = files.__ptr[i].filename;
    args[i].shingle_k = current_k;
    pthread_create(&threads[i], NULL, tokenize_thread, &args[i]);
  }

  // asteptam ca toate thread-urile sa termine
  for (int i = 0; i < n; i++) {
    pthread_join(threads[i], NULL);
    token_counts[i] = args[i].token_count;
    shingle_counts[i] = args[i].shingle_count;
  }

  // construim raportul cu similaritatea Jaccard pentru fiecare pereche
  int bufsize = 65536;
  char *buf = malloc(bufsize);
  int pos = 0;
  pos += snprintf(buf + pos, bufsize - pos,
                  "=== Raport Detectare Plagiat ===\n"
                  "Fisiere analizate: %d\n\n",
                  n);

  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      double sim = jaccard(shingles[i], shingle_counts[i], shingles[j],
                           shingle_counts[j]);
      pos += snprintf(buf + pos, bufsize - pos, "%s <-> %s : %.1f%%\n",
                      files.__ptr[i].filename, files.__ptr[j].filename,
                      sim * 100.0);
    }
  }

  if (g_admin_state) {
    atomic_fetch_add(&g_admin_state->requests_done, 1);
    char smsg[128];
    snprintf(smsg, sizeof(smsg), "Ultima cerere: %d fisiere analizate", n);
    shm_set_status(smsg);
  }

  char logbuf[BUFFER_SIZE];
  snprintf(logbuf, sizeof(logbuf), "Raport generat pentru %d fisiere", n);
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
  mkdir(log_dir, 0755);

  // cream un pipe pentru comunicarea intre procesul principal si cel de logging
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0)
    return;

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
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d_%H-%M-%S.log", tm_info);
    snprintf(filename, sizeof(filename), "%s/%s", log_dir, time_str);

    // deschidem fisierul de log pentru scriere
    FILE *f = fopen(filename, "w");
    if (!f) {
      close(pipe_fds[0]);
      _exit(1);
    }

    // citim in bucla din pipe si scriem in fisierul de log
    char buffer[BUFFER_SIZE];
    ssize_t n = 0;
    while ((n = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      fprintf(f, "%s\n", buffer);
      fflush(f);
    }

    // inchidem fisierul si iesim din procesul copil
    fclose(f);
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
  if (log_fd < 0)
    return;

  if (g_admin_state &&
      atomic_load(&g_admin_state->log_level) == LOG_LEVEL_QUIET)
    return;

  // generam timestamp-ul curent
  char timestamp[BUFFER_SIZE];
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  // formatam mesajul final cu timestamp si il scriem in pipe
  char final_buf[BUFFER_SIZE];
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
    fprintf(stderr, "[SERVER] Avertisment: nu s-a putut initializa shm. "
                    "Admin panel-ul nu va functiona.\n");
  }

  // pornim sistemul de logging
  logger_init(log_dir);

  // facem bind pe portul configurat
  if (soap_bind(&soap, NULL, port, 100) < 0) {
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
      sleep(1);
      continue;
    }

    if (soap_accept(&soap) < 0)
      break;
    soap_serve(&soap);
    soap_end(&soap);
  }

  // inchidem pipe-ul de logging si eliberam resursele soap
  close(log_fd);
  soap_done(&soap);
  server_shm_cleanup();
}