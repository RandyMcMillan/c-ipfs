CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../lmdb/libraries/liblmdb -I../c-libp2p/c-protobuf -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

LFLAGS = 
DEPS = ../include/ipfsdatastore/ds_helper.h
OBJS = ds_helper.o key_impl.o 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
