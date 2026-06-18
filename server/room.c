#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <winsock2.h>
#include <windows.h>
#include "room.h"
#include "logger.h"
#include "utils.h"

ClientInfo       clients[MAX_CLIENTS];
Room             rooms[MAX_ROOMS];
int              room_count  = 0;
int              client_count = 0;
CRITICAL_SECTION room_cs;
CRITICAL_SECTION client_cs;

void rooms_init(void) {
    InitializeCriticalSection(&room_cs);
    InitializeCriticalSection(&client_cs);
    memset(clients, 0, sizeof(clients));
    memset(rooms,   0, sizeof(rooms));
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].sock = INVALID_SOCKET;
    for (int i = 0; i < MAX_ROOMS; i++)
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++)
            rooms[i].members[j] = INVALID_SOCKET;
    room_create(DEFAULT_ROOM_1);
    room_create(DEFAULT_ROOM_2);
}

int room_create(const char *name) {
    int slot = -1;
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active && strcmp(rooms[i].name, name) == 0) {
            LeaveCriticalSection(&room_cs);
            return -2;
        }
        if (!rooms[i].active && slot == -1) slot = i;
    }
    if (slot == -1) { LeaveCriticalSection(&room_cs); return -1; }
    SAFE_STRNCPY(rooms[slot].name, name, MAX_ROOM_LEN);
    rooms[slot].member_count = 0;
    rooms[slot].active = 1;
    for (int j = 0; j < MAX_ROOM_MEMBERS; j++)
        rooms[slot].members[j] = INVALID_SOCKET;
    room_count++;
    LeaveCriticalSection(&room_cs);
    log_info("Room created: %s", name);
    return 0;
}

int room_join(const char *room_name, SOCKET sock) {
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || strcmp(rooms[i].name, room_name) != 0) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++)
            if (rooms[i].members[j] == sock) { LeaveCriticalSection(&room_cs); return -2; }
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            if (rooms[i].members[j] == INVALID_SOCKET) {
                rooms[i].members[j] = sock;
                rooms[i].member_count++;
                LeaveCriticalSection(&room_cs);
                return 0;
            }
        }
        LeaveCriticalSection(&room_cs);
        return -1;
    }
    LeaveCriticalSection(&room_cs);
    return -3;
}

int room_leave(const char *room_name, SOCKET sock) {
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || strcmp(rooms[i].name, room_name) != 0) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            if (rooms[i].members[j] == sock) {
                rooms[i].members[j] = INVALID_SOCKET;
                rooms[i].member_count--;
                LeaveCriticalSection(&room_cs);
                return 0;
            }
        }
        LeaveCriticalSection(&room_cs);
        return -1;
    }
    LeaveCriticalSection(&room_cs);
    return -1;
}

void room_leave_all(SOCKET sock) {
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            if (rooms[i].members[j] == sock) {
                rooms[i].members[j] = INVALID_SOCKET;
                rooms[i].member_count--;
                break;
            }
        }
    }
    LeaveCriticalSection(&room_cs);
}

void room_broadcast(const char *room_name, const char *msg, SOCKET exclude) {
    char buf[MAX_MSG_LEN];
    int  len = (int)strlen(msg);
    if (len > 0 && msg[len - 1] != '\n') {
        snprintf(buf, sizeof(buf), "%s\n", msg);
        len = (int)strlen(buf);
    } else {
        SAFE_STRNCPY(buf, msg, sizeof(buf));
    }

    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || strcmp(rooms[i].name, room_name) != 0) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++) {
            SOCKET s = rooms[i].members[j];
            if (s != INVALID_SOCKET && s != exclude)
                send(s, buf, (int)strlen(buf), 0);
        }
        break;
    }
    LeaveCriticalSection(&room_cs);
}

int room_exists(const char *name) {
    int found = 0;
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++)
        if (rooms[i].active && strcmp(rooms[i].name, name) == 0) { found = 1; break; }
    LeaveCriticalSection(&room_cs);
    return found;
}

int room_is_member(const char *room_name, SOCKET sock) {
    int found = 0;
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || strcmp(rooms[i].name, room_name) != 0) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS; j++)
            if (rooms[i].members[j] == sock) { found = 1; break; }
        break;
    }
    LeaveCriticalSection(&room_cs);
    return found;
}

