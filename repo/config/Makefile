CC = gcc
CFLAGS = -O0 -I../../include -I../../c-libp2p/include -I../../c-libp2p/c-multiaddr/include -I../../c-libp2p/c-multihash/include -I../../c-libp2p/c-protobuf -Wall -std=c99

ifdef DEBUG
CFLAGS += -g3
endif

LFLAGS = 
DEPS = config.h datastore.h identity.h replication.h
OBJS = config.o identity.o bootstrap_peers.o gateway.o addresses.o swarm.o peer.o replication.o config_impl.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
