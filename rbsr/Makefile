CC = gcc
CFLAGS = -O0 -I../include -g3 -Wall -std=gnu99

OBJS = rbsr.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
