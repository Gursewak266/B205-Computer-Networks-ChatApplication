#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include "config.h"
#include "logger.h"
#include "room.h"
#include "client_handler.h"
#include "protocol.h"

static int server_port  = DEFAULT_PORT;
static int max_clients  = MAX_CLIENTS;

static void load_config(void) {
    FILE *f = fopen(SERVER_CONF_FILE, "r");
    if (!f) return;
    char line[256], key[64], val[64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "port")        == 0) server_port = atoi(val);
            if (strcmp(key, "max_clients") == 0) max_clients = atoi(val);
        }
    }
    fclose(f);
}

int main(void) {
    CreateDirectoryA("logs", NULL);
    load_config();

    if (logger_init(SERVER_LOG_FILE) != 0)
        fprintf(stderr, "Warning: Could not open log file.\n");

    log_info("ChatServer starting on port %d", server_port);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_error("WSAStartup failed: %d", WSAGetLastError());
        return 1;
    }

    rooms_init();
    log_info("Default rooms created: %s, %s", DEFAULT_ROOM_1, DEFAULT_ROOM_2);

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        log_error("socket() failed: %d", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)server_port);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        log_error("bind() failed: %d", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    if (listen(server_sock, BACKLOG) == SOCKET_ERROR) {
        log_error("listen() failed: %d", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    log_info("Server ready. Listening on port %d.", server_port);
    printf("\n");
    printf("============================================\n");
    printf("  B205 ChatServer  |  Port: %d\n", server_port);
    printf("  Max clients: %d  |  Rooms: %s, %s\n", max_clients, DEFAULT_ROOM_1, DEFAULT_ROOM_2);
    printf("  Press Ctrl+C to stop.\n");
    printf("============================================\n\n");

    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);

    while (1) {
        SOCKET client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) {
            log_error("accept() failed: %d", WSAGetLastError());
            continue;
        }

        char *client_ip = inet_ntoa(client_addr.sin_addr);
        log_info("New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));

        if (client_count >= max_clients) {
            log_warn("Server full, rejecting %s", client_ip);
            const char *msg = "ERR Server is full\n";
            send(client_sock, msg, (int)strlen(msg), 0);
            closesocket(client_sock);
            continue;
        }

        if (client_add(client_sock) < 0) {
            const char *msg = "ERR Server error\n";
            send(client_sock, msg, (int)strlen(msg), 0);
            closesocket(client_sock);
            continue;
        }

        LPVOID sock_arg = (LPVOID)(uintptr_t)client_sock;
        HANDLE thread = CreateThread(NULL, 0, client_handler, sock_arg, 0, NULL);
        if (!thread) {
            log_error("CreateThread failed for socket %d", (int)client_sock);
            client_remove(client_sock);
            closesocket(client_sock);
            continue;
        }

        EnterCriticalSection(&client_cs);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock == client_sock) { clients[i].thread = thread; break; }
        }
        LeaveCriticalSection(&client_cs);

        CloseHandle(thread);
    }

    closesocket(server_sock);
    logger_close();
    WSACleanup();
    return 0;
}
