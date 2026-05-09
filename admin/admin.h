#ifndef ADMIN_H
#define ADMIN_H

#include "../server_state.h"
#include <ncurses.h>

// dimensiunile panourilor tui
#define PANEL_STATUS_HEIGHT 6
#define PANEL_CONTROLS_HEIGHT 12
#define PANEL_LOG_MIN_HEIGHT 5

// culorile folosite in interfata
#define COLOR_PAIR_TITLE 1
#define COLOR_PAIR_ACTIVE 2
#define COLOR_PAIR_PAUSED 3
#define COLOR_PAIR_LABEL 4
#define COLOR_PAIR_VALUE 5
#define COLOR_PAIR_SELECTED 6
#define COLOR_PAIR_BORDER 7
#define COLOR_PAIR_LOG 8
#define COLOR_PAIR_SHUTDOWN 9

// numarul de controale editabile
#define NUM_CONTROLS 4

// structura care retine starea locala a admin clientului
typedef struct {
  AdminState *state;       // pointer catre memoria partajata
  int shm_fd;              // file descriptor al segmentului shm
  int selected;            // indicele controlului selectat curent
  char log_lines[16][128]; // ultimele mesaje de log local
  int log_count;
} AdminClient;

// initializeaza clientul admin: mapeaza shm si pregateste ncurses
int admin_init(AdminClient *client);

// deseneaza intreaga interfata
void admin_draw(AdminClient *client);

// procseaza input de la tastatura, returneaza 0 daca trebuie sa iasa
int admin_handle_input(AdminClient *client, int ch);

// adauga un mesaj in log-ul local al interfetei
void admin_log(AdminClient *client, const char *msg);

// curata resursele (ncurses + shm)
void admin_cleanup(AdminClient *client);

#endif // ADMIN_H