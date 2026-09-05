CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/v2/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -Wall -std=gnu99

ifeq ($(DEBUG),true)
CFLAGS += -g3
endif

OBJS = ffi.o

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
