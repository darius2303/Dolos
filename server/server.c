#include "server.h"
#include "soapH.h"
#include "soapStub.h"
#include <ctype.h>
#include <libconfig.h>
#include <libstemmer.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// descriptorul de fisier  pentru scrierea in pipe-ul de logging
int log_fd = -1;

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
                   int max_shingles) {
  int count = 0;
  for (int i = 0; i <= token_count - SHINGLE_K && count < max_shingles; i++) {
    unsigned long h = 5381;
    for (int j = 0; j < SHINGLE_K; j++) {
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
  a->token_count = tokenize_and_stem(a->data, a->size, (char (*)[64])a->tokens,
                                     a->max_tokens);
  a->shingle_count = build_shingles((char (*)[64])a->tokens, a->token_count,
                                    a->shingles, a->max_shingles);
  printf("[SERVER] [thread] Fisier analizat: %s (%d tokeni, %d shingle-uri)\n",
         a->filename, a->token_count, a->shingle_count);
  return NULL;
}

// calculeaza similaritatea Jaccard intre doua seturi de shingle-uri
// rezultatul este un numar intre 0.0 (complet diferite) si 1.0 (identice)
static double jaccard(unsigned long *a, int na, unsigned long *b, int nb) {
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

  // pornim cate un thread pentru fiecare fisier
  for (int i = 0; i < n; i++) {
    args[i].data = files.__ptr[i].data.__ptr;
    args[i].size = files.__ptr[i].data.__size;
    args[i].tokens = (char (*)[64])tokens[i];
    args[i].max_tokens = MAX_TOKENS;
    args[i].shingles = shingles[i];
    args[i].max_shingles = MAX_SHINGLES;
    args[i].filename = files.__ptr[i].filename;
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

  // pornim sistemul de logging
  logger_init(log_dir);

  // facem bind pe portul configurat
  if (soap_bind(&soap, NULL, port, 100) < 0) {
    soap_print_fault(&soap, stderr);
    return;
  }

  printf("[SERVER] Server ruleaza pe port %d...\n", port);
  logger_log("Server initializat.");

  // bucla principala: asteptam si procesam cereri soap
  while (1) {
    if (soap_accept(&soap) < 0)
      break;
    soap_serve(&soap);
    soap_end(&soap);
  }

  // inchidem pipe-ul de logging si eliberam resursele soap
  close(log_fd);
  soap_done(&soap);
}
