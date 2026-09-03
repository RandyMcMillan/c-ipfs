CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -Wall -std=gnu99

ifdef DEBUG
CFLAGS += -g3
endif

ifdef HAS_LSQUIC
CFLAGS += -DHAS_LSQUIC $(shell pkg-config --cflags lsquic 2>/dev/null || echo "")
LFLAGS += $(shell pkg-config --libs lsquic 2>/dev/null || echo "-llsquic -lssl -lcrypto")
endif

ifdef HAS_LIBWEBSOCKETS
CFLAGS += -DHAS_LIBWEBSOCKETS $(shell pkg-config --cflags libwebsockets 2>/dev/null || echo "")
LFLAGS += $(shell pkg-config --libs libwebsockets 2>/dev/null || echo "-lwebsockets")
endif

DEPS =
OBJS = quic_transport.o ws_transport.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJS)

clean:
	rm -f *.o
