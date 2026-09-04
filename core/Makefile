CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/v2/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

LFLAGS = 
DEPS = builder.h ipfs_node.h
OBJS = builder.o daemon.o null.o ping.o bootstrap.o ipfs_node.o api.o client_api.o http_request.o swarm.o net_interfaces.o multipart.o api_gzip.o http_dht.o http_query.o bootstrap_impl.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
