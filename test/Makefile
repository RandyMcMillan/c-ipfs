CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -I../lmdb/libraries/liblmdb -g3 -Wall -std=gnu99
LFLAGS = -L../c-libp2p -L../c-libp2p/c-multihash -L../c-libp2p/c-multiaddr -lp2p -lm -lmultihash -lmultiaddr -lpthread -lcurl
ifeq ($(shell uname -s),Darwin)
LFLAGS += -Wl,-rpath,@loader_path/../c-libp2p
else
LFLAGS += -Wl,-rpath,'$$ORIGIN/../c-libp2p'
endif
DEPS = cmd/ipfs/test_init.h repo/test_repo_bootstrap_peers.h repo/test_repo_config.h repo/test_repo_identity.h cid/test_cid.h
OBJS = testit.o test_helper.o \
	../blocks/block.o ../blocks/blockstore.o \
	../cid/cid.o ../cid/set.o \
	../cmd/cli.o \
	../cmd/ipfs/init.o \
	../commands/argument.o ../commands/command_option.o ../commands/command.o ../commands/cli/parse.o \
	../core/*.o \
	../datastore/ds_helper.o ../datastore/key.o \
	../exchange/bitswap/*.o \
	../flatfs/flatfs.o \
	../importer/importer.o ../importer/exporter.o ../importer/resolver.o \
	../journal/*.o \
	../merkledag/merkledag.o ../merkledag/node.o \
	../multibase/multibase.o \
	../namesys/pb.o \
	../namesys/publisher.o \
	../namesys/resolver.o \
	../namesys/name.o \
	../pin/pin.o \
	../repo/init.o \
	../repo/fsrepo/*.o \
	../repo/config/*.o \
	../routing/offline.o \
	../routing/online.o \
	../routing/k_routing.o \
	../routing/supernode.o \
	../thirdparty/ipfsaddr/ipfs_addr.o \
	../unixfs/unixfs.o ../unixfs/hamt.o \
	../util/thread_pool.o \
	../c-libp2p/c-protobuf/protobuf.o ../c-libp2p/c-protobuf/varint.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

test_ipfs: $(OBJS)
	$(CC) -o $@ $^ $(LFLAGS) ../lmdb/libraries/liblmdb/liblmdb.a

all: test_ipfs

clean:
	rm -f *.o
	rm -f test_ipfs
	
rebuild: clean all
