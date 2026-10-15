#include "../test_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>

#include "libp2p/net/tcp.h"
#include "libp2p/conn/yamux.h"

static void yamux_test_ignore_sigpipe(void) {
    signal(SIGPIPE, SIG_IGN);
}

struct yamux_server_ctx {
    int fd;
    int ok;
};

static void* yamux_echo_server(void* arg)
{
    struct yamux_server_ctx* ctx = (struct yamux_server_ctx*)arg;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(ctx->fd);
    if (!raw) return NULL;

    struct YamuxSession* session = libp2p_yamux_session_new(raw, 1);
    if (!session) {
        raw->close(raw);
        return NULL;
    }

    struct Libp2pV2Stream* stream = libp2p_yamux_stream_accept(session);
    if (!stream) {
        libp2p_yamux_session_free(session);
        return NULL;
    }

    char buf[256];
    ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        if (strcmp(buf, "hello yamux") == 0) {
            const char* reply = "echo: hello yamux";
            if (stream->write(stream, (const unsigned char*)reply, strlen(reply)) == (ssize_t)strlen(reply))
                ctx->ok = 1;
        }
    }

    stream->close(stream);
    libp2p_yamux_session_free(session);
    return NULL;
}

int test_yamux_v2_stream_roundtrip(void)
{
    yamux_test_ignore_sigpipe();
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return 0;

    struct yamux_server_ctx server_ctx = { fds[1], 0 };
    pthread_t tid;
    if (pthread_create(&tid, NULL, yamux_echo_server, &server_ctx) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(fds[0]);
    int ok = 0;
    if (raw) {
        struct YamuxSession* session = libp2p_yamux_session_new(raw, 0);
        if (session) {
            struct Libp2pV2Stream* stream = libp2p_yamux_stream_open(session);
            if (stream) {
                const char* msg = "hello yamux";
                if (stream->write(stream, (const unsigned char*)msg, strlen(msg)) == (ssize_t)strlen(msg)) {
                    char buf[256];
                    ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf) - 1);
                    if (n > 0) {
                        buf[n] = '\0';
                        if (strcmp(buf, "echo: hello yamux") == 0)
                            ok = 1;
                    }
                }
                stream->close(stream);
            }
            libp2p_yamux_session_free(session);
        }
    }

    pthread_join(tid, NULL);
    return ok && server_ctx.ok;
}

static void* yamux_ping_server(void* arg)
{
    struct yamux_server_ctx* ctx = (struct yamux_server_ctx*)arg;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(ctx->fd);
    if (!raw) return NULL;

    struct YamuxSession* session = libp2p_yamux_session_new(raw, 1);
    if (!session) {
        raw->close(raw);
        return NULL;
    }

    struct Libp2pV2Stream* stream = libp2p_yamux_stream_accept(session);
    if (!stream) {
        libp2p_yamux_session_free(session);
        return NULL;
    }

    /* Block reading data; incoming ping requests are answered automatically. */
    char buf[64];
    ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        if (strcmp(buf, "ping") == 0) {
            const char* reply = "pong";
            if (stream->write(stream, (const unsigned char*)reply, strlen(reply)) == (ssize_t)strlen(reply))
                ctx->ok = 1;
        }
    }

    stream->close(stream);
    libp2p_yamux_session_free(session);
    return NULL;
}

int test_yamux_v2_ping(void)
{
    yamux_test_ignore_sigpipe();
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return 0;

    struct yamux_server_ctx server_ctx = { fds[1], 0 };
    pthread_t tid;
    if (pthread_create(&tid, NULL, yamux_ping_server, &server_ctx) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    int ok = 0;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(fds[0]);
    if (raw) {
        struct YamuxSession* session = libp2p_yamux_session_new(raw, 0);
        if (session) {
            struct Libp2pV2Stream* stream = libp2p_yamux_stream_open(session);
            if (stream) {
                if (libp2p_yamux_session_ping(session)) {
                    const char* msg = "ping";
                    if (stream->write(stream, (const unsigned char*)msg, strlen(msg)) == (ssize_t)strlen(msg)) {
                        char buf[64];
                        ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf) - 1);
                        if (n > 0) {
                            buf[n] = '\0';
                            if (strcmp(buf, "pong") == 0)
                                ok = 1;
                        }
                    }
                }
                stream->close(stream);
            }
            libp2p_yamux_session_free(session);
        }
    }

    pthread_join(tid, NULL);
    return ok && server_ctx.ok;
}

static void* yamux_goaway_server(void* arg)
{
    struct yamux_server_ctx* ctx = (struct yamux_server_ctx*)arg;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(ctx->fd);
    if (!raw) return NULL;

    struct YamuxSession* session = libp2p_yamux_session_new(raw, 1);
    if (!session) {
        raw->close(raw);
        return NULL;
    }

    struct Libp2pV2Stream* stream = libp2p_yamux_stream_accept(session);
    if (stream) {
        char buf[64];
        ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf));
        (void)n;
    }

    /* Send GO_AWAY while the stream is still open; the client read must fail. */
    libp2p_yamux_session_goaway(session, 0);
    libp2p_yamux_session_free(session);
    ctx->ok = 1;
    return NULL;
}

