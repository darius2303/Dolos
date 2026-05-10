#include "admin.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int admin_init(AdminClient *client) {
  memset(client, 0, sizeof(*client));
  client->selected = 0;

  // deschidem segmentul de memorie partajata creat de server
  // O_RDWR deoarece vom modifica campuri atomice
  client->shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
  if (client->shm_fd < 0) {
    fprintf(stderr,
            "Eroare: nu s-a putut deschide memoria partajata '%s'.\n"
            "Asigurati-va ca serverul ruleaza inainte de a porni admin-ul.\n"
            "Detalii: %s\n",
            SHM_NAME, strerror(errno));
    return -1;
  }

  // mapam segmentul in spatiul de adrese al procesului curent
  client->state = mmap(NULL, sizeof(AdminState), PROT_READ | PROT_WRITE,
                       MAP_SHARED, client->shm_fd, 0);
  if (client->state == MAP_FAILED) {
    fprintf(stderr, "Eroare mmap: %s\n", strerror(errno));
    close(client->shm_fd);
    return -1;
  }

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(200);

  start_color();
  use_default_colors();
  init_pair(COLOR_PAIR_TITLE, COLOR_BLACK, COLOR_CYAN);
  init_pair(COLOR_PAIR_ACTIVE, COLOR_GREEN, -1);
  init_pair(COLOR_PAIR_PAUSED, COLOR_RED, -1);
  init_pair(COLOR_PAIR_LABEL, COLOR_CYAN, -1);
  init_pair(COLOR_PAIR_VALUE, COLOR_WHITE, -1);
  init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, COLOR_YELLOW);
  init_pair(COLOR_PAIR_BORDER, COLOR_CYAN, -1);
  init_pair(COLOR_PAIR_LOG, COLOR_WHITE, -1);
  init_pair(COLOR_PAIR_SHUTDOWN, COLOR_WHITE, COLOR_RED);

  admin_log(client, "Admin conectat la server.");
  return 0;
}

void admin_log(AdminClient *client, const char *msg) {
  char ts[32];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(ts, sizeof(ts), "%H:%M:%S", t);

  if (client->log_count == 16) {
    for (int i = 0; i < 15; i++)
      memcpy(client->log_lines[i], client->log_lines[i + 1], 128);
    client->log_count = 15;
  }

  snprintf(client->log_lines[client->log_count], 128, "[%s] %s", ts, msg);
  client->log_count++;
}

static void draw_box(int y, int x, int h, int w, const char *title) {
  attron(COLOR_PAIR(COLOR_PAIR_BORDER));
  mvhline(y, x + 1, ACS_HLINE, w - 2);
  mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
  mvvline(y + 1, x, ACS_VLINE, h - 2);
  mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
  mvaddch(y, x, ACS_ULCORNER);
  mvaddch(y, x + w - 1, ACS_URCORNER);
  mvaddch(y + h - 1, x, ACS_LLCORNER);
  mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
  attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

  if (title && *title) {
    int tlen = (int)strlen(title) + 2;
    int tx = x + (w - tlen) / 2;
    attron(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
    mvprintw(y, tx, " %s ", title);
    attroff(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
  }
}

static void draw_control_row(int y, int x, int w, const char *label,
                             const char *value, int selected, int enabled) {
  mvhline(y, x, ' ', w);

  if (selected) {
    attron(COLOR_PAIR(COLOR_PAIR_SELECTED) | A_BOLD);
    mvprintw(y, x + 1, ">");
    attroff(COLOR_PAIR(COLOR_PAIR_SELECTED) | A_BOLD);
  } else {
    mvprintw(y, x + 1, " ");
  }

  attron(COLOR_PAIR(COLOR_PAIR_LABEL));
  mvprintw(y, x + 3, "%-20s", label);
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL));

  int cpair = enabled ? COLOR_PAIR_VALUE : COLOR_PAIR_PAUSED;
  attron(COLOR_PAIR(cpair) | (selected ? A_BOLD : 0));
  mvprintw(y, x + 24, "%s", value);
  attroff(COLOR_PAIR(cpair) | (selected ? A_BOLD : 0));
}

