#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include "client_handler.h"
#include "room.h"
#include "logger.h"
#include "protocol.h"
#include "utils.h"

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

static void strip_brackets(char *str) {
    size_t len = strlen(str);
    if (len >= 2 && str[0] == '<' && str[len - 1] == '>') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

static void handle_join(SOCKET sock, char *args) {
    char username[MAX_USERNAME_LEN] = {0};
    if (sscanf(args, "%31s", username) != 1 || STR_EMPTY(username)) {
        send_to_client(sock, "ERR Invalid username");
        return;
    }
    strip_brackets(username);
    if (STR_EMPTY(username)) {
        send_to_client(sock, "ERR Invalid username");
        return;
    }

    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci) { send_to_client(sock, "ERR Internal error"); return; }

    if (ci->authenticated) {
        send_to_client(sock, "ERR Already registered as %s", ci->username);
        return;
    }
    if (username_exists(username)) {
        send_to_client(sock, "ERR Username '%s' is already taken", username);
        return;
    }

    EnterCriticalSection(&client_cs);
    SAFE_STRNCPY(ci->username, username, MAX_USERNAME_LEN);
    ci->authenticated = 1;
    SAFE_STRNCPY(ci->current_room, DEFAULT_ROOM_1, MAX_ROOM_LEN);
    LeaveCriticalSection(&client_cs);

    room_join(DEFAULT_ROOM_1, sock);

    send_to_client(sock, "OK Welcome %s! You are in room '%s'", username, DEFAULT_ROOM_1);
    log_info("User '%s' registered on socket %d", username, (int)sock);

    char notify[MAX_MSG_LEN];
    snprintf(notify, sizeof(notify), "NOTIFY %s has joined the server", username);
    room_broadcast(DEFAULT_ROOM_1, notify, sock);
}

static void handle_msg(SOCKET sock, char *args) {
    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci || !ci->authenticated) {
        send_to_client(sock, "ERR Not registered. Use JOIN <username>");
        return;
    }

    char room[MAX_ROOM_LEN]   = {0};
    char text[MAX_TEXT_LEN]   = {0};
    if (sscanf(args, "%31s %1023[^\n]", room, text) < 2 || STR_EMPTY(text)) {
        send_to_client(sock, "ERR Usage: MSG <room> <message>");
        return;
    }
    if (!room_is_member(room, sock)) {
        send_to_client(sock, "ERR You are not in room '%s'", room);
        return;
    }

    char broadcast[MAX_MSG_LEN];
    snprintf(broadcast, sizeof(broadcast), "MSG %s %s %s", room, ci->username, text);
    room_broadcast(room, broadcast, INVALID_SOCKET);
    log_info("[%s] %s: %s", room, ci->username, text);
}

static void handle_create(SOCKET sock, char *args) {
    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci || !ci->authenticated) { send_to_client(sock, "ERR Not registered"); return; }

    char room[MAX_ROOM_LEN] = {0};
    if (sscanf(args, "%31s", room) != 1 || STR_EMPTY(room)) {
        send_to_client(sock, "ERR Usage: CREATE <room>");
        return;
    }

    int ret = room_create(room);
    if (ret == -2) { send_to_client(sock, "ERR Room '%s' already exists", room); return; }
    if (ret != 0)  { send_to_client(sock, "ERR Server room limit reached"); return; }

    room_join(room, sock);

    EnterCriticalSection(&client_cs);
    SAFE_STRNCPY(ci->current_room, room, MAX_ROOM_LEN);
    LeaveCriticalSection(&client_cs);

    send_to_client(sock, "OK Room '%s' created and joined", room);
    log_info("User '%s' created room '%s'", ci->username, room);
}

static void handle_switch(SOCKET sock, char *args) {
    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci || !ci->authenticated) { send_to_client(sock, "ERR Not registered"); return; }

    char room[MAX_ROOM_LEN] = {0};
    if (sscanf(args, "%31s", room) != 1 || STR_EMPTY(room)) {
        send_to_client(sock, "ERR Usage: SWITCH <room>");
        return;
    }
    if (!room_exists(room)) {
        send_to_client(sock, "ERR Room '%s' does not exist", room);
        return;
    }

    if (!room_is_member(room, sock)) {
        room_join(room, sock);
        char notify[MAX_MSG_LEN];
        snprintf(notify, sizeof(notify), "NOTIFY %s has joined the room", ci->username);
        room_broadcast(room, notify, sock);
    }

    EnterCriticalSection(&client_cs);
    SAFE_STRNCPY(ci->current_room, room, MAX_ROOM_LEN);
    LeaveCriticalSection(&client_cs);

    send_to_client(sock, "OK Switched to room '%s'", room);
    log_info("User '%s' switched to room '%s'", ci->username, room);
}

