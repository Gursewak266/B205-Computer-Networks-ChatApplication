#ifndef ROOM_H
#define ROOM_H

#include <winsock2.h>
#include <windows.h>
#include "protocol.h"
#include "config.h"

typedef struct {
    SOCKET sock;
    char   username[MAX_USERNAME_LEN];
    char   current_room[MAX_ROOM_LEN];
    int    active;
    int    authenticated;
    HANDLE thread;
} ClientInfo;

typedef struct {
    char   name[MAX_ROOM_LEN];
    SOCKET members[MAX_ROOM_MEMBERS];
    int    member_count;
    int    active;
} Room;

extern ClientInfo        clients[MAX_CLIENTS];
extern Room              rooms[MAX_ROOMS];
extern int               room_count;
extern int               client_count;
extern CRITICAL_SECTION  room_cs;
extern CRITICAL_SECTION  client_cs;

void        rooms_init(void);
int         room_create(const char *name);
int         room_join(const char *room_name, SOCKET sock);
int         room_leave(const char *room_name, SOCKET sock);
void        room_leave_all(SOCKET sock);
void        room_broadcast(const char *room_name, const char *msg, SOCKET exclude);
int         room_exists(const char *name);
int         room_is_member(const char *room_name, SOCKET sock);
int         room_invite(const char *room_name, const char *target, SOCKET inviter_sock);
void        room_list(char *buf, size_t buf_size);
void        room_list_users(const char *room_name, char *buf, size_t buf_size);
int         username_exists(const char *username);
ClientInfo *client_find_by_sock(SOCKET sock);
ClientInfo *client_find_by_name(const char *username);
void        client_remove(SOCKET sock);
int         client_add(SOCKET sock);
void        send_to_client(SOCKET sock, const char *fmt, ...);

#endif
