#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include "ipfs/transport/swarm_dialer_v2_bridge.h"
#include "ipfs/transport/noise_v2_bridge.h"
#include "libp2p/net/tcp.h"
#include "libp2p/net/ws.h"
#include "libp2p/net/quic.h"
#include "libp2p/conn/yamux.h"
#include "libp2p/conn/noise.h"
#include "libp2p/conn/multistream.h"
#include "libp2p/identify/identify_v2_ipfs.h"
#include "libp2p/crypto/key.h"
#include "libp2p/crypto/rsa.h"
#include "libp2p/utils/logger.h"
#include "libp2p/utils/debug.h"
#include "varint.h"

/* NOTE: keep debug prints. They are gated by DEBUG_SCOPE / DEBUG env vars
 * (see libp2p/utils/debug.h). Do not strip them when a phase is "done". */

#define IPFS_IDENTIFY_PROTO_100 "/ipfs/id/1.0.0"

#define BRIDGE_IDENTIFY_BUF_SIZE 4096

struct ipfs_v2_stream_bridge {
    struct YamuxSession *yamux_session;
    struct Libp2pV2Stream *yamux_stream;
    int connected;
    unsigned char identify_buf[BRIDGE_IDENTIFY_BUF_SIZE];
    size_t identify_len;
    size_t identify_pos;
};

static int bridge_parse_multiaddr(const char *multiaddr_str, char *ip_out, size_t ip_len, uint16_t *port_out)
{
    if (!multiaddr_str || !ip_out || !port_out)
        return 0;

    char ip[INET6_ADDRSTRLEN];
    unsigned int port = 0;
    int parsed = sscanf(multiaddr_str, "/ip4/%15[^/]/tcp/%u", ip, &port);
    if (parsed != 2) {
        parsed = sscanf(multiaddr_str, "/ip6/%45[^/]/tcp/%u", ip, &port);
    }
    if (parsed != 2)
        return 0;
    if (port == 0 || port > 65535)
        return 0;

    strncpy(ip_out, ip, ip_len - 1);
    ip_out[ip_len - 1] = '\0';
    *port_out = (uint16_t)port;
    return 1;
}

static int bridge_parse_quic_multiaddr(const char *multiaddr_str, char *ip_out, size_t ip_len,
                                       uint16_t *port_out, char *peer_id_out, size_t peer_id_len)
{
    if (!multiaddr_str || !ip_out || !port_out || !peer_id_out)
        return 0;

    char ip[INET6_ADDRSTRLEN];
    unsigned int port = 0;
    int parsed = sscanf(multiaddr_str, "/ip4/%15[^/]/udp/%u/quic-v1", ip, &port);
    if (parsed != 2)
        parsed = sscanf(multiaddr_str, "/ip6/%45[^/]/udp/%u/quic-v1", ip, &port);
    if (parsed != 2)
        return 0;
    if (port == 0 || port > 65535)
        return 0;

    strncpy(ip_out, ip, ip_len - 1);
    ip_out[ip_len - 1] = '\0';
    *port_out = (uint16_t)port;

    peer_id_out[0] = '\0';
    const char *p2p = strstr(multiaddr_str, "/p2p/");
    if (p2p) {
        p2p += 5;
        size_t i = 0;
        while (p2p[i] && p2p[i] != '/' && i + 1 < peer_id_len) {
            peer_id_out[i] = p2p[i];
            i++;
        }
        peer_id_out[i] = '\0';
    }
    return 1;
}

static int bridge_negotiate_protocol(struct Libp2pV2Stream *stream, const char *protocol)
{
    if (!stream || !protocol)
        return 0;

    if (!libp2p_v2_net_multistream_connect(stream))
        return 0;

    return libp2p_net_multistream_negotiate_protocol(stream, protocol);
}

