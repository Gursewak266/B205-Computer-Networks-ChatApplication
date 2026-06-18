#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <winsock2.h>
#include <windows.h>
#include "config.h"
#include "logger.h"
#include "ui.h"
#include "protocol.h"
#include "utils.h"

static SOCKET        g_sock         = INVALID_SOCKET;
static char          g_username[MAX_USERNAME_LEN]  = {0};
static char          g_current_room[MAX_ROOM_LEN]  = {0};
static volatile int  g_running      = 1;
static HANDLE        g_recv_thread  = NULL;
static CRITICAL_SECTION g_state_cs;

static int  server_port = DEFAULT_SERVER_PORT;
static char server_ip[64] = DEFAULT_SERVER_IP;

static void state_set_username(const char *u) {
    EnterCriticalSection(&g_state_cs);
    SAFE_STRNCPY(g_username, u, MAX_USERNAME_LEN);
    LeaveCriticalSection(&g_state_cs);
}

static void state_set_room(const char *r) {
    EnterCriticalSection(&g_state_cs);
    SAFE_STRNCPY(g_current_room, r, MAX_ROOM_LEN);
    LeaveCriticalSection(&g_state_cs);
}

static void state_get_username(char *out) {
    EnterCriticalSection(&g_state_cs);
    SAFE_STRNCPY(out, g_username, MAX_USERNAME_LEN);
    LeaveCriticalSection(&g_state_cs);
}

static void state_get_room(char *out) {
    EnterCriticalSection(&g_state_cs);
    SAFE_STRNCPY(out, g_current_room, MAX_ROOM_LEN);
    LeaveCriticalSection(&g_state_cs);
}

static void load_config(void) {
    FILE *f = fopen(CLIENT_CONF_FILE, "r");
    if (!f) return;
    char line[256], key[64], val[64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "server_ip")   == 0) SAFE_STRNCPY(server_ip, val, sizeof(server_ip));
            if (strcmp(key, "server_port") == 0) server_port = atoi(val);
        }
    }
    fclose(f);
}

