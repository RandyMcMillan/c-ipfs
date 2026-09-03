CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -I../lmdb/libraries/liblmdb -I../nostril -I../nostril/deps/secp256k1/include -g3 -Wall -std=gnu99
LFLAGS = -L../c-libp2p -L../c-libp2p/c-multihash -L../c-libp2p/c-multiaddr -lp2p -lm -lmultihash -lmultiaddr -lpthread -lresolv -lcurl -lz
ifeq ($(shell uname -s),Darwin)
CFLAGS += -I/opt/homebrew/opt/openssl/include
LFLAGS += -Wl,-rpath,@loader_path/../c-libp2p -L/opt/homebrew/opt/openssl/lib
else
LFLAGS += -Wl,-rpath,'$$ORIGIN/../c-libp2p'
endif
LFLAGS += -lssl -lcrypto ../nostril/libsecp256k1.a
DEPS = cmd/ipfs/test_init.h repo/test_repo_bootstrap_peers.h repo/test_repo_config.h repo/test_repo_identity.h cid/test_cid.h transport/test_transport.h
OBJS = testit.o test_helper.o \
	../blocks/block.o ../blocks/blockstore.o ../blocks/blockstore_shard.o \
	../cid/cid.o ../cid/set.o ../cid/cid_v1.o \
	../cmd/cli.o \
	../cmd/ipfs/init.o \
	../commands/argument.o ../commands/command_option.o ../commands/command.o ../commands/cli/parse.o \
	../core/*.o \
	../crypto/verify.o ../crypto/verify_impl.o ../crypto/security.o \
	../datastore/ds_helper.o ../datastore/key.o ../datastore/key_impl.o \
	../dnslink/dnslink.o ../dnslink/dns_cache.o \
	../exchange/bitswap/*.o \
	../flatfs/flatfs.o \
	../importer/importer.o ../importer/exporter.o ../importer/resolver.o ../importer/recursive.o ../importer/progress.o \
	../ipld/dag_cbor.o \
	../journal/*.o \
	../merkledag/merkledag.o ../merkledag/node.o ../merkledag/node_copy.o \
	../multibase/multibase.o \
	../namesys/base.o \
	../namesys/dns.o \
	../namesys/isdomain.o \
	../namesys/namesys.o \
	../namesys/pb.o \
	../namesys/proquint.o \
	../namesys/publisher.o \
	../namesys/resolver.o ../namesys/namesys_resolve.o \
	../namesys/name.o \
	../namesys/verify.o \
	../path/path.o \
	../path/resolver.o \
	../pin/pin.o ../pin/pin_gc.o \
	../pubsub/ipns_pubsub.o ../pubsub/gossipsub.o \
	../repo/init.o \
	../repo/fsrepo/*.o \
	../repo/config/*.o \
	../routing/offline.o \
	../routing/online.o \
	../routing/k_routing.o \
	../routing/supernode.o \
	../thirdparty/ipfsaddr/ipfs_addr.o \
	../transport/quic_transport.o ../transport/ws_transport.o ../transport/registry.o \
	../unixfs/unixfs.o ../unixfs/hamt.o \
	../util/thread_pool.o ../util/time.o ../util/errs.o ../util/errs_impl.o \
	../nostr/event.o ../nostr/git.o ../nostr/kind.o ../nostr/pip.o ../nostr/tag.o \
	../rbsr/rbsr.o \
	../nostril/sha256.o \
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