int ipfs_swarm_connect_v2_bridge(const char *multiaddr_str,
                                 const char *protocol,
                                 void *private_key,
                                 ipfs_v2_stream_bridge_t **out_bridge)
{
    if (!multiaddr_str || !private_key || !out_bridge)
        return -EINVAL;

    /* ------------------------------------------------------------------
     * QUIC path: TLS 1.3 + QUIC provides both security and multiplexing,
     * so we skip Noise and yamux entirely.
     * ------------------------------------------------------------------ */
#ifdef HAS_LSQUIC
    if (strstr(multiaddr_str, "/quic-v1") != NULL) {
        char ip[INET6_ADDRSTRLEN];
        uint16_t port = 0;
        char expected_peer_id[128];
        if (!bridge_parse_quic_multiaddr(multiaddr_str, ip, sizeof(ip), &port,
                                         expected_peer_id, sizeof(expected_peer_id))) {
            libp2p_logger_error("v2_bridge", "Cannot parse QUIC multiaddr: %s\n", multiaddr_str);
            return -EINVAL;
        }

        const noise_identity_callbacks_t *tls_cbs = ipfs_tls_rsa_callbacks();
        const char *peer_id = expected_peer_id[0] ? expected_peer_id : NULL;
        struct Libp2pV2Stream *outbound = libp2p_net_quic_dial(ip, port, private_key,
                                                               peer_id, tls_cbs);
        if (!outbound) {
            libp2p_logger_error("v2_bridge", "QUIC dial to %s:%u failed\n", ip, port);
            return -ECONNREFUSED;
        }
        DBG_SCOPE("v2_bridge", "QUIC outbound stream acquired\n");

        char msg[256];

        /* Build our Identify response from the repo identity key. */
        struct RsaPrivateKey *rsa = (struct RsaPrivateKey *)private_key;
        char *our_peer_id = NULL;
        const unsigned char *pubkey_der = NULL;
        size_t pubkey_len = 0;
        if (rsa && rsa->public_key_der && rsa->public_key_length > 0) {
            pubkey_der = (const unsigned char *)rsa->public_key_der;
            pubkey_len = rsa->public_key_length;
            struct PublicKey pubkey;
            memset(&pubkey, 0, sizeof(pubkey));
            pubkey.type = KEYTYPE_RSA;
            pubkey.data = (unsigned char *)rsa->public_key_der;
            pubkey.data_size = rsa->public_key_length;
            libp2p_crypto_public_key_to_peer_id(&pubkey, &our_peer_id);
        }

        /* Identify exchange over QUIC: libp2p Identify opens one stream in
         * each direction, just like the yamux path above.
         *
         *  - outbound stream: we opened it; run the full multistream-select
         *    handshake for /ipfs/id/1.0.0 so Kubo sees a complete Identify
         *    request and opens its own Identify stream to us.
         *  - inbound stream: Kubo opens it; we accept it, echo the header and
         *    protocol, then send our Identify response.
         *
         * Crucially, send our Identify on the inbound stream BEFORE reading
         * Kubo's Identify on the outbound stream. go-libp2p can defer its
         * outbound Identify until it has seen ours; reading first creates a
         * circular wait. */
        DBG_SCOPE("v2_bridge", "QUIC outbound multistream handshake start\n");
        if (!libp2p_net_multistream_write_protocol(outbound, "/multistream/1.0.0\n") ||
            !libp2p_net_multistream_read_protocol(outbound, msg, sizeof(msg)) ||
            strcmp(msg, "/multistream/1.0.0\n") != 0 ||
            !libp2p_net_multistream_write_protocol(outbound, "/ipfs/id/1.0.0\n") ||
            !libp2p_net_multistream_read_protocol(outbound, msg, sizeof(msg)) ||
            strcmp(msg, "/ipfs/id/1.0.0\n") != 0) {
            libp2p_logger_error("v2_bridge", "Outbound QUIC Identify protocol negotiation failed: '%s'\n", msg);
            if (our_peer_id) free(our_peer_id);
            outbound->close(outbound);
            return -EPROTO;
        }
        DBG_SCOPE("v2_bridge", "QUIC outbound Identify negotiated\n");

        /* Now that Kubo has seen our complete Identify request, accept its
         * inbound Identify stream. */
        struct Libp2pV2Stream *inbound = libp2p_net_quic_accept_stream(outbound);
        if (!inbound) {
            libp2p_logger_error("v2_bridge", "QUIC accept inbound identify stream failed\n");
            if (our_peer_id) free(our_peer_id);
            outbound->close(outbound);
            return -ECONNABORTED;
        }
        DBG_SCOPE("v2_bridge", "QUIC inbound stream acquired\n");

        /* Inbound stream: peer opened it, so we are the multistream-select
         * responder. Read and echo its header, then read and confirm its
         * /ipfs/id/1.0.0 proposal. */
        if (!libp2p_net_multistream_read_protocol(inbound, msg, sizeof(msg)) ||
            strcmp(msg, "/multistream/1.0.0\n") != 0 ||
            !libp2p_net_multistream_write_protocol(inbound, "/multistream/1.0.0\n") ||
            !libp2p_net_multistream_read_protocol(inbound, msg, sizeof(msg)) ||
            strcmp(msg, "/ipfs/id/1.0.0\n") != 0 ||
            !libp2p_net_multistream_write_protocol(inbound, "/ipfs/id/1.0.0\n")) {
            libp2p_logger_error("v2_bridge", "Inbound QUIC Identify protocol negotiation failed: '%s'\n", msg);
            if (our_peer_id) free(our_peer_id);
            inbound->close(inbound);
            outbound->close(outbound);
            return -EPROTO;
        }
        DBG_SCOPE("v2_bridge", "QUIC inbound Identify negotiated; sending our response\n");

        /* Send our Identify on the inbound stream first, so Kubo is unblocked
         * to send its Identify on our outbound stream. */
        if (!libp2p_identify_send_response_ipfs(inbound, our_peer_id, pubkey_der, pubkey_len, NULL, 0)) {
            libp2p_logger_error("v2_bridge", "Failed to send QUIC Identify response on inbound stream\n");
            if (our_peer_id) free(our_peer_id);
            inbound->close(inbound);
            outbound->close(outbound);
            return -EPROTO;
        }
        DBG_SCOPE("v2_bridge", "QUIC Identify response sent on inbound stream\n");

        /* Outbound stream: Kubo is the responder. Read its length-delimited
         * Identify response into a temporary bridge buffer. */
        ipfs_v2_stream_bridge_t *bridge = (ipfs_v2_stream_bridge_t *)calloc(1, sizeof(*bridge));
        if (!bridge) {
            if (our_peer_id) free(our_peer_id);
            inbound->close(inbound);
            outbound->close(outbound);
            return -ENOMEM;
        }
        bridge->connected = 1;

        unsigned char prefix_buf[16];
        size_t prefix_len = 0;
        size_t payload_len = 0;
        size_t prefix_consumed = 0;
        while (prefix_len < sizeof(prefix_buf)) {
            ssize_t n = outbound->read(outbound, prefix_buf + prefix_len,
                                       sizeof(prefix_buf) - prefix_len);
            DBG_SCOPE("v2_bridge", "QUIC outbound prefix read n=%zd prefix_len=%zu\n", n, prefix_len);
            if (n <= 0)
                break;
            prefix_len += (size_t)n;
            size_t decoded_bytes = 0;
            unsigned long long v = varint_decode(prefix_buf, (int)prefix_len, &decoded_bytes);
            if (decoded_bytes > 0 && decoded_bytes <= prefix_len) {
                payload_len = (size_t)v;
                prefix_consumed = decoded_bytes;
                DBG_SCOPE("v2_bridge", "QUIC outbound identify payload_len=%zu\n", payload_len);
                break;
            }
        }
        size_t payload_in_prefix = 0;
        if (prefix_consumed > 0 && prefix_len > prefix_consumed) {
            payload_in_prefix = prefix_len - prefix_consumed;
            if (payload_in_prefix > sizeof(bridge->identify_buf))
                payload_in_prefix = sizeof(bridge->identify_buf);
            memcpy(bridge->identify_buf, prefix_buf + prefix_consumed, payload_in_prefix);
        }
        size_t total = payload_in_prefix;
        while (payload_len > 0 && total < payload_len && total < sizeof(bridge->identify_buf)) {
            ssize_t n = outbound->read(outbound, bridge->identify_buf + total,
                                       sizeof(bridge->identify_buf) - total);
            DBG_SCOPE("v2_bridge", "QUIC outbound payload read n=%zd total=%zu/%zu\n", n, total, payload_len);
            if (n <= 0)
                break;
            total += (size_t)n;
        }
        bridge->identify_len = total;
        DBG_SCOPE("v2_bridge", "QUIC read %zu bytes outbound Identify response\n", total);

        if (our_peer_id)
            free(our_peer_id);
        inbound->close(inbound);
        DBG_SCOPE("v2_bridge", "QUIC inbound stream closed\n");
        outbound->close(outbound);
        DBG_SCOPE("v2_bridge", "QUIC outbound stream closed, returning bridge\n");

        *out_bridge = bridge;
        DBG_SCOPE("v2_bridge", "QUIC returning 0\n");
        return 0;
    }
#else
    if (strstr(multiaddr_str, "/quic-v1") != NULL) {
        libp2p_logger_error("v2_bridge", "QUIC support not compiled in (HAS_LSQUIC missing)\n");
        return -EPROTONOSUPPORT;
    }
#endif

    char ip[INET6_ADDRSTRLEN];
    uint16_t port = 0;
    if (!bridge_parse_multiaddr(multiaddr_str, ip, sizeof(ip), &port)) {
        libp2p_logger_error("v2_bridge", "Cannot parse multiaddr: %s\n", multiaddr_str);
        return -EINVAL;
    }

    struct Libp2pV2Stream *transport_stream = NULL;
    int is_ws = (strstr(multiaddr_str, "/ws") != NULL);
    if (is_ws) {
        transport_stream = libp2p_net_ws_dial(ip, port);
    } else {
        transport_stream = libp2p_net_tcp_dial(ip, port);
    }
    if (!transport_stream) {
        libp2p_logger_error("v2_bridge", "%s dial to %s:%u failed\n",
                            is_ws ? "WebSocket" : "TCP", ip, port);
        return -ECONNREFUSED;
    }

    /* Multistream: /noise */
    if (!libp2p_v2_net_multistream_connect(transport_stream)) {
        libp2p_logger_error("v2_bridge", "Multistream handshake failed for /noise\n");
        transport_stream->close(transport_stream);
        return -EPROTO;
    }

    if (!libp2p_net_multistream_negotiate_protocol(transport_stream, "/noise")) {
        libp2p_logger_error("v2_bridge", "Peer did not accept /noise\n");
        transport_stream->close(transport_stream);
        return -EPROTONOSUPPORT;
    }

    /* Noise XX initiator handshake */
    const noise_identity_callbacks_t *noise_cbs = ipfs_noise_rsa_callbacks();
    struct Libp2pV2Stream *noise_stream = libp2p_noise_handshake_raw(transport_stream, private_key, NULL, noise_cbs);
    if (!noise_stream) {
        libp2p_logger_error("v2_bridge", "Noise initiator handshake failed\n");
        transport_stream->close(transport_stream);
        return -ECONNABORTED;
    }

    /* Muxer negotiation: /yamux/1.0.0 over the secure channel. */
    if (!libp2p_v2_net_multistream_connect(noise_stream)) {
        libp2p_logger_error("v2_bridge", "Multistream handshake for yamux failed\n");
        noise_stream->close(noise_stream);
        return -EPROTO;
    }
    if (!libp2p_net_multistream_negotiate_protocol(noise_stream, "/yamux/1.0.0")) {
        libp2p_logger_error("v2_bridge", "Peer did not accept /yamux/1.0.0\n");
        noise_stream->close(noise_stream);
        return -EPROTONOSUPPORT;
    }

    /* Yamux client session */
    struct YamuxSession *session = libp2p_yamux_session_new(noise_stream, 0);
    if (!session) {
        libp2p_logger_error("v2_bridge", "Yamux client session creation failed\n");
        noise_stream->close(noise_stream);
        return -ENOMEM;
    }

    /* Identify exchange: libp2p Identify opens one stream in each direction.
     *  - outbound stream: we open it, negotiate /ipfs/id/1.0.0, and send our Identify.
     *  - inbound stream: Kubo opens it; we accept it, accept /ipfs/id/1.0.0, and read Kubo's Identify.
     */
    struct Libp2pV2Stream *outbound_id = libp2p_yamux_stream_open(session);
    if (!outbound_id) {
        libp2p_logger_error("v2_bridge", "Yamux identify stream open failed\n");
        libp2p_yamux_session_free(session);
        return -ECONNABORTED;
    }

    libp2p_logger_info("v2_bridge", "Negotiating outbound identify protocol on stream %p\n", (void*)outbound_id);

    /* Send our outbound multistream header immediately so Kubo has traffic to
     * respond to, then accept its inbound stream. */
    char msg[256];
    if (!libp2p_net_multistream_write_protocol(outbound_id, "/multistream/1.0.0\n")) {
        libp2p_logger_error("v2_bridge", "Failed to write outbound multistream header\n");
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }

    struct Libp2pV2Stream *inbound_id = libp2p_yamux_stream_accept(session);
    if (!inbound_id) {
        libp2p_logger_error("v2_bridge", "Yamux inbound identify stream accept failed\n");
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -ECONNABORTED;
    }
    libp2p_logger_info("v2_bridge", "Accepted inbound identify stream %p\n", (void*)inbound_id);



    /* Multistream-select Identify handshake.
     * Kubo opened inbound_id, so it is the initiator on that stream. Read and
     * echo its header, then finish our outbound identify handshake and send our
     * Identify. Only after that do we read Kubo's inbound protocol proposal and
     * Identify payload; this prevents circular waits when go-libp2p defers its
     * outbound response until it has seen our Identify. */
    if (!libp2p_net_multistream_read_protocol(inbound_id, msg, sizeof(msg)) ||
        strcmp(msg, "/multistream/1.0.0\n") != 0) {
        libp2p_logger_error("v2_bridge", "Failed to read inbound multistream header: '%s'\n", msg);
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }
    if (!libp2p_net_multistream_write_protocol(inbound_id, "/multistream/1.0.0\n")) {
        libp2p_logger_error("v2_bridge", "Failed to echo inbound multistream header\n");
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }

    /* Finish our outbound identify handshake and send our Identify. */
    if (!libp2p_net_multistream_read_protocol(outbound_id, msg, sizeof(msg)) ||
        strcmp(msg, "/multistream/1.0.0\n") != 0) {
        libp2p_logger_error("v2_bridge", "Outbound multistream handshake failed\n");
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }

    if (!libp2p_net_multistream_write_protocol(outbound_id, "/ipfs/id/1.0.0\n")) {
        libp2p_logger_error("v2_bridge", "Failed to write outbound identify proposal\n");
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }
    if (!libp2p_net_multistream_read_protocol(outbound_id, msg, sizeof(msg))) {
        libp2p_logger_error("v2_bridge", "Failed to read outbound identify confirmation\n");
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }
    if (strcmp(msg, "/ipfs/id/1.0.0\n") != 0) {
        libp2p_logger_error("v2_bridge", "Outbound identify protocol negotiation failed: got '%s'\n", msg);
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }

    /* Now read and confirm Kubo's inbound protocol proposal. */
    if (!libp2p_net_multistream_read_protocol(inbound_id, msg, sizeof(msg)) ||
        strcmp(msg, "/ipfs/id/1.0.0\n") != 0) {
        libp2p_logger_error("v2_bridge", "Unexpected inbound protocol proposal: '%s'\n", msg);
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }
    if (!libp2p_net_multistream_write_protocol(inbound_id, "/ipfs/id/1.0.0\n")) {
        libp2p_logger_error("v2_bridge", "Failed to confirm inbound identify protocol\n");
        inbound_id->close(inbound_id);
        outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -EPROTO;
    }
    libp2p_logger_info("v2_bridge", "Identify multistream handshake complete\n");

    /* Build our Identify response from the repo identity key.
     * private_key is &identity->private_key, i.e. struct RsaPrivateKey*. */
    struct RsaPrivateKey *rsa = (struct RsaPrivateKey *)private_key;
    char *peer_id = NULL;
    const unsigned char *pubkey_der = NULL;
    size_t pubkey_len = 0;
    if (rsa && rsa->public_key_der && rsa->public_key_length > 0) {
        pubkey_der = (const unsigned char *)rsa->public_key_der;
        pubkey_len = rsa->public_key_length;
        struct PublicKey pubkey;
        memset(&pubkey, 0, sizeof(pubkey));
        pubkey.type = KEYTYPE_RSA;
        pubkey.data = (unsigned char *)rsa->public_key_der;
        pubkey.data_size = rsa->public_key_length;
        libp2p_crypto_public_key_to_peer_id(&pubkey, &peer_id);
    }

    /* Inbound stream: Kubo initiated it, so we are the responder. Send our
     * Identify and let Kubo read it. */
    DBG_SCOPE("v2_bridge", "About to send Identify response on inbound stream (peer_id=%s pubkey_len=%zu)\n",
              peer_id ? peer_id : "?", pubkey_len);
    int send_ok = libp2p_identify_send_response_ipfs(inbound_id, peer_id, pubkey_der, pubkey_len, NULL, 0);
    if (!send_ok) {
        DBG_SCOPE("v2_bridge", "Failed to send Identify response on inbound stream\n");
    } else {
        DBG_SCOPE("v2_bridge", "Sent Identify response on inbound stream (peer_id=%s)\n", peer_id ? peer_id : "?");
    }
    if (peer_id)
        free(peer_id);

    /* Outbound stream: we initiated it, so Kubo is the responder. Read Kubo's
     * length-delimited (msgio) Identify response. */
    ipfs_v2_stream_bridge_t *bridge = (ipfs_v2_stream_bridge_t *)calloc(1, sizeof(*bridge));
    if (!bridge) {
        inbound_id->close(inbound_id);
        if (outbound_id)
            outbound_id->close(outbound_id);
        libp2p_yamux_session_free(session);
        return -ENOMEM;
    }
    bridge->yamux_session = session;
    bridge->connected = 1;

    DBG_SCOPE("v2_bridge", "About to read outbound Identify response\n");
    fflush(stderr);

    unsigned char prefix_buf[16];
    size_t prefix_len = 0;
    size_t payload_len = 0;
    size_t prefix_consumed = 0;
    while (prefix_len < sizeof(prefix_buf)) {
        ssize_t n = outbound_id->read(outbound_id, prefix_buf + prefix_len,
                                      sizeof(prefix_buf) - prefix_len);
        DBG_SCOPE("v2_bridge", "prefix read n=%zd prefix_len=%zu\n", n, prefix_len);
        if (n <= 0)
            break;
        prefix_len += (size_t)n;

        size_t decoded_bytes = 0;
        unsigned long long v = varint_decode(prefix_buf, (int)prefix_len, &decoded_bytes);
        DBG_SCOPE("v2_bridge", "varint_decode v=%llu decoded_bytes=%zu\n", v, decoded_bytes);
        if (decoded_bytes > 0 && decoded_bytes <= prefix_len) {
            payload_len = (size_t)v;
            prefix_consumed = decoded_bytes;
            DBG_SCOPE("v2_bridge", "outbound Identify payload_len=%zu prefix_consumed=%zu\n",
                      payload_len, prefix_consumed);
            fflush(stderr);
            break;
        }
    }

    size_t payload_in_prefix = 0;
    if (prefix_consumed > 0 && prefix_len > prefix_consumed) {
        payload_in_prefix = prefix_len - prefix_consumed;
        if (payload_in_prefix > sizeof(bridge->identify_buf))
            payload_in_prefix = sizeof(bridge->identify_buf);
        memcpy(bridge->identify_buf, prefix_buf + prefix_consumed, payload_in_prefix);
    }

    size_t total = payload_in_prefix;
    DBG_SCOPE("v2_bridge", "payload loop start payload_len=%zu total=%zu\n", payload_len, total);
    while (payload_len > 0 && total < payload_len && total < sizeof(bridge->identify_buf)) {
        ssize_t n = outbound_id->read(outbound_id, bridge->identify_buf + total,
                                      sizeof(bridge->identify_buf) - total);
        DBG_SCOPE("v2_bridge", "payload read n=%zd total=%zu\n", n, total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    bridge->identify_len = total;
    libp2p_logger_info("v2_bridge", "Read %zu bytes of outbound Identify response\n", total);

    inbound_id->close(inbound_id);
    if (outbound_id)
        outbound_id->close(outbound_id);

    *out_bridge = bridge;
    return 0;
}

ssize_t ipfs_v2_stream_write(ipfs_v2_stream_bridge_t *bridge, const void *buf, size_t count)
{
    if (!bridge || !bridge->yamux_stream || !bridge->connected)
        return -EINVAL;

    return bridge->yamux_stream->write(bridge->yamux_stream, (const unsigned char *)buf, count);
}

ssize_t ipfs_v2_stream_read(ipfs_v2_stream_bridge_t *bridge, void *buf, size_t count)
{
    if (!bridge || !bridge->connected)
        return -EINVAL;

    /* Serve the buffered Identify response first. */
    if (bridge->identify_pos < bridge->identify_len) {
        size_t avail = bridge->identify_len - bridge->identify_pos;
        size_t to_copy = count < avail ? count : avail;
        memcpy(buf, bridge->identify_buf + bridge->identify_pos, to_copy);
        bridge->identify_pos += to_copy;
        return (ssize_t)to_copy;
    }

    if (!bridge->yamux_stream)
        return -EINVAL;

    return bridge->yamux_stream->read(bridge->yamux_stream, (unsigned char *)buf, count);
}

void ipfs_v2_stream_free(ipfs_v2_stream_bridge_t *bridge)
{
    if (!bridge)
        return;

    if (bridge->yamux_stream)
        bridge->yamux_stream->close(bridge->yamux_stream);

    if (bridge->yamux_session)
        libp2p_yamux_session_free(bridge->yamux_session);

    free(bridge);
}
