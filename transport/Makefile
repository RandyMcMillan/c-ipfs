CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multiaddr/include -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

# libwebsockets submodule build
LWS_BUILD_DIR = ../libwebsockets/build-c-ipfs
LWS_LIB = $(LWS_BUILD_DIR)/lib/libwebsockets.a
LWS_INC = -I$(LWS_BUILD_DIR)/include -I../libwebsockets/include

# lsquic submodule build (requires BoringSSL; OpenSSL is not fully compatible)
LSQUIC_BUILD_DIR = ../lsquic/build-c-ipfs
LSQUIC_LIB = $(LSQUIC_BUILD_DIR)/src/liblsquic/liblsquic.a
LSQUIC_INC = -I../lsquic/include

# BoringSSL submodule build
BORINGSSL_BUILD_DIR = ../boringssl/build-c-ipfs
BORINGSSL_LIB = $(BORINGSSL_BUILD_DIR)/libssl.a $(BORINGSSL_BUILD_DIR)/libcrypto.a
BORINGSSL_INC = -I../boringssl/include

ifdef HAS_LSQUIC
CFLAGS += -DHAS_LSQUIC $(LSQUIC_INC) $(BORINGSSL_INC)
LFLAGS += $(LSQUIC_LIB) $(BORINGSSL_LIB) -lz
endif

ifdef HAS_LIBWEBSOCKETS
CFLAGS += -DHAS_LIBWEBSOCKETS $(LWS_INC)
LFLAGS += $(LWS_LIB)
endif

DEPS =
OBJS = quic_transport.o ws_transport.o registry.o stream_bridge.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
