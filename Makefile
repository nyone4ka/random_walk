CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-lncurses -lpthread

all: server client

server: server.c common.h protocol.h
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c common.h protocol.h
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f server client results.csv
