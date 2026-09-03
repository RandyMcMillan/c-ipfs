CC = gcc
CFLAGS = -O0 -I../../include -I../../c-libp2p/include -I../../c-libp2p/c-multiaddr/include -I../../c-libp2p/c-multihash/include -I../../c-libp2p/c-protobuf -Wall -std=c99

ifdef DEBUG
CFLAGS += -g3
endif

LFLAGS = 
DEPS = 
OBJS = bitswap.o message.o network.o peer_request_queue.o want_manager.o wantlist_queue.o engine.o future.o want_manager_impl.o wantlist_queue_impl.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
