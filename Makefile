CFLAGS=-c -Wall -Wextra -Werror -Wno-error=unused-variable
CC=g++

all: server subscriber

helpers.o: helpers.cpp
	$(CC) $(CFLAGS) helpers.cpp -o helpers.o

server: server.cpp helpers.o
	$(CC) $(CFLAGS) server.cpp -o server.o
	$(CC) server.o helpers.o -o server

subscriber: subscriber.cpp helpers.o
	$(CC) $(CFLAGS) subscriber.cpp -o subscriber.o
	$(CC) subscriber.o helpers.o -o subscriber

clean:
	rm -rf server subscriber *.o