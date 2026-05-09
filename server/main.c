// punctul de intrare al serverului
#include "ns.nsmap"
#include "server.h"
#include <stdio.h>

int main(void) {
  // incarcam configuratia serverului din fisierul cfg
  ServerConfig cfg;
  config_load("config/server.cfg", &cfg);

  // afisam configuratia cu care porneste serverul
  printf("Configurare server: port = %d, log_dir = %s\n", cfg.port,
         cfg.log_dir);

  // pornim serverul
  start_server(cfg.port, cfg.log_dir);
  return 0;
}