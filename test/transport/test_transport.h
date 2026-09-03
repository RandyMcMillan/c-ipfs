#ifndef TEST_TRANSPORT_H
#define TEST_TRANSPORT_H

#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ipfs/transport/stream.h"
#include "ipfs/transport/transport.h"
#include "ipfs/transport/registry.h"

// Forward declarations for transport creation functions
extern libp2p_transport_t *libp2p_quic_transport_create(void *tls_ctx);
extern libp2p_transport_t *libp2p_ws_transport_create(void);

int test_transport_stream_struct_size(void) {
    libp2p_stream_t s;
    (void)s;
    return 1;
}

int test_transport_struct_size(void) {
    libp2p_transport_t t;
    (void)t;
    return 1;
}

int test_transport_quic_create_without_lsquic(void) {
    // When HAVE_LSQUIC is not defined, creation should return NULL gracefully
    libp2p_transport_t *t = libp2p_quic_transport_create(NULL);
    if (t != NULL) {
        // If lsquic is available, at least verify the struct is populated
        if (t->name == NULL || t->dial == NULL) {
            return 0;
        }
        if (t->close != NULL) {
            t->close(t);
        }
    }
    return 1;
}

int test_transport_ws_create_without_libwebsockets(void) {
    // When HAS_LIBWEBSOCKETS is not defined, creation should return NULL gracefully
    libp2p_transport_t *t = libp2p_ws_transport_create();
    if (t != NULL) {
        // If libwebsockets is available, at least verify the struct is populated
        if (t->name == NULL || t->dial == NULL) {
            return 0;
        }
        if (t->close != NULL) {
            t->close(t);
        }
    }
    return 1;
}

int test_transport_registry_init_add_free(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *ws = libp2p_ws_transport_create();
    if (ws != NULL) {
        if (transport_registry_add(&reg, ws) != 0) {
            return 0;
        }
    }

    transport_registry_free(&reg);
    return 1;
}

int test_transport_registry_remove(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *ws = libp2p_ws_transport_create();
    if (ws != NULL) {
        transport_registry_add(&reg, ws);
        if (transport_registry_remove(&reg, "ws") != 0) {
            return 0;
        }
    }

    transport_registry_free(&reg);
    return 1;
}

int test_transport_registry_dial_no_match(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_stream_t *stream = NULL;
    int ret = transport_registry_dial(&reg, "/ip4/127.0.0.1/tcp/4001/ws", &stream);
    // With no transports registered, dial should fail
    if (ret == 0 || stream != NULL) {
        return 0;
    }

    transport_registry_free(&reg);
    return 1;
}

static ssize_t mock_stream_read(libp2p_stream_t *stream, uint8_t *buf, size_t len) {
    (void)stream;
    (void)buf;
    (void)len;
    return 0;
}

static ssize_t mock_stream_write(libp2p_stream_t *stream, const uint8_t *buf, size_t len) {
    (void)stream;
    (void)buf;
    return (ssize_t)len;
}

static void mock_stream_close(libp2p_stream_t *stream) {
    free(stream);
}

static int mock_transport_dial(libp2p_transport_t *self, const char *multiaddr, libp2p_stream_t **out_stream) {
    (void)self;
    (void)multiaddr;
    if (!out_stream) {
        return -1;
    }
    libp2p_stream_t *stream = (libp2p_stream_t*)calloc(1, sizeof(libp2p_stream_t));
    if (!stream) {
        return -1;
    }
    stream->read = mock_stream_read;
    stream->write = mock_stream_write;
    stream->close = mock_stream_close;
    *out_stream = stream;
    return 0;
}

static void mock_transport_close(libp2p_transport_t *self) {
    (void)self;
}

int test_transport_registry_dial_match(void) {
    transport_registry_t reg;
    transport_registry_init(&reg);

    libp2p_transport_t *transport = (libp2p_transport_t*)calloc(1, sizeof(libp2p_transport_t));
    if (!transport) {
        return 0;
    }
    transport->name = "ws";
    transport->dial = mock_transport_dial;
    transport->close = mock_transport_close;

    if (transport_registry_add(&reg, transport) != 0) {
        free(transport);
        return 0;
    }

    libp2p_stream_t *stream = NULL;
    if (transport_registry_dial(&reg, "/ip4/127.0.0.1/tcp/4001/ws", &stream) != 0) {
        transport_registry_free(&reg);
        return 0;
    }
    if (stream == NULL) {
        transport_registry_free(&reg);
        return 0;
    }

    (void)stream;
    return 1;
}

