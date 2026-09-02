CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -I../nostril -I../nostril/deps/secp256k1/include -g3 -Wall -std=gnu99
LFLAGS = -L../c-libp2p -L../c-libp2p/c-multihash -L../c-libp2p/c-multiaddr -lp2p -lm -lmultihash -lmultiaddr -lpthread -lresolv -lcurl
SECP256K1_AR = ../nostril/libsecp256k1.a
$(SECP256K1_AR):
	$(MAKE) -C ../nostril libsecp256k1.a

ifeq ($(shell uname -s),Darwin)
CFLAGS += -I/opt/homebrew/opt/openssl/include
LFLAGS += -Wl,-rpath,@loader_path/../c-libp2p -L/opt/homebrew/opt/openssl/lib
else
LFLAGS += -Wl,-rpath,'$$ORIGIN/../c-libp2p'
endif
LFLAGS += -lssl -lcrypto
DEPS = cmd/ipfs/test_init.h repo/test_repo_bootstrap_peers.h repo/test_repo_config.h repo/test_repo_identity.h cid/test_cid.h
OBJS = main.o \
	../blocks/block.o ../blocks/blockstore.o \
	../cid/cid.o ../cid/set.o \
	../cmd/ipfs/init.o \
	../cmd/ipfs/id.o \
	../cmd/ipfs/get.o \
	../cmd/ipfs/nostr.o \
	../cmd/*.o \
	../commands/argument.o ../commands/command_option.o ../commands/command.o ../commands/cli/parse.o \
	../core/*.o \
	../crypto/verify.o \
	../datastore/ds_helper.o \
	../datastore/key.o \
	../dnslink/*.o \
	../exchange/bitswap/*.o \
	../flatfs/flatfs.o \
	../importer/importer.o ../importer/exporter.o ../importer/resolver.o \
	../journal/*.o \
	../path/path.o \
	../merkledag/merkledag.o ../merkledag/node.o \
	../multibase/multibase.o \
	../namesys/*.o \
	../pin/pin.o \
	../repo/init.o \
	../repo/fsrepo/*.o \
	../repo/config/*.o \
	../routing/*.o \
	../thirdparty/ipfsaddr/ipfs_addr.o \
	../unixfs/unixfs.o ../unixfs/hamt.o \
	../c-libp2p/c-protobuf/protobuf.o ../c-libp2p/c-protobuf/varint.o \
	../util/errs.o \
	../util/time.o \
	../util/thread_pool.o \
	../nostr/event.o ../nostr/tag.o ../nostr/git.o ../nostr/kind.o ../nostr/pip.o \
	../rbsr/rbsr.o \
	../nostril/sha256.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

ipfs: $(OBJS) $(SECP256K1_AR)
	$(CC) -o $@ $^ $(LFLAGS) ../lmdb/libraries/liblmdb/liblmdb.a

all: ipfs

clean:
	rm -f *.o
	rm -f ipfs