int test_yamux_v2_goaway(void)
{
    yamux_test_ignore_sigpipe();
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return 0;

    struct yamux_server_ctx server_ctx = { fds[1], 0 };
    pthread_t tid;
    if (pthread_create(&tid, NULL, yamux_goaway_server, &server_ctx) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    int ok = 0;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(fds[0]);
    if (raw) {
        struct YamuxSession* session = libp2p_yamux_session_new(raw, 0);
        if (session) {
            struct Libp2pV2Stream* stream = libp2p_yamux_stream_open(session);
            if (stream) {
                const char* msg = "ping";
                if (stream->write(stream, (const unsigned char*)msg, strlen(msg)) == (ssize_t)strlen(msg)) {
                    char buf[64];
                    ssize_t n = stream->read(stream, (unsigned char*)buf, sizeof(buf));
                    if (n < 0)
                        ok = 1;
                }
                stream->close(stream);
            }
            libp2p_yamux_session_free(session);
        }
    }

    pthread_join(tid, NULL);
    return ok && server_ctx.ok;
}

#define WINDOW_TEST_TOTAL ((256 * 1024) + 4096)
#define WINDOW_TEST_CHUNK 4096

struct yamux_window_ctx {
    int fd;
    size_t bytes_read;
    int ok;
};

static void* yamux_window_server(void* arg)
{
    struct yamux_window_ctx* ctx = (struct yamux_window_ctx*)arg;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(ctx->fd);
    if (!raw) return NULL;

    struct YamuxSession* session = libp2p_yamux_session_new(raw, 1);
    if (!session) {
        raw->close(raw);
        return NULL;
    }

    struct Libp2pV2Stream* stream = libp2p_yamux_stream_accept(session);
    if (!stream) {
        libp2p_yamux_session_free(session);
        return NULL;
    }

    unsigned char buf[WINDOW_TEST_CHUNK];
    size_t first_phase = 128 * 1024;
    size_t total = 0;
    while (total < first_phase) {
        ssize_t n = stream->read(stream, buf, sizeof(buf));
        if (n <= 0)
            break;
        total += (size_t)n;
    }

    if (total == first_phase) {
        const char* go = "go";
        if (stream->write(stream, (const unsigned char*)go, strlen(go)) != (ssize_t)strlen(go))
            total = 0;
    }

    while (total < WINDOW_TEST_TOTAL) {
        ssize_t n = stream->read(stream, buf, sizeof(buf));
        if (n <= 0)
            break;
        total += (size_t)n;
    }

    ctx->bytes_read = total;
    if (total == WINDOW_TEST_TOTAL) {
        const char* ack = "ok";
        if (stream->write(stream, (const unsigned char*)ack, strlen(ack)) == (ssize_t)strlen(ack))
            ctx->ok = 1;
    }

    stream->close(stream);
    libp2p_yamux_session_free(session);
    return NULL;
}

int test_yamux_v2_window_update(void)
{
    yamux_test_ignore_sigpipe();
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return 0;

    struct yamux_window_ctx server_ctx = { fds[1], 0, 0 };
    pthread_t tid;
    if (pthread_create(&tid, NULL, yamux_window_server, &server_ctx) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    unsigned char* data = (unsigned char*)malloc(WINDOW_TEST_CHUNK);
    if (!data) {
        pthread_join(tid, NULL);
        return 0;
    }
    for (int i = 0; i < WINDOW_TEST_CHUNK; i++)
        data[i] = (unsigned char)(i & 0xff);

    int ok = 0;
    struct Libp2pV2Stream* raw = libp2p_net_tcp_wrap_fd(fds[0]);
    if (raw) {
        struct YamuxSession* session = libp2p_yamux_session_new(raw, 0);
        if (session) {
            struct Libp2pV2Stream* stream = libp2p_yamux_stream_open(session);
            if (stream) {
                size_t sent = 0;
                size_t first_phase = 128 * 1024;
                while (sent < first_phase) {
                    size_t to_send = WINDOW_TEST_CHUNK;
                    if (to_send > first_phase - sent)
                        to_send = first_phase - sent;
                    ssize_t n = stream->write(stream, data, to_send);
                    if (n <= 0)
                        break;
                    sent += (size_t)n;
                }

                /* Read the "go" signal; the read loop also processes incoming
                 * WINDOW_UPDATE frames and replenishes the send window. */
                if (sent == first_phase) {
                    char go[8];
                    ssize_t n = stream->read(stream, (unsigned char*)go, sizeof(go) - 1);
                    if (n > 0) {
                        go[n] = '\0';
                        if (strcmp(go, "go") == 0) {
                            while (sent < WINDOW_TEST_TOTAL) {
                                size_t to_send = WINDOW_TEST_CHUNK;
                                if (to_send > WINDOW_TEST_TOTAL - sent)
                                    to_send = WINDOW_TEST_TOTAL - sent;
                                ssize_t n2 = stream->write(stream, data, to_send);
                                if (n2 <= 0)
                                    break;
                                sent += (size_t)n2;
                            }
                        }
                    }
                }

                if (sent == WINDOW_TEST_TOTAL) {
                    char ack[8];
                    ssize_t n = stream->read(stream, (unsigned char*)ack, sizeof(ack) - 1);
                    if (n > 0) {
                        ack[n] = '\0';
                        if (strcmp(ack, "ok") == 0)
                            ok = 1;
                    }
                }
                stream->close(stream);
            }
            libp2p_yamux_session_free(session);
        }
    }

    free(data);
    pthread_join(tid, NULL);
    return ok && server_ctx.ok;
}