typedef struct {
    libp2p_transport_t base;
} live_tcp_transport_t;

typedef struct {
    libp2p_stream_t base;
    int socket_fd;
} live_tcp_stream_t;

static ssize_t live_tcp_stream_read(libp2p_stream_t *stream, uint8_t *buf, size_t len) {
    live_tcp_stream_t *impl = (live_tcp_stream_t*)stream;
    if (impl == NULL || impl->socket_fd < 0 || buf == NULL) {
        return -1;
    }
    return recv(impl->socket_fd, buf, len, 0);
}

static ssize_t live_tcp_stream_write(libp2p_stream_t *stream, const uint8_t *buf, size_t len) {
    live_tcp_stream_t *impl = (live_tcp_stream_t*)stream;
    if (impl == NULL || impl->socket_fd < 0 || buf == NULL) {
        return -1;
    }
    ssize_t written = send(impl->socket_fd, buf, len, 0);
    return written < 0 ? -1 : written;
}

static void live_tcp_stream_close(libp2p_stream_t *stream) {
    live_tcp_stream_t *impl = (live_tcp_stream_t*)stream;
    if (impl != NULL) {
        if (impl->socket_fd >= 0) {
            close(impl->socket_fd);
            impl->socket_fd = -1;
        }
        free(impl);
    }
}

static void live_tcp_transport_close(libp2p_transport_t *self) {
    (void)self;
}

static int live_tcp_transport_dial(libp2p_transport_t *self, const char *multiaddr, libp2p_stream_t **out_stream) {
    (void)self;
    if (out_stream == NULL || multiaddr == NULL) {
        return -1;
    }

    char host[64];
    int port = 0;
    if (sscanf(multiaddr, "/ip4/%63[^/]/tcp/%d", host, &port) != 2) {
        return -1;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1 || connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(socket_fd);
        return -1;
    }

    live_tcp_stream_t *wrapper = (live_tcp_stream_t*)calloc(1, sizeof(live_tcp_stream_t));
    if (wrapper == NULL) {
        close(socket_fd);
        return -1;
    }

    wrapper->base.read = live_tcp_stream_read;
    wrapper->base.write = live_tcp_stream_write;
    wrapper->base.close = live_tcp_stream_close;
    wrapper->socket_fd = socket_fd;
    *out_stream = (libp2p_stream_t*)wrapper;
    return 0;
}

int test_transport_registry_live_tcp_dial(void) {
    int retVal = 0;
    transport_registry_t reg;
    transport_registry_init(&reg);

    live_tcp_transport_t *transport = (live_tcp_transport_t*)calloc(1, sizeof(live_tcp_transport_t));
    if (transport == NULL) {
        return 0;
    }
    transport->base.name = "tcp";
    transport->base.dial = live_tcp_transport_dial;
    transport->base.close = live_tcp_transport_close;

    if (transport_registry_add(&reg, (libp2p_transport_t*)transport) != 0) {
        free(transport);
        return 0;
    }

    pthread_t daemon_thread;
    int daemon_started = 0;
    char *ipfs_path = "./tmp/ipfs_transport_registry_integration";
    char *peer_id = NULL;
    if (!drop_and_build_repository(ipfs_path, 4001, NULL, &peer_id)) {
        transport_registry_free(&reg);
        return 0;
    }
    if (pthread_create(&daemon_thread, NULL, test_daemon_start, (void*)ipfs_path) != 0) {
        transport_registry_free(&reg);
        free(peer_id);
        return 0;
    }
    daemon_started = 1;

    libp2p_stream_t *stream = NULL;
    char multiaddr[256];
    snprintf(multiaddr, sizeof(multiaddr), "/ip4/127.0.0.1/tcp/4001/ipfs/%s", peer_id);

    for (int i = 0; i < 10; i++) {
        if (transport_registry_dial(&reg, multiaddr, &stream) == 0 && stream != NULL) {
            break;
        }
        sleep(1);
    }

    if (stream != NULL && stream->write != NULL && stream->close != NULL) {
        const uint8_t probe[] = "transport-registry-probe\n";
        if (stream->write(stream, probe, strlen((const char*)probe)) > 0) {
            retVal = 1;
        }
        stream->close(stream);
    }

    ipfs_daemon_stop();
    if (daemon_started) {
        pthread_join(daemon_thread, NULL);
    }
    transport_registry_free(&reg);
    free(peer_id);
    return retVal;
}

#endif