void admin_draw(AdminClient *client) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  erase();

  AdminState *st = client->state;

  attron(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
  mvhline(0, 0, ' ', cols);
  const char *title = " PlagDet Admin Panel ";
  mvprintw(0, (cols - (int)strlen(title)) / 2, "%s", title);
  attroff(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);

  int status_w = cols / 2;
  draw_box(1, 0, PANEL_STATUS_HEIGHT, status_w, "Stare Server");

  int paused = atomic_load(&st->paused);
  int shutdown = atomic_load(&st->shutdown);

  attron(COLOR_PAIR(COLOR_PAIR_LABEL));
  mvprintw(2, 2, "Status:       ");
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL));
  if (shutdown) {
    attron(COLOR_PAIR(COLOR_PAIR_SHUTDOWN) | A_BOLD | A_BLINK);
    mvprintw(2, 16, "OPRIRE IN CURS");
    attroff(COLOR_PAIR(COLOR_PAIR_SHUTDOWN) | A_BOLD | A_BLINK);
  } else if (paused) {
    attron(COLOR_PAIR(COLOR_PAIR_PAUSED) | A_BOLD);
    mvprintw(2, 16, "PAUZA  ");
    attroff(COLOR_PAIR(COLOR_PAIR_PAUSED) | A_BOLD);
  } else {
    attron(COLOR_PAIR(COLOR_PAIR_ACTIVE) | A_BOLD);
    mvprintw(2, 16, "ACTIV  ");
    attroff(COLOR_PAIR(COLOR_PAIR_ACTIVE) | A_BOLD);
  }

  attron(COLOR_PAIR(COLOR_PAIR_LABEL));
  mvprintw(3, 2, "Cereri totale:");
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL));
  attron(COLOR_PAIR(COLOR_PAIR_VALUE));
  mvprintw(3, 17, "%-8d", atomic_load(&st->requests_done));
  attroff(COLOR_PAIR(COLOR_PAIR_VALUE));

  attron(COLOR_PAIR(COLOR_PAIR_LABEL));
  mvprintw(4, 2, "In procesare: ");
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL));
  attron(COLOR_PAIR(COLOR_PAIR_VALUE));
  mvprintw(4, 17, "%-8d", atomic_load(&st->requests_active));
  attroff(COLOR_PAIR(COLOR_PAIR_VALUE));

  attron(COLOR_PAIR(COLOR_PAIR_LABEL));
  mvprintw(5, 2, "Msg:  ");
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL));
  attron(COLOR_PAIR(COLOR_PAIR_VALUE));
  char smsg[60];
  strncpy(smsg, (char *)st->status_msg, 59);
  smsg[59] = '\0';
  mvprintw(5, 8, "%-*s", status_w - 10, smsg);
  attroff(COLOR_PAIR(COLOR_PAIR_VALUE));

  int help_x = status_w;
  int help_w = cols - status_w;
  draw_box(1, help_x, PANEL_STATUS_HEIGHT, help_w, "Taste");

  const char *keys[] = {
      " [P]   Pauza / Reluare",
      " [S]   Oprire server",
      " [+/-] Modifica valoarea",
      " [Q]   Iesire admin",
  };
  for (int i = 0; i < 4; i++) {
    attron(COLOR_PAIR(COLOR_PAIR_LOG));
    mvprintw(2 + i, help_x + 2, "%-*s", help_w - 4, keys[i]);
    attroff(COLOR_PAIR(COLOR_PAIR_LOG));
  }

  int ctrl_y = 1 + PANEL_STATUS_HEIGHT;
  draw_box(ctrl_y, 0, PANEL_CONTROLS_HEIGHT, cols, "Controale Live");

  attron(COLOR_PAIR(COLOR_PAIR_LABEL) | A_DIM);
  mvprintw(ctrl_y + 1, 2,
           "Navigare: sus/jos. Modificare: +/-  (sau stanga/dreapta)");
  attroff(COLOR_PAIR(COLOR_PAIR_LABEL) | A_DIM);

  const char *ctrl_labels[NUM_CONTROLS] = {
      "Nivel Log",
      "Max Threads",
      "Shingle K",
      "Pauza Server",
  };
  const char *log_level_names[] = {"0 - Quiet", "1 - Normal", "2 - Verbose"};

  char ctrl_values[NUM_CONTROLS][32];
  int log_lv = atomic_load(&st->log_level);
  if (log_lv < 0)
    log_lv = 0;
  if (log_lv > 2)
    log_lv = 2;
  snprintf(ctrl_values[0], 32, "%s", log_level_names[log_lv]);
  snprintf(ctrl_values[1], 32, "%d thread-uri", atomic_load(&st->max_threads));
  snprintf(ctrl_values[2], 32, "k = %d", atomic_load(&st->shingle_k));
  snprintf(ctrl_values[3], 32, "%s", paused ? "DA  (activ)" : "NU  (inactiv)");

  for (int i = 0; i < NUM_CONTROLS; i++) {
    draw_control_row(ctrl_y + 3 + i * 2, 1, cols - 2, ctrl_labels[i],
                     ctrl_values[i], client->selected == i, !paused || i == 3);
  }

  int log_y = ctrl_y + PANEL_CONTROLS_HEIGHT;
  int log_h = rows - log_y;
  if (log_h >= PANEL_LOG_MIN_HEIGHT) {
    draw_box(log_y, 0, log_h, cols, "Log Admin");
    int visible = log_h - 2;
    int start = client->log_count - visible;
    if (start < 0)
      start = 0;
    for (int i = 0; i < visible && (start + i) < client->log_count; i++) {
      attron(COLOR_PAIR(COLOR_PAIR_LOG));
      mvprintw(log_y + 1 + i, 2, "%-*s", cols - 4,
               client->log_lines[start + i]);
      attroff(COLOR_PAIR(COLOR_PAIR_LOG));
    }
  }

  attron(COLOR_PAIR(COLOR_PAIR_TITLE));
  mvhline(rows - 1, 0, ' ', cols);
  mvprintw(rows - 1, 1,
           "PlagDet Admin  |  Q=iesire  P=pauza  S=shutdown  +/-=modifica");
  attroff(COLOR_PAIR(COLOR_PAIR_TITLE));

  refresh();
}

