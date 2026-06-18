#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_MSG_LEN      2048
#define MAX_USERNAME_LEN   32
#define MAX_ROOM_LEN       32
#define MAX_TEXT_LEN     1024

#define CMD_JOIN         "JOIN"
#define CMD_MSG          "MSG"
#define CMD_CREATE       "CREATE"
#define CMD_SWITCH       "SWITCH"
#define CMD_INVITE       "INVITE"
#define CMD_LIST_ROOMS   "LIST_ROOMS"
#define CMD_LIST_USERS   "LIST_USERS"
#define CMD_QUIT         "QUIT"

#define RESP_OK          "OK"
#define RESP_ERR         "ERR"
#define RESP_MSG         "MSG"
#define RESP_NOTIFY      "NOTIFY"
#define RESP_ROOMS       "ROOMS"
#define RESP_USERS       "USERS"

#define DEFAULT_ROOM_1   "General"
#define DEFAULT_ROOM_2   "Technology"

#endif
