#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdatomic.h>

// Numele segmentului de memorie partajata POSIX
#define SHM_NAME "/dss"

// Niveluri de log disponibile
typedef enum {
  LOG_LEVEL_QUIET = 0,  // doar erorile critice
  LOG_LEVEL_NORMAL = 1, // comportamentul implicit
  LOG_LEVEL_VERBOSE = 2 // tot (inclusiv debug)
} LogLevel;

// Starea serverului vizibila din admin panel
// Toate campurile sunt atomice pentru a evita race conditions
// intre procesul server si procesul admin client
typedef struct {
  atomic_int paused;      // 1 = serverul nu accepta cereri noi
  atomic_int log_level;   // LogLevel: 0=quiet, 1=normal, 2=verbose
  atomic_int max_threads; // limita de thread-uri paralele (1-64)
  atomic_int
      requests_done; // contor de cereri procesate (readonly pentru admin)
  atomic_int requests_active; // cereri in curs de procesare
  atomic_int shutdown;        // 1 = serverul trebuie sa se opreasca
  atomic_int shingle_k;       // dimensiunea ferestrei de shingle-uri (live)
  char status_msg[128];       // mesaj de stare scris de server (non-atomic,
                              // best-effort)
} AdminState;

#endif // SHARED_STATE_H