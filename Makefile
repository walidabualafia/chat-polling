CC=clang
#CFLAGS=-O3
CFLAGS=-g -Wall -Werror -Wno-unused

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f client server *.o
	rm -fr *.dSYM