static void handle_invite(SOCKET sock, char *args) {
    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci || !ci->authenticated) { send_to_client(sock, "ERR Not registered"); return; }

    char room[MAX_ROOM_LEN]     = {0};
    char target[MAX_USERNAME_LEN] = {0};
    if (sscanf(args, "%31s %31s", room, target) != 2) {
        send_to_client(sock, "ERR Usage: INVITE <room> <username>");
        return;
    }
    strip_brackets(room);
    strip_brackets(target);

    int ret = room_invite(room, target, sock);
    if (ret == -1) { send_to_client(sock, "ERR User '%s' not found or not connected", target); return; }
    if (ret == -2) { send_to_client(sock, "ERR Room '%s' does not exist", room); return; }
    if (ret == -3) { send_to_client(sock, "ERR User '%s' is already in room '%s'", target, room); return; }
    if (ret != 0)  { send_to_client(sock, "ERR Failed to invite user"); return; }

    send_to_client(sock, "OK User '%s' invited to room '%s'", target, room);
    log_info("User '%s' invited '%s' to room '%s'", ci->username, target, room);
}

static void handle_list_rooms(SOCKET sock) {
    char buf[MAX_MSG_LEN] = {0};
    room_list(buf, sizeof(buf));
    send_to_client(sock, "ROOMS %s", buf);
}

static void handle_list_users(SOCKET sock, char *args) {
    ClientInfo *ci = client_find_by_sock(sock);
    if (!ci || !ci->authenticated) { send_to_client(sock, "ERR Not registered"); return; }

    char room[MAX_ROOM_LEN] = {0};
    if (sscanf(args, "%31s", room) != 1 || STR_EMPTY(room)) {
        EnterCriticalSection(&client_cs);
        SAFE_STRNCPY(room, ci->current_room, MAX_ROOM_LEN);
        LeaveCriticalSection(&client_cs);
    }
    if (!room_exists(room)) {
        send_to_client(sock, "ERR Room '%s' does not exist", room);
        return;
    }

    char buf[MAX_MSG_LEN] = {0};
    room_list_users(room, buf, sizeof(buf));
    send_to_client(sock, "USERS %s", buf);
}

DWORD WINAPI client_handler(LPVOID arg) {
    SOCKET sock = (SOCKET)(uintptr_t)arg;
    char   buf[MAX_MSG_LEN];

    log_info("Client connected on socket %d", (int)sock);
    send_to_client(sock, "OK Connected to ChatServer. Use JOIN <username> to register.");

    while (recv_line(sock, buf, MAX_MSG_LEN) >= 0) {
        STRIP_NEWLINE(buf);
        if (buf[0] == '\0') continue;

        char cmd[32]          = {0};
        char args[MAX_MSG_LEN] = {0};
        sscanf(buf, "%31s %2047[^\n]", cmd, args);

        for (int i = 0; cmd[i]; i++) {
            if (cmd[i] >= 'a' && cmd[i] <= 'z') {
                cmd[i] = cmd[i] - 'a' + 'A';
            }
        }

        if      (strcmp(cmd, CMD_JOIN)       == 0) handle_join(sock, args);
        else if (strcmp(cmd, CMD_MSG)        == 0) handle_msg(sock, args);
        else if (strcmp(cmd, CMD_CREATE)     == 0) handle_create(sock, args);
        else if (strcmp(cmd, CMD_SWITCH)     == 0) handle_switch(sock, args);
        else if (strcmp(cmd, CMD_INVITE)     == 0) handle_invite(sock, args);
        else if (strcmp(cmd, CMD_LIST_ROOMS) == 0) handle_list_rooms(sock);
        else if (strcmp(cmd, CMD_LIST_USERS) == 0) handle_list_users(sock, args);
        else if (strcmp(cmd, CMD_QUIT)       == 0) break;
        else send_to_client(sock, "ERR Unknown command '%s'. Try LIST_ROOMS or JOIN.", cmd);
    }

    char username[MAX_USERNAME_LEN]  = {0};
    char user_rooms[MAX_ROOMS][MAX_ROOM_LEN];
    int  user_room_count = 0;

    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sock == sock) {
            SAFE_STRNCPY(username, clients[i].username, MAX_USERNAME_LEN);
            break;
        }
    }
    LeaveCriticalSection(&client_cs);

    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS && user_room_count < MAX_ROOMS; i++) {
        if (!rooms[i].active) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            if (rooms[i].members[j] == sock) {
                SAFE_STRNCPY(user_rooms[user_room_count++], rooms[i].name, MAX_ROOM_LEN);
                break;
            }
        }
    }
    LeaveCriticalSection(&room_cs);

    if (username[0] != '\0') {
        char notify[MAX_MSG_LEN];
        snprintf(notify, sizeof(notify), "NOTIFY %s has left the chat", username);
        for (int i = 0; i < user_room_count; i++)
            room_broadcast(user_rooms[i], notify, sock);
    }

    room_leave_all(sock);
    client_remove(sock);
    closesocket(sock);
    log_info("Client disconnected: socket %d, user: %s", (int)sock, username[0] ? username : "(unregistered)");
    return 0;
}
