#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "secp256k1.h"
#include "secp256k1_schnorrsig.h"

#include "ipfs/nostr/event.h"
#include "ipfs/nostr/kind.h"
#include "ipfs/nostr/tag.h"

#include "sha256.h"
#include "hex.h"
#include "random.h"

static int push_str(char **p, char *end, const char *s)
{
    size_t len = strlen(s);
    if (*p + len >= end) return 0;
    memcpy(*p, s, len);
    *p += len;
    return 1;
}

static int push_json_str(char **p, char *end, const char *str)
{
    size_t i, len = strlen(str);
    if (*p + len + 2 >= end) return 0;
    *(*p)++ = '"';
    for (i = 0; i < len; i++) {
        char c = str[i];
        switch (c) {
        case '"':  if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = '"'; break;
        case '\\': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = '\\'; break;
        case '\b': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = 'b';  break;
        case '\f': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = 'f';  break;
        case '\n': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = 'n';  break;
        case '\r': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = 'r';  break;
        case '\t': if (*p + 2 >= end) return 0; *(*p)++ = '\\'; *(*p)++ = 't';  break;
        default:   *(*p)++ = c; break;
        }
    }
    *(*p)++ = '"';
    return 1;
}

static int push_hex(char **p, char *end, unsigned char *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (*p + len * 2 >= end) return 0;
    for (i = 0; i < len; i++) {
        *(*p)++ = hex[data[i] >> 4];
        *(*p)++ = hex[data[i] & 0x0f];
    }
    return 1;
}

void* nostr_context_new(void)
{
    unsigned char randomize[32];
    secp256k1_context *ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return NULL;
    if (!fill_random(randomize, sizeof(randomize))) {
        secp256k1_context_destroy(ctx);
        return NULL;
    }
    (void)secp256k1_context_randomize(ctx, randomize);
    return ctx;
}

void nostr_context_free(void *ctx)
{
    if (ctx) secp256k1_context_destroy((secp256k1_context*)ctx);
}

int nostr_key_generate(void *ctx, struct NostrKey *key)
{
    secp256k1_keypair pair;
    secp256k1_xonly_pubkey pubkey;
    if (!fill_random(key->seckey, 32)) return 0;
    if (!secp256k1_keypair_create((secp256k1_context*)ctx, &pair, key->seckey))
        return 0;
    if (!secp256k1_keypair_xonly_pub((secp256k1_context*)ctx, &pubkey, NULL, &pair))
        return 0;
    return secp256k1_xonly_pubkey_serialize((secp256k1_context*)ctx, key->pubkey, &pubkey);
}

int nostr_key_from_hex(void *ctx, const char *hex, struct NostrKey *key)
{
    secp256k1_keypair pair;
    secp256k1_xonly_pubkey pubkey;
    if (!hex_decode(hex, strlen(hex), key->seckey, 32)) return 0;
    if (!secp256k1_keypair_create((secp256k1_context*)ctx, &pair, key->seckey))
        return 0;
    if (!secp256k1_keypair_xonly_pub((secp256k1_context*)ctx, &pubkey, NULL, &pair))
        return 0;
    return secp256k1_xonly_pubkey_serialize((secp256k1_context*)ctx, key->pubkey, &pubkey);
}

void ipfs_nostr_event_init(struct NostrEvent *ev)
{
    memset(ev, 0, sizeof(*ev));
    ev->created_at = (uint64_t)time(NULL);
    ev->kind = NOSTR_KIND_TEXT_NOTE;
    ipfs_nostr_tags_init(&ev->tags);
}

void ipfs_nostr_event_set_content(struct NostrEvent *ev, const char *content)
{
    strncpy(ev->content, content, sizeof(ev->content) - 1);
    ev->content[sizeof(ev->content) - 1] = '\0';
}

void ipfs_nostr_event_set_kind(struct NostrEvent *ev, int kind)
{
    ev->kind = kind;
}

int ipfs_nostr_event_commit(struct NostrEvent *ev, unsigned char *buf, size_t buflen)
{
    char pubkey_hex[65];
    char tags_json[16384];
    char *p = (char*)buf;
    char *end = (char*)buf + buflen;
    int n;

    if (!hex_encode(ev->pubkey, 32, pubkey_hex, sizeof(pubkey_hex)))
        return 0;
    if (!ipfs_nostr_tags_to_json(&ev->tags, tags_json, sizeof(tags_json)))
        return 0;

    n = snprintf(p, end - p, "[0,\"%s\",%" PRIu64 ",%d,%s,",
                 pubkey_hex, ev->created_at, ev->kind, tags_json);
    if (n < 0 || (size_t)n >= (size_t)(end - p))
        return 0;
    p += n;

    if (!push_json_str(&p, end, ev->content)) return 0;
    if (p + 2 >= end) return 0;
    *p++ = ']';
    *p = '\0';

    sha256((struct sha256*)ev->id, (unsigned char*)buf, p - (char*)buf);
    return 1;
}

int ipfs_nostr_event_sign(void *ctx, struct NostrKey *key, struct NostrEvent *ev)
{
    secp256k1_keypair pair;
    unsigned char aux[32];
    if (!secp256k1_keypair_create((secp256k1_context*)ctx, &pair, key->seckey))
        return 0;
    if (!fill_random(aux, sizeof(aux))) return 0;
    return secp256k1_schnorrsig_sign32((secp256k1_context*)ctx, ev->sig, ev->id, &pair, aux);
}