int room_invite(const char *room_name, const char *target_username, SOCKET inviter_sock) {
    char   inviter_name[MAX_USERNAME_LEN] = {0};
    SOCKET target_sock = INVALID_SOCKET;

    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) continue;
        if (clients[i].sock == inviter_sock)
            SAFE_STRNCPY(inviter_name, clients[i].username, MAX_USERNAME_LEN);
        if (clients[i].authenticated && strcmp(clients[i].username, target_username) == 0)
            target_sock = clients[i].sock;
    }
    LeaveCriticalSection(&client_cs);

    if (inviter_name[0] == '\0' || target_sock == INVALID_SOCKET) return -1;
    if (!room_exists(room_name)) return -2;

    int ret = room_join(room_name, target_sock);
    if (ret == -2) return -3;
    if (ret != 0)  return -4;

    char m1[MAX_MSG_LEN];
    snprintf(m1, sizeof(m1), "NOTIFY You have been invited to room '%s' by %s", room_name, inviter_name);
    send_to_client(target_sock, "%s", m1);

    char m2[MAX_MSG_LEN];
    snprintf(m2, sizeof(m2), "NOTIFY %s has been invited to room '%s' by %s", target_username, room_name, inviter_name);
    room_broadcast(room_name, m2, target_sock);

    return 0;
}

void room_list(char *buf, size_t buf_size) {
    buf[0] = '\0';
    int first = 1;
    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active) continue;
        if (!first) strncat(buf, ",", buf_size - strlen(buf) - 1);
        strncat(buf, rooms[i].name, buf_size - strlen(buf) - 1);
        first = 0;
    }
    LeaveCriticalSection(&room_cs);
}

void room_list_users(const char *room_name, char *buf, size_t buf_size) {
    SOCKET socks[MAX_ROOM_MEMBERS];
    int    count = 0;
    buf[0] = '\0';

    EnterCriticalSection(&room_cs);
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || strcmp(rooms[i].name, room_name) != 0) continue;
        for (int j = 0; j < MAX_ROOM_MEMBERS && count < MAX_ROOM_MEMBERS; j++)
            if (rooms[i].members[j] != INVALID_SOCKET)
                socks[count++] = rooms[i].members[j];
        break;
    }
    LeaveCriticalSection(&room_cs);

    int first = 1;
    EnterCriticalSection(&client_cs);
    for (int s = 0; s < count; s++) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active || !clients[i].authenticated || clients[i].sock != socks[s]) continue;
            if (!first) strncat(buf, ",", buf_size - strlen(buf) - 1);
            strncat(buf, clients[i].username, buf_size - strlen(buf) - 1);
            first = 0;
            break;
        }
    }
    LeaveCriticalSection(&client_cs);
}

int username_exists(const char *username) {
    int found = 0;
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && clients[i].authenticated && strcmp(clients[i].username, username) == 0)
            { found = 1; break; }
    LeaveCriticalSection(&client_cs);
    return found;
}

ClientInfo *client_find_by_sock(SOCKET sock) {
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && clients[i].sock == sock)
            { LeaveCriticalSection(&client_cs); return &clients[i]; }
    LeaveCriticalSection(&client_cs);
    return NULL;
}

ClientInfo *client_find_by_name(const char *username) {
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && clients[i].authenticated && strcmp(clients[i].username, username) == 0)
            { LeaveCriticalSection(&client_cs); return &clients[i]; }
    LeaveCriticalSection(&client_cs);
    return NULL;
}

int client_add(SOCKET sock) {
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            memset(&clients[i], 0, sizeof(ClientInfo));
            clients[i].sock          = sock;
            clients[i].active        = 1;
            clients[i].authenticated = 0;
            client_count++;
            LeaveCriticalSection(&client_cs);
            return i;
        }
    }
    LeaveCriticalSection(&client_cs);
    return -1;
}

void client_remove(SOCKET sock) {
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sock == sock) {
            memset(&clients[i], 0, sizeof(ClientInfo));
            clients[i].sock = INVALID_SOCKET;
            client_count--;
            break;
        }
    }
    LeaveCriticalSection(&client_cs);
}

void send_to_client(SOCKET sock, const char *fmt, ...) {
    char buf[MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, (int)sizeof(buf) - 2, fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len >= (int)sizeof(buf) - 2) len = (int)sizeof(buf) - 3;
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    send(sock, buf, len + 1, 0);
}
