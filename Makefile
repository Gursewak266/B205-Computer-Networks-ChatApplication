CC      = gcc
CFLAGS  = -Wall -Wextra
LDFLAGS = -lws2_32

SERVER_SRCS = server/server.c server/logger.c server/room.c server/client_handler.c
CLIENT_SRCS = client/client.c client/logger.c client/ui.c

all: chat_server.exe chat_client.exe

chat_server.exe: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -Icommon -Iserver -o $@ $^ $(LDFLAGS)

chat_client.exe: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -Icommon -Iclient -o $@ $^ $(LDFLAGS)

clean:
	del chat_server.exe chat_client.exe 2>nul || true

.PHONY: all clean