int ipfs_nostr_event_to_json(struct NostrEvent *ev, char *buf, size_t buflen)
{
    char *p = buf;
    char *end = buf + buflen;

    if (!push_str(&p, end, "{\"id\":\"")) return 0;
    if (!push_hex(&p, end, ev->id, 32)) return 0;
    if (!push_str(&p, end, "\",\"pubkey\":\"")) return 0;
    if (!push_hex(&p, end, ev->pubkey, 32)) return 0;
    if (p + 64 >= end) return 0;
    p += sprintf(p, "\",\"created_at\":%" PRIu64 ",\"kind\":%d,\"tags\":",
                 ev->created_at, ev->kind);
    if (!ipfs_nostr_tags_to_json(&ev->tags, p, end - p)) return 0;
    p += strlen(p);
    if (!push_str(&p, end, ",\"content\":")) return 0;
    if (!push_json_str(&p, end, ev->content)) return 0;
    if (!push_str(&p, end, ",\"sig\":\"")) return 0;
    if (!push_hex(&p, end, ev->sig, 64)) return 0;
    if (!push_str(&p, end, "\"}")) return 0;
    return 1;
}

int ipfs_nostr_event_to_envelope_json(struct NostrEvent *ev, char *buf, size_t buflen)
{
    if (buflen < 10) return 0;
    strcpy(buf, "[\"EVENT\",");
    if (!ipfs_nostr_event_to_json(ev, buf + 9, buflen - 9)) return 0;
    strcat(buf, "]");
    return 1;
}

void ipfs_nostr_event_print(struct NostrEvent *ev)
{
    char buf[32768];
    if (ipfs_nostr_event_to_json(ev, buf, sizeof(buf)))
        printf("%s\n", buf);
}

int ipfs_nostr_event_verify(void *ctx, struct NostrEvent *ev)
{
    secp256k1_xonly_pubkey pubkey;
    unsigned char check_id[32];
    unsigned char buf[32768];
    if (!ipfs_nostr_event_commit(ev, buf, sizeof(buf))) return 0;
    memcpy(check_id, ev->id, 32);
    if (memcmp(check_id, ev->id, 32) != 0) return 0;
    if (!secp256k1_xonly_pubkey_parse((secp256k1_context*)ctx, &pubkey, ev->pubkey)) return 0;
    return secp256k1_schnorrsig_verify((secp256k1_context*)ctx, ev->sig, ev->id, 32, &pubkey);
}

int ipfs_nostr_event_make_ipfs_content(void *ctx, struct NostrKey *key,
                                   const char *cid, const char *description,
                                   struct NostrEvent *ev)
{
    unsigned char buf[32768];
    ipfs_nostr_event_init(ev);
    ev->kind = NOSTR_KIND_IPFS_CONTENT;
    ipfs_nostr_event_set_content(ev, description ? description : cid);
    if (!ipfs_nostr_tags_add_cid(&ev->tags, cid))
        return 0;
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!ipfs_nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return ipfs_nostr_event_sign(ctx, key, ev);
}

int ipfs_nostr_event_make_ipfs_provider(void *ctx, struct NostrKey *key,
                                    const char *cid, const char *multiaddr,
                                    struct NostrEvent *ev)
{
    unsigned char buf[32768];
    ipfs_nostr_event_init(ev);
    ev->kind = NOSTR_KIND_IPFS_PROVIDER;
    ipfs_nostr_event_set_content(ev, multiaddr ? multiaddr : "");
    if (!ipfs_nostr_tags_add_cid(&ev->tags, cid))
        return 0;
    if (multiaddr && !ipfs_nostr_tags_add(&ev->tags, "multiaddr", multiaddr))
        return 0;
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!ipfs_nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return ipfs_nostr_event_sign(ctx, key, ev);
}

int ipfs_nostr_event_make_ipfs_pin_request(void *ctx, struct NostrKey *key,
                                       const char *cid, const char *relay_hint,
                                       struct NostrEvent *ev)
{
    unsigned char buf[32768];
    ipfs_nostr_event_init(ev);
    ev->kind = NOSTR_KIND_IPFS_PIN_REQUEST;
    ipfs_nostr_event_set_content(ev, relay_hint ? relay_hint : "");
    if (!ipfs_nostr_tags_add_cid(&ev->tags, cid))
        return 0;
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!ipfs_nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return ipfs_nostr_event_sign(ctx, key, ev);
}

int ipfs_nostr_event_make_ipfs_pin_confirm(void *ctx, struct NostrKey *key,
                                       const char *cid, const char *request_event_id,
                                       struct NostrEvent *ev)
{
    unsigned char buf[32768];
    ipfs_nostr_event_init(ev);
    ev->kind = NOSTR_KIND_IPFS_PIN_CONFIRM;
    ipfs_nostr_event_set_content(ev, "");
    if (!ipfs_nostr_tags_add_cid(&ev->tags, cid))
        return 0;
    if (request_event_id && !ipfs_nostr_tags_add_event_ref(&ev->tags, request_event_id))
        return 0;
    memcpy(ev->pubkey, key->pubkey, 32);
    if (!ipfs_nostr_event_commit(ev, buf, sizeof(buf)))
        return 0;
    return ipfs_nostr_event_sign(ctx, key, ev);
}
