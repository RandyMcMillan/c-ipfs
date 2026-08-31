CC = gcc
CFLAGS = -O0 -I../include -I../nostril -I../nostril/deps/secp256k1/include -g3 -Wall -std=gnu99

OBJS = tag.o event.o git.o kind.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
