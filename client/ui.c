#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include "ui.h"

#define COL_DEFAULT  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COL_WHITE    (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COL_GREEN    (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_CYAN     (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_YELLOW   (FOREGROUND_RED  | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_RED      (FOREGROUND_RED  | FOREGROUND_INTENSITY)
#define COL_MAGENTA  (FOREGROUND_RED  | FOREGROUND_BLUE  | FOREGROUND_INTENSITY)

static HANDLE           h;
static CRITICAL_SECTION ui_cs;

static void set_color(WORD color) { SetConsoleTextAttribute(h, color); }

void ui_init(void) {
    h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("B205 Chat Application");
    InitializeCriticalSection(&ui_cs);
}

void ui_close(void) {
    DeleteCriticalSection(&ui_cs);
}

void ui_clear_line(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int width = 80;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        width = csbi.dwSize.X - 1;
        if (width < 10) width = 10;
    }
    printf("\r%*s\r", width, "");
    fflush(stdout);
}

void ui_print_message(const char *room, const char *sender, const char *text) {
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_YELLOW);  printf("[%s] ", room);
    
    set_color(COL_GREEN);
    if (sender[0] == '<' && sender[strlen(sender) - 1] == '>') {
        printf("%s ", sender);
    } else {
        printf("<%s> ", sender);
    }
    
    set_color(COL_WHITE);   printf("%s\n", text);
    set_color(COL_DEFAULT);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}

void ui_print_notification(const char *text) {
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_CYAN);
    printf("  *** %s ***\n", text);
    set_color(COL_DEFAULT);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}

void ui_print_system(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_MAGENTA); printf("[SERVER] ");
    set_color(COL_DEFAULT);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
    va_end(args);
}

void ui_print_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_RED); printf("[ERROR]  ");
    set_color(COL_DEFAULT);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
    va_end(args);
}

void ui_print_rooms(const char *rooms_csv) {
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_CYAN);  printf("  Rooms: ");
    set_color(COL_WHITE);

    char buf[1024];
    strncpy(buf, rooms_csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");
    int   n   = 0;
    while (tok) {
        if (n++ > 0) printf(", ");
        printf("%s", tok);
        tok = strtok(NULL, ",");
    }
    printf("\n");
    set_color(COL_DEFAULT);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}

void ui_print_users(const char *users_csv, const char *room) {
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_CYAN);  printf("  Users in [%s]: ", room);
    set_color(COL_WHITE);

    char buf[1024];
    strncpy(buf, users_csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");
    int   n   = 0;
    while (tok) {
        if (n++ > 0) printf(", ");
        printf("%s", tok);
        tok = strtok(NULL, ",");
    }
    printf("\n");
    set_color(COL_DEFAULT);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}

void ui_print_help(void) {
    EnterCriticalSection(&ui_cs);
    ui_clear_line();
    set_color(COL_CYAN);
    printf("  Commands:\n");
    set_color(COL_WHITE);
    printf("  /create <room>           Create a new chat room\n");
    printf("  /switch <room>           Switch active room\n");
    printf("  /invite <room> <user>    Invite a user to a room\n");
    printf("  /rooms                   List all available rooms\n");
    printf("  /users [room]            List users in a room\n");
    printf("  /quit                    Disconnect and exit\n");
    printf("  <message>                Send to current active room\n");
    set_color(COL_DEFAULT);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}

void ui_show_prompt(const char *room, const char *username) {
    EnterCriticalSection(&ui_cs);
    set_color(COL_GREEN);
    if (username[0] == '<' && username[strlen(username) - 1] == '>') {
        printf("[%s", username);
    } else {
        printf("[<%s>", username);
    }
    set_color(COL_DEFAULT); printf("@");
    set_color(COL_YELLOW); printf("%s", room);
    set_color(COL_GREEN);  printf("]> ");
    set_color(COL_WHITE);
    fflush(stdout);
    LeaveCriticalSection(&ui_cs);
}