static int recv_line(SOCKET sock, char *buf, int max_len) {
    int  total = 0;
    char c;
    while (total < max_len - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

static void send_raw(const char *fmt, ...) {
    char buf[MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, (int)sizeof(buf) - 2, fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len >= (int)sizeof(buf) - 2) len = (int)sizeof(buf) - 3;
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    send(g_sock, buf, len + 1, 0);
}

static void process_server_message(const char *msg) {
    char cmd[32]           = {0};
    char rest[MAX_MSG_LEN] = {0};
    sscanf(msg, "%31s %2047[^\n]", cmd, rest);

    if (strcmp(cmd, RESP_MSG) == 0) {
        char room[MAX_ROOM_LEN]      = {0};
        char sender[MAX_USERNAME_LEN] = {0};
        char text[MAX_TEXT_LEN]      = {0};
        if (sscanf(rest, "%31s %31s %1023[^\n]", room, sender, text) >= 3)
            ui_print_message(room, sender, text);

    } else if (strcmp(cmd, RESP_NOTIFY) == 0) {
        ui_print_notification(rest);

    } else if (strcmp(cmd, RESP_OK) == 0) {
        char username[MAX_USERNAME_LEN] = {0};
        char room[MAX_ROOM_LEN]         = {0};

        if (sscanf(rest, "Welcome %31[^!]! You are in room '%31[^']'", username, room) == 2) {
            state_set_username(username);
            state_set_room(room);
            log_info("Registered as '%s' in room '%s'", username, room);
        } else if (sscanf(rest, "Switched to room '%31[^']'", room) == 1) {
            state_set_room(room);
        } else if (sscanf(rest, "Room '%31[^']' created and joined", room) == 1) {
            state_set_room(room);
        }
        ui_print_system("%s", rest);

    } else if (strcmp(cmd, RESP_ERR) == 0) {
        ui_print_error("%s", rest);

    } else if (strcmp(cmd, RESP_ROOMS) == 0) {
        ui_print_rooms(rest);

    } else if (strcmp(cmd, RESP_USERS) == 0) {
        char room[MAX_ROOM_LEN] = {0};
        state_get_room(room);
        ui_print_users(rest, room);

    } else {
        ui_print_system("%s", msg);
    }
}

static DWORD WINAPI recv_thread_func(LPVOID arg) {
    UNUSED(arg);
    char buf[MAX_MSG_LEN];
    while (g_running) {
        int n = recv_line(g_sock, buf, MAX_MSG_LEN);
        if (n < 0) {
            if (g_running) {
                ui_print_error("Connection to server lost.");
                g_running = 0;
            }
            break;
        }
        if (n == 0) continue;
        process_server_message(buf);

        char uname[MAX_USERNAME_LEN] = {0};
        char uroom[MAX_ROOM_LEN]     = {0};
        state_get_username(uname);
        state_get_room(uroom);
        if (uname[0] != '\0' && uroom[0] != '\0')
            ui_show_prompt(uroom, uname);
    }
    return 0;
}

static void handle_user_input(const char *raw) {
    char input[MAX_MSG_LEN];
    SAFE_STRNCPY(input, raw, MAX_MSG_LEN);
    STRIP_NEWLINE(input);
    if (STR_EMPTY(input)) return;

    char uname[MAX_USERNAME_LEN] = {0};
    char uroom[MAX_ROOM_LEN]     = {0};
    state_get_username(uname);
    state_get_room(uroom);

    if (input[0] == '/') {
        char cmd[32]            = {0};
        char args[MAX_MSG_LEN]  = {0};
        sscanf(input + 1, "%31s %2047[^\n]", cmd, args);

        for (int i = 0; cmd[i]; i++) {
            if (cmd[i] >= 'A' && cmd[i] <= 'Z') {
                cmd[i] = cmd[i] - 'A' + 'a';
            }
        }

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            send_raw(CMD_QUIT);
            g_running = 0;

        } else if (strcmp(cmd, "rooms") == 0) {
            send_raw(CMD_LIST_ROOMS);

        } else if (strcmp(cmd, "users") == 0) {
            if (args[0] != '\0') send_raw("%s %s", CMD_LIST_USERS, args);
            else                 send_raw("%s %s", CMD_LIST_USERS, uroom);

        } else if (strcmp(cmd, "create") == 0) {
            if (STR_EMPTY(args)) { ui_print_error("Usage: /create <room>"); return; }
            send_raw("%s %s", CMD_CREATE, args);

        } else if (strcmp(cmd, "switch") == 0) {
            if (STR_EMPTY(args)) { ui_print_error("Usage: /switch <room>"); return; }
            send_raw("%s %s", CMD_SWITCH, args);

        } else if (strcmp(cmd, "invite") == 0) {
            if (STR_EMPTY(args)) { ui_print_error("Usage: /invite <room> <username>"); return; }
            send_raw("%s %s", CMD_INVITE, args);

        } else if (strcmp(cmd, "help") == 0) {
            ui_print_help();

        } else {
            ui_print_error("Unknown command: /%s  (try /help)", cmd);
        }

    } else {
        if (uname[0] == '\0') {
            send_raw("%s %s", CMD_JOIN, input);
        } else if (uroom[0] == '\0') {
            ui_print_error("Not in any room. Use /rooms then /switch <room>.");
        } else {
            send_raw("%s %s %s", CMD_MSG, uroom, input);
        }
    }
}

int main(void) {
    CreateDirectoryA("logs", NULL);
    InitializeCriticalSection(&g_state_cs);
    load_config();

    if (logger_init(CLIENT_LOG_FILE) != 0)
        fprintf(stderr, "Warning: Could not open client log file.\n");

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    ui_init();

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) {
        ui_print_error("Failed to create socket: %d", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons((u_short)server_port);
    srv.sin_addr.s_addr = inet_addr(server_ip);

    ui_print_system("Connecting to %s:%d ...", server_ip, server_port);

    if (connect(g_sock, (struct sockaddr *)&srv, sizeof(srv)) == SOCKET_ERROR) {
        ui_print_error("Connection failed (%d). Is the server running?", WSAGetLastError());
        closesocket(g_sock);
        WSACleanup();
        return 1;
    }

    ui_print_system("Connected! Type your username and press Enter to join.");
    log_info("Connected to %s:%d", server_ip, server_port);

    g_recv_thread = CreateThread(NULL, 0, recv_thread_func, NULL, 0, NULL);
    if (!g_recv_thread) {
        ui_print_error("Failed to create receive thread.");
        closesocket(g_sock);
        WSACleanup();
        return 1;
    }

    char input[MAX_MSG_LEN];

    while (g_running) {
        char uname[MAX_USERNAME_LEN] = {0};
        char uroom[MAX_ROOM_LEN]     = {0};
        state_get_username(uname);
        state_get_room(uroom);

        if (uname[0] != '\0' && uroom[0] != '\0') {
            ui_show_prompt(uroom, uname);
        } else if (uname[0] == '\0') {
            HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hout, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            printf("Enter username: ");
            SetConsoleTextAttribute(hout, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            fflush(stdout);
        }

        if (fgets(input, sizeof(input), stdin) == NULL) break;
        if (!g_running) break;
        handle_user_input(input);
    }

    g_running = 0;
    log_info("Client exiting.");

    if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
    if (g_recv_thread) { WaitForSingleObject(g_recv_thread, 2000); CloseHandle(g_recv_thread); }

    ui_close();
    DeleteCriticalSection(&g_state_cs);
    logger_close();
    WSACleanup();

    printf("\nDisconnected. Goodbye!\n");
    return 0;
}
