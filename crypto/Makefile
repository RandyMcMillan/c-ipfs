CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

ifeq ($(shell uname -s),Darwin)
CFLAGS += -I/opt/homebrew/opt/openssl/include
LFLAGS += -L/opt/homebrew/opt/openssl/lib
endif

LFLAGS += -lssl -lcrypto
DEPS = 
OBJS = verify.o verify_impl.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
