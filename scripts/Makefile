CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99

TARGETS = dag_cbor_encode

all: $(TARGETS)

dag_cbor_encode: dag_cbor_encode.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

.PHONY: all clean
