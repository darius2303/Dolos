#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>

#include "tui.h"
#include "client.h"

#define MAX_FILES 128
#define MAX_PATH  256

char file_list[MAX_FILES][MAX_PATH];
int file_count = 0;
int selected[MAX_FILES];

void load_files()
{
    DIR *d = opendir("demo");
    struct dirent *dir;

    file_count = 0;

    if (!d) {
        strcpy(file_list[file_count++], "demo/stack_original.c");
        strcpy(file_list[file_count++], "demo/stack_palg.c");
        strcpy(file_list[file_count++], "demo/other_stack.c");
        return;
    }

    while ((dir = readdir(d)) != NULL && file_count < MAX_FILES) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }

        snprintf(file_list[file_count],
                 MAX_PATH,
                 "demo/%s",
                 dir->d_name);
        file_count++;
    }

    closedir(d);
}

void draw(int idx)
{
    clear();

    mvprintw(1, 2, "Dolos TUI Client (ncurses)");
    mvprintw(2, 2, "SPACE select | r run analyze | q quit");

    for (int i = 0; i < file_count; i++) {
        if (i == idx)
            attron(A_REVERSE);

        mvprintw(4 + i, 4, "[%c] %s",
                 selected[i] ? 'x' : ' ',
                 file_list[i]);

        if (i == idx)
            attroff(A_REVERSE);
    }

    refresh();
}

void show_report(char *report)
{
    clear();
    refresh();

    WINDOW *win = newwin(LINES - 2, COLS - 2, 1, 1);
    box(win, 0, 0);

    mvwprintw(win, 1, 2, "RESULT REPORT:");

    char *report_copy = strdup(report);
    if (report_copy) {
        int y = 3;
        char *line = strtok(report_copy, "\n");

        while (line && y < LINES - 4) {
            mvwprintw(win, y++, 2, "%.*s", COLS - 6, line);
            line = strtok(NULL, "\n");
        }
        free(report_copy);
    } else {
        mvwprintw(win, 3, 2, "Error: Out of memory displaying report.");
    }

    mvwprintw(win, LINES - 4, 2, "Press any key to return... ");
    wrefresh(win);

    wgetch(win);
    delwin(win);
}

void launch_tui(const char *endpoint)
{
    load_files();

    memset(selected, 0, sizeof(selected));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int idx = 0;
    int ch;

    while (1) {
        draw(idx);
        ch = getch();

        switch (ch) {

        case 'q':
            endwin();
            return;

        case KEY_UP:
            if (idx > 0) idx--;
            break;

        case KEY_DOWN:
            if (idx < file_count - 1) idx++;
            break;

        case ' ':
            selected[idx] = !selected[idx];
            break;

        case 'r': {
            const char *files[MAX_FILES];
            int count = 0;

            for (int i = 0; i < file_count; i++) {
                if (selected[i]) {
                    files[count++] = file_list[i];
                }
            }

            if (count < 2) {
                mvprintw(LINES - 2, 2,
                         "Select at least 2 files!");
                getch();
                break;
            }

            mvprintw(LINES - 2, 2, "Analyzing... please wait.");
            refresh();

            char *report = client_send_files(files, count, endpoint);
            if (report) {
                show_report(report);
                free(report);
            } else {
                mvprintw(LINES - 2, 2, "Error: Received empty response from client.");
                getch();
            }

        } break;
        }
    }

    endwin();
}