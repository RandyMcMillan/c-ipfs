CC = gcc
CFLAGS = -O0 -I../include -I../c-libp2p/include -I../c-libp2p/c-multihash/include -I../c-libp2p/c-multiaddr/include -I../c-libp2p/c-protobuf -I../lmdb/libraries/liblmdb -I../nostril -I../nostril/deps/secp256k1/include -g3 -Wall -std=gnu99
LWS_LIB = ../libwebsockets/build-c-ipfs/lib/libwebsockets.a
LFLAGS = -L../c-libp2p -L../c-libp2p/c-multihash -L../c-libp2p/c-multiaddr -lp2p -lm -lmultihash -lmultiaddr -lpthread -lresolv $(LWS_LIB)
ifeq ($(shell uname -s),Darwin)
LFLAGS += -Wl,-rpath,@loader_path/../c-libp2p -framework SystemConfiguration -framework CoreFoundation
else
LFLAGS += -Wl,-rpath,'$$ORIGIN/../c-libp2p'
endif

# lsquic + BoringSSL (optional; experimental due to OpenSSL symbol conflicts)
LSQUIC_LIB = ../lsquic/build-c-ipfs/src/liblsquic/liblsquic.a
BORINGSSL_LIB = ../boringssl/build-c-ipfs/libssl.a ../boringssl/build-c-ipfs/libcrypto.a

ifdef HAS_LSQUIC
CFLAGS += -DHAS_LSQUIC -I../lsquic/include -I../boringssl/include
LFLAGS += $(LSQUIC_LIB) $(BORINGSSL_LIB) -lz -lcurl -lz ../nostril/libsecp256k1.a
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
LFLAGS += -lcurl -lz -lssl -lcrypto ../nostril/libsecp256k1.a
endif
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
	../transport/quic_transport.o ../transport/ws_transport.o ../transport/registry.o ../transport/stream_bridge.o \
	../transport/v2_stream_wrapper.o ../transport/noise_v2_bridge.o \
	../c-libp2p/v2/libp2p_v2.a \
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

# secp256k1 cross-verification: libsecp256k1 vs linked SSL library
OPENSSL_LDFLAGS =
ifeq ($(shell uname -s),Darwin)
OPENSSL_LDFLAGS += -L/opt/homebrew/opt/openssl/lib
endif
SECP256K1_LDFLAGS = -L../nostril/deps/secp256k1/.libs -lsecp256k1

test_secp256k1_cross_openssl: crypto/test_secp256k1_cross.c
	$(CC) -o $@ $< $(CFLAGS) $(OPENSSL_LDFLAGS) -lssl -lcrypto $(SECP256K1_LDFLAGS) -lpthread

test_secp256k1_cross_boringssl: crypto/test_secp256k1_cross.c
	$(CC) -o $@ $< $(CFLAGS) -I../boringssl/include \
		../boringssl/build-c-ipfs/libssl.a ../boringssl/build-c-ipfs/libcrypto.a \
		$(SECP256K1_LDFLAGS) -lpthread -lz \
		$(shell if [ "$$(uname -s)" = "Darwin" ]; then echo "-lc++"; else echo "-lstdc++"; fi)

secp256k1-cross-verify: test_secp256k1_cross_openssl test_secp256k1_cross_boringssl
	@echo "=== OpenSSL-linked verification ==="
	./test_secp256k1_cross_openssl
	@echo "=== BoringSSL-linked verification ==="
	./test_secp256k1_cross_boringssl

clean:
	rm -f *.o
	rm -f test_ipfs
	rm -f test_secp256k1_cross_openssl test_secp256k1_cross_boringssl
	
rebuild: clean all
