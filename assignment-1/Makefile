# Makefile for Assignment 1

# Update following lines as needed.
# You may change this file as you wish, as long as your Makefile can generate three binaries (server, client, and client_multi).

CLIENT_HEADER = src/common.h
CLIENT_SOURCE = src/client.c

CLIENT_MULTI_HEADER = src/common.h
CLIENT_MULTI_SOURCE = src/client_multi.c

SERVER_HEADER = src/common.h
SERVER_SOURCE = src/server.c 

##############################

CC=g++
CFLAGS=-I. -g

build: client client_multi server

client: $(CLIENT_SOURCE) $(CLIENT_HEADER) bin
	$(CC) $(CFLAGS) -o bin/$@ $(CLIENT_SOURCE)

client_multi: $(CLIENT_MULTI_SOURCE) $(CLIENT_MULTI_HEADER) bin
	$(CC) $(CFLAGS) -o bin/$@ $(CLIENT_MULTI_SOURCE)

server: $(SERVER_SOURCE) $(SERVER_HEADER) bin
	$(CC) $(CFLAGS) -o bin/$@ $(SERVER_SOURCE)
    
.PHONY: clean test build

bin:
	mkdir -p bin

test: client server
	test/simple.sh

clean:
	rm -rf bin