int admin_handle_input(AdminClient *client, int ch) {
  AdminState *st = client->state;
  char logbuf[128];

  switch (ch) {
  case 'q':
  case 'Q':
    return 0;

  case 'p':
  case 'P': {
    int was_paused = atomic_fetch_xor(&st->paused, 1);
    snprintf(logbuf, sizeof(logbuf), "Server %s.",
             was_paused ? "reluat" : "pus in pauza");
    admin_log(client, logbuf);
    break;
  }

  case 's':
  case 'S': {
    atomic_store(&st->shutdown, 1);
    admin_log(client, "Semnal de oprire trimis serverului!");
    break;
  }

  case KEY_UP:
    client->selected = (client->selected - 1 + NUM_CONTROLS) % NUM_CONTROLS;
    break;

  case KEY_DOWN:
    client->selected = (client->selected + 1) % NUM_CONTROLS;
    break;

  case '+':
  case KEY_RIGHT: {
    switch (client->selected) {
    case 0: {
      int lv = atomic_load(&st->log_level);
      if (lv < 2) {
        atomic_store(&st->log_level, lv + 1);
        snprintf(logbuf, sizeof(logbuf), "Log level -> %d", lv + 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 1: {
      int mt = atomic_load(&st->max_threads);
      if (mt < 64) {
        atomic_store(&st->max_threads, mt + 1);
        snprintf(logbuf, sizeof(logbuf), "Max threads -> %d", mt + 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 2: {
      int k = atomic_load(&st->shingle_k);
      if (k < 10) {
        atomic_store(&st->shingle_k, k + 1);
        snprintf(logbuf, sizeof(logbuf), "Shingle K -> %d", k + 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 3: {
      int was = atomic_fetch_xor(&st->paused, 1);
      snprintf(logbuf, sizeof(logbuf), "Server %s.",
               was ? "reluat" : "pus in pauza");
      admin_log(client, logbuf);
      break;
    }
    }
    break;
  }

  case '-':
  case KEY_LEFT: {
    switch (client->selected) {
    case 0: {
      int lv = atomic_load(&st->log_level);
      if (lv > 0) {
        atomic_store(&st->log_level, lv - 1);
        snprintf(logbuf, sizeof(logbuf), "Log level -> %d", lv - 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 1: {
      int mt = atomic_load(&st->max_threads);
      if (mt > 1) {
        atomic_store(&st->max_threads, mt - 1);
        snprintf(logbuf, sizeof(logbuf), "Max threads -> %d", mt - 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 2: {
      int k = atomic_load(&st->shingle_k);
      if (k > 1) {
        atomic_store(&st->shingle_k, k - 1);
        snprintf(logbuf, sizeof(logbuf), "Shingle K -> %d", k - 1);
        admin_log(client, logbuf);
      }
      break;
    }
    case 3: {
      int was = atomic_fetch_xor(&st->paused, 1);
      snprintf(logbuf, sizeof(logbuf), "Server %s.",
               was ? "reluat" : "pus in pauza");
      admin_log(client, logbuf);
      break;
    }
    }
    break;
  }

  default:
    break;
  }

  return 1;
}

void admin_cleanup(AdminClient *client) {
  endwin();
  if (client->state && client->state != MAP_FAILED)
    munmap(client->state, sizeof(AdminState));
  if (client->shm_fd >= 0)
    close(client->shm_fd);
}

int main(void) {
  AdminClient client;

  if (admin_init(&client) < 0)
    return 1;

  // bucla principala: redesenam la fiecare 200ms
  while (1) {
    admin_draw(&client);
    int ch = getch();
    if (ch != ERR) {
      if (!admin_handle_input(&client, ch))
        break;
    }
  }

  admin_cleanup(&client);
  return 0;
}