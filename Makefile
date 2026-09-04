
DEBUG = true
export DEBUG
.DEFAULT_GOAL := help

# Modules that are part of the main project
MODULES := blocks cid cmd commands core crypto exchange importer ipld journal merkledag multibase pin pubsub repo flatfs datastore thirdparty unixfs routing dnslink namesys path util rbsr nostr main transport

# External submodules
SUBMODULES := c-libp2p lmdb nostril libwebsockets boringssl lsquic c-libnostr

# Utility scripts
SCRIPTS := scripts

# Local act helpers
include ACT.mk

prepare:
	@case "$$(uname -s)" in \
		Darwin) bad_fmt='Mach-O' ;; \
		*) bad_fmt='ELF' ;; \
	esac; \
	if find . -type f \( -name '*.o' -o -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -exec file {} + 2>/dev/null | grep -vq "$$bad_fmt"; then \
		echo "  CLEAN stale foreign build outputs"; \
		find . -type f \( -name '*.o' -o -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -delete; \
	fi

# ---------------------------------------------------------------------------
# Top-level aggregates
# ---------------------------------------------------------------------------
all: prepare $(SUBMODULES) $(MODULES) $(SCRIPTS) build-test-module

clean: $(addprefix clean-,$(SUBMODULES)) $(addprefix clean-,$(MODULES)) clean-$(SCRIPTS) clean-build-test-module

clean-all: clean

rebuild: clean all

selfhost: all
	./scripts/selfhost.sh

# ---------------------------------------------------------------------------
# Submodule builds
# ---------------------------------------------------------------------------
c-libp2p:
	cd c-libp2p && $(MAKE) all
	cd c-libp2p/v2 && $(MAKE) all

lmdb:
	cd lmdb/libraries/liblmdb && $(MAKE) all XCFLAGS="-fno-unwind-tables"

nostril:
	@# Ensure secp256k1 submodule exists (it may have been deleted by a prior clean)
	@if [ ! -d nostril/deps/secp256k1 ]; then \
		cd nostril && git submodule update --init --recursive; \
	fi
	@# Pre-configure secp256k1 with conservative CFLAGS so older GCCs
	@# (e.g. act containers) don't fail on auto-detected -std=gnu23.
	@if [ ! -f nostril/deps/secp256k1/config.log ]; then \
		cd nostril/deps/secp256k1 && \
		(if [ ! -x ./configure ]; then ./autogen.sh; fi) && \
		CFLAGS="-std=c99 -O2" ./configure --disable-shared --enable-module-ecdh --enable-module-schnorrsig --enable-module-extrakeys; \
	fi
	@# Remove stale generated files so ./configurator and secp256k1 rebuild for
	@# the current platform (macOS vs Linux act containers).
	rm -f nostril/config.h nostril/configurator nostril/*.o nostril/*.a
	@if [ -f nostril/deps/secp256k1/config.log ]; then \
		host_triplet=$$(grep "^host='" nostril/deps/secp256k1/config.log | head -1 | sed "s/host='//;s/'$$//"); \
		current_arch=$$(uname -m); \
		current_os=$$(uname -s | tr '[:upper:]' '[:lower:]'); \
		if ! echo "$$host_triplet" | grep -q "$$current_arch" 2>/dev/null || \
		   ! echo "$$host_triplet" | grep -q "$$current_os" 2>/dev/null; then \
			echo "  CLEAN stale secp256k1 configure cache ($$host_triplet != $$current_arch-$$current_os)"; \
			rm -rf nostril/deps/secp256k1/config.log nostril/deps/secp256k1/config.status nostril/deps/secp256k1/Makefile nostril/deps/secp256k1/src/*.lo nostril/deps/secp256k1/src/.libs; \
		fi; \
	fi
	cd nostril && $(MAKE) config.h libsecp256k1.a

libwebsockets:
	@if [ ! -f libwebsockets/build-c-ipfs/lib/libwebsockets.a ]; then \
		cmake -B libwebsockets/build-c-ipfs -S libwebsockets -DLWS_WITH_SSL=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_TEST_SERVER=ON -DLWS_WITHOUT_TEST_CLIENT=ON -DLWS_WITHOUT_EXTENSIONS=ON -DCMAKE_BUILD_TYPE=Release -DLWS_STATIC_PIC=ON && \
		cmake --build libwebsockets/build-c-ipfs -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4); \
	fi

boringssl:
	@if [ ! -f boringssl/build-c-ipfs/libssl.a ]; then \
		cmake -B boringssl/build-c-ipfs -S boringssl -DCMAKE_BUILD_TYPE=Release && \
		cmake --build boringssl/build-c-ipfs -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4); \
	fi

lsquic: boringssl
	@if [ ! -f lsquic/build-c-ipfs/src/liblsquic/liblsquic.a ]; then \
		cmake -B lsquic/build-c-ipfs -S lsquic -DCMAKE_BUILD_TYPE=Release -DSSLLIB_INCLUDE=$(PWD)/boringssl/include -DLIBSSL_LIB=$(PWD)/boringssl/build-c-ipfs && \
		cmake --build lsquic/build-c-ipfs -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4); \
	fi

c-libnostr:
	@if [ ! -f c-libnostr/build/libnostr.a ]; then \
		cmake -B c-libnostr/build -S c-libnostr -DCMAKE_BUILD_TYPE=Release \
			-DSECP256K1_LIB_DIR=$(PWD)/nostril/deps/secp256k1/.libs \
			-DSECP256K1_INCLUDE_DIRS=$(PWD)/nostril/deps/secp256k1/include \
			-DSECP256K1_FOUND=TRUE \
			-DNOSTR_FEATURE_RELAY=OFF -DBUILD_SHARED_LIBS=OFF \
			&& cmake --build c-libnostr/build --target nostr_static -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4); \
	fi

# ---------------------------------------------------------------------------
# Module builds
# ---------------------------------------------------------------------------
$(MODULES):
	cd $@ && $(MAKE) all

transport: libwebsockets

# ---------------------------------------------------------------------------
# Clean targets
# ---------------------------------------------------------------------------
clean-c-libp2p:
	cd c-libp2p && $(MAKE) clean
	cd c-libp2p/v2 && $(MAKE) clean

clean-lmdb:
	cd lmdb/libraries/liblmdb && $(MAKE) clean

clean-nostril:
	@# Don't run 'make clean' inside nostril because it does 'rm -rf deps/secp256k1',
	@# which destroys the submodule and breaks subsequent builds.
	cd nostril && rm -f nostril configurator *.o *.a config.h

clean-libwebsockets:
	rm -rf libwebsockets/build-c-ipfs

clean-boringssl:
	rm -rf boringssl/build-c-ipfs

clean-lsquic:
	rm -rf lsquic/build-c-ipfs

clean-c-libnostr:
	rm -rf c-libnostr/build

scripts:
	cd scripts && $(MAKE) all

clean-scripts:
	cd scripts && $(MAKE) clean

clean-%:
	cd $* && $(MAKE) clean

build-test-module:
	cd test && $(MAKE) all

clean-build-test-module:
	cd test && $(MAKE) clean

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
TEST_BIN     := test/test_ipfs
TESTS        ?=
TEST_TIMEOUT ?= 120

# Portable timeout: Linux has `timeout`, macOS often needs `gtimeout` (coreutils)
TIMEOUT_CMD := $(shell command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null)

test-build: all
	cd test && $(MAKE) all

test-run:
	@if [ ! -f $(TEST_BIN) ]; then \
		echo "  ERROR: $(TEST_BIN) not found. Run 'make test-build' first."; \
		exit 1; \
	fi
	@if [ -n "$(TIMEOUT_CMD)" ]; then \
		$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(TEST_BIN) $(TESTS); \
	else \
		echo "  WARNING: no timeout command found (install 'coreutils' on macOS)."; \
		echo "           Running tests without timeout protection."; \
		$(TEST_BIN) $(TESTS); \
	fi
	@echo "--- c-libnostr standalone smoke test ---"
	@cd test && $(MAKE) test_c_libnostr_standalone && ./test_c_libnostr_standalone

test: test-build test-run

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------
help:
	@echo "c-ipfs build commands"
	@echo ""
	@echo "  make all                Build everything (submodules + modules + tests)"
	@echo "  make clean              Remove all build artifacts"
	@echo "  make rebuild            clean + all"
	@echo "  make selfhost           Build then run self-host script"
	@echo "  make act-build          Run the CI build job locally"
	@echo "  make act-kubo-interop   Run the Kubo interop job locally"
	@echo "  make act-list           List workflows and jobs"
	@echo "  make act-watch          Watch repo changes and rerun the workflow"
	@echo "  make act-shell          Open a shell in the act runner image"
	@echo "  make act-job            Run any workflow job with ACT_JOB=<job>"
	@echo ""
	@echo "Submodule targets:"
	@echo "  make c-libp2p           Build c-libp2p submodule"
	@echo "  make lmdb               Build LMDB submodule"
	@echo "  make nostril            Build nostril submodule"
	@echo ""
	@echo "Module targets:"
	@echo "  make <module>           Build a single module (e.g., make namesys)"
	@echo ""
	@echo "Test targets:"
	@echo "  make test               Build and run the full test suite"
	@echo "  make test-build         Build tests only"
	@echo "  make test-run           Run tests without rebuilding"
	@echo "  make test TESTS='t1'    Build and run specific test(s)"
	@echo ""
	@echo "Act flags:"
	@echo "  ACT_REUSE=true          Keep containers after successful runs"
	@echo "  ACT_BIND=true           Bind mount the workspace for reused artifacts"
	@echo "  ACT_REBUILD=true        Rebuild local action images"
	@echo "  ACT_QUIET=true          Silence step output"
	@echo "  ACT_PRIVILEGED=true     Run containers in privileged mode"
	@echo "  ACT_LIST=true           List workflows and jobs"
	@echo "  ACT_WATCH=true          Watch files and rerun workflows"
	@echo "  ACT_DRYRUN=true         Validate workflows without creating containers"
	@echo "  ACT_CLANG_BUILD=true    Also run the optional clang build"
	@echo "  ACT_SHELL=bash          Shell to launch inside the runner image"
	@echo ""
	@echo "Examples:"
	@echo "  make act-build"
	@echo "  make act-build ACT_REUSE=true ACT_BIND=true"
	@echo "  make act-watch ACT_REUSE=true ACT_BIND=true"
	@echo "  make act-shell"
	@echo "  make act-kubo-interop ACT_PRIVILEGED=true"
	@echo "  make act-job ACT_JOB=build ACT_CLANG_BUILD=true"
	@echo ""
	@echo "Clean targets:"
	@echo "  make clean-<name>       Clean a specific submodule or module"

.PHONY: all clean clean-all rebuild selfhost prepare help test test-build test-run build-test-module clean-build-test-module
.PHONY: $(SUBMODULES) $(MODULES) $(SCRIPTS)
.PHONY: $(addprefix clean-,$(SUBMODULES)) $(addprefix clean-,$(MODULES)) $(addprefix clean-,$(SCRIPTS))
