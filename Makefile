
DEBUG = true
export DEBUG
.DEFAULT_GOAL := all

# Modules that are part of the main project
MODULES := blocks cid cmd commands core crypto exchange importer ipld journal merkledag multibase pin pubsub repo flatfs datastore thirdparty unixfs routing dnslink namesys path util rbsr nostr main transport

# External submodules
SUBMODULES := c-libp2p lmdb nostril

# Utility scripts
SCRIPTS := scripts

prepare:
	@if [ "$$(uname -s)" = "Darwin" ] && find . -type f -name '*.o' -exec file {} + 2>/dev/null | grep -vq 'Mach-O'; then \
		echo "  CLEAN stale non-Mach-O build outputs"; \
		find . -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -delete; \
	fi

# ---------------------------------------------------------------------------
# Top-level aggregates
# ---------------------------------------------------------------------------
all: prepare $(SUBMODULES) $(MODULES) $(SCRIPTS) build-test-module

clean: $(addprefix clean-,$(SUBMODULES)) $(addprefix clean-,$(MODULES)) clean-$(SCRIPTS) clean-build-test-module

rebuild: clean all

selfhost: all
	./scripts/selfhost.sh

# ---------------------------------------------------------------------------
# Submodule builds
# ---------------------------------------------------------------------------
c-libp2p:
	cd c-libp2p && $(MAKE) all

lmdb:
	cd lmdb/libraries/liblmdb && $(MAKE) all XCFLAGS="-fno-unwind-tables"

nostril:
	cd nostril && $(MAKE) config.h libsecp256k1.a

# ---------------------------------------------------------------------------
# Module builds
# ---------------------------------------------------------------------------
$(MODULES):
	cd $@ && $(MAKE) all

# ---------------------------------------------------------------------------
# Clean targets
# ---------------------------------------------------------------------------
clean-c-libp2p:
	cd c-libp2p && $(MAKE) clean

clean-lmdb:
	cd lmdb/libraries/liblmdb && $(MAKE) clean

clean-nostril:
	cd nostril && $(MAKE) clean

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
	@echo "Clean targets:"
	@echo "  make clean-<name>       Clean a specific submodule or module"

.PHONY: all clean rebuild selfhost prepare help test test-build test-run build-test-module clean-build-test-module
.PHONY: $(SUBMODULES) $(MODULES) $(SCRIPTS)
.PHONY: $(addprefix clean-,$(SUBMODULES)) $(addprefix clean-,$(MODULES)) $(addprefix clean-,$(SCRIPTS))
