CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -I../nostril/deps/secp256k1/include -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

ifdef HAS_LSQUIC
CFLAGS += -DHAS_LSQUIC -I../lsquic/include -I../boringssl/include
LFLAGS += ../boringssl/build-c-ipfs/libssl.a ../boringssl/build-c-ipfs/libcrypto.a -lz
ifeq ($(shell uname -s),Darwin)
LFLAGS += -lc++
else
LFLAGS += -lstdc++
endif
else
ifeq ($(shell uname -s),Darwin)
CFLAGS += -I/opt/homebrew/opt/openssl/include
LFLAGS += -L/opt/homebrew/opt/openssl/lib
endif
LFLAGS += -lssl -lcrypto
endif
DEPS = 
OBJS = verify.o verify_impl.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
