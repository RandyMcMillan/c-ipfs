#ifndef TEST_DNSADDR_RESOLVER_H
#define TEST_DNSADDR_RESOLVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <resolv.h>

#include "ipfs/transport/dnsaddr_resolver.h"

static size_t encode_dns_name(const char *name, uint8_t *out, size_t out_len) {
	size_t off = 0;
	const char *p = name;
	while (*p) {
		const char *dot = strchr(p, '.');
		size_t label_len = dot ? (size_t)(dot - p) : strlen(p);
		if (label_len > 63 || off + 1 + label_len > out_len)
			return 0;
		out[off++] = (uint8_t)label_len;
		memcpy(out + off, p, label_len);
		off += label_len;
		if (!dot)
			break;
		p = dot + 1;
	}
	if (off >= out_len)
		return 0;
	out[off++] = 0;
	return off;
}

static size_t build_txt_response(const char *query_name, const char **txts, size_t txt_count, uint8_t *out, size_t out_len) {
	if (out_len < 12)
		return 0;
	size_t off = 0;
	out[off++] = 0x00; out[off++] = 0x00;
	out[off++] = 0x81; out[off++] = 0x80;
	out[off++] = 0x00; out[off++] = 0x01;
	out[off++] = (uint8_t)((txt_count >> 8) & 0xFF); out[off++] = (uint8_t)(txt_count & 0xFF);
	out[off++] = 0x00; out[off++] = 0x00;
	out[off++] = 0x00; out[off++] = 0x00;

	size_t qname_len = encode_dns_name(query_name, out + off, out_len - off);
	if (qname_len == 0)
		return 0;
	off += qname_len;
	if (off + 4 > out_len)
		return 0;
	out[off++] = 0x00; out[off++] = 16;
	out[off++] = 0x00; out[off++] = 1;

	for (size_t i = 0; i < txt_count; i++) {
		const char *txt = txts[i];
		size_t txt_len = strlen(txt);
		if (off + 12 > out_len)
			return 0;
		out[off++] = 0xC0; out[off++] = 12;
		out[off++] = 0x00; out[off++] = 16;
		out[off++] = 0x00; out[off++] = 1;
		out[off++] = 0x00; out[off++] = 0x00; out[off++] = 0x00; out[off++] = 0x00;
		size_t rdlen = 1 + txt_len;
		out[off++] = (uint8_t)((rdlen >> 8) & 0xFF);
		out[off++] = (uint8_t)(rdlen & 0xFF);
		if (off + 1 + txt_len > out_len)
			return 0;
		out[off++] = (uint8_t)txt_len;
		memcpy(out + off, txt, txt_len);
		off += txt_len;
	}
	return off;
}

#define MAX_MOCK_DNS_RESPONSES 8

struct mock_dns_response {
	char name[256];
	uint8_t *answer;
	int len;
};

static struct mock_dns_response mock_dns_responses[MAX_MOCK_DNS_RESPONSES];
static int mock_dns_response_count = 0;
static char mock_dns_last_query[256];
static char mock_getaddrinfo_host[256];
static char mock_getaddrinfo_ip[64];

static void mock_dns_reset(void) {
	for (int i = 0; i < mock_dns_response_count; i++) {
		free(mock_dns_responses[i].answer);
		mock_dns_responses[i].answer = NULL;
		mock_dns_responses[i].len = 0;
		mock_dns_responses[i].name[0] = '\0';
	}
	mock_dns_response_count = 0;
	mock_dns_last_query[0] = '\0';
	mock_getaddrinfo_host[0] = '\0';
	mock_getaddrinfo_ip[0] = '\0';
}

static int mock_dns_add_response(const char *query_name, const uint8_t *answer, int len) {
	if (mock_dns_response_count >= MAX_MOCK_DNS_RESPONSES)
		return 0;
	struct mock_dns_response *resp = &mock_dns_responses[mock_dns_response_count++];
	snprintf(resp->name, sizeof(resp->name), "%s", query_name);
	resp->answer = (uint8_t*)malloc(len);
	if (!resp->answer) {
		mock_dns_response_count--;
		return 0;
	}
	memcpy(resp->answer, answer, len);
	resp->len = len;
	return 1;
}

static int mock_res_query(const char *dname, int class, int type, unsigned char *answer, int anslen) {
	(void)class; (void)type;
	snprintf(mock_dns_last_query, sizeof(mock_dns_last_query), "%s", dname);
	for (int i = 0; i < mock_dns_response_count; i++) {
		if (strcmp(mock_dns_responses[i].name, dname) == 0) {
			int copy_len = mock_dns_responses[i].len < anslen ? mock_dns_responses[i].len : anslen;
			memcpy(answer, mock_dns_responses[i].answer, copy_len);
			return copy_len;
		}
	}
	return -1;
}

static int mock_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	(void)service; (void)hints;
	snprintf(mock_getaddrinfo_host, sizeof(mock_getaddrinfo_host), "%s", node);
	struct addrinfo *ai = (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
	struct sockaddr_in *sin = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
	if (!ai || !sin) {
		free(ai);
		free(sin);
		return EAI_MEMORY;
	}
	sin->sin_family = AF_INET;
	if (mock_getaddrinfo_ip[0] != '\0') {
		inet_pton(AF_INET, mock_getaddrinfo_ip, &sin->sin_addr);
	} else {
		inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
	}
	ai->ai_family = AF_INET;
	ai->ai_socktype = SOCK_STREAM;
	ai->ai_addrlen = sizeof(struct sockaddr_in);
	ai->ai_addr = (struct sockaddr*)sin;
	ai->ai_next = NULL;
	*res = ai;
	return 0;
}

static void mock_freeaddrinfo(struct addrinfo *res) {
	while (res) {
		struct addrinfo *next = res->ai_next;
		free(res->ai_addr);
		free(res);
		res = next;
	}
}

static int has_result(char **results, size_t count, const char *wanted) {
	for (size_t i = 0; i < count; i++) {
		if (results[i] && strcmp(results[i], wanted) == 0)
			return 1;
	}
	return 0;
}

int test_dnsaddr_resolver_single_txt(void) {
	int retVal = 0;
	char **results = NULL;
	size_t count = 0;
	uint8_t answer[512];
	const char *txts[] = { "dnsaddr=/ip4/1.2.3.4/tcp/4001/p2p/QmTestPeerId" };
	size_t len = build_txt_response("_dnsaddr.example.com", txts, 1, answer, sizeof(answer));
	if (len == 0)
		return 0;

	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(mock_res_query);
	ipfs_dnsaddr_resolver_set_getaddrinfo(mock_getaddrinfo);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(mock_freeaddrinfo);
	mock_dns_add_response("_dnsaddr.example.com", answer, (int)len);

	if (ipfs_resolve_dnsaddr("/dnsaddr/example.com", &results, &count) != 0)
		goto exit;
	if (count != 1 || !has_result(results, count, "/ip4/1.2.3.4/tcp/4001/p2p/QmTestPeerId"))
		goto exit;
	retVal = 1;

exit:
	ipfs_free_resolved_multiaddrs(results, count);
	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(NULL);
	ipfs_dnsaddr_resolver_set_getaddrinfo(NULL);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(NULL);
	return retVal;
}

int test_dnsaddr_resolver_recursion(void) {
	int retVal = 0;
	char **results = NULL;
	size_t count = 0;
	uint8_t outer[512], inner[512];
	const char *outer_txts[] = { "dnsaddr=/dnsaddr/inner.example.com" };
	const char *inner_txts[] = { "dnsaddr=/ip4/5.6.7.8/tcp/4001/p2p/QmInnerPeer" };
	size_t outer_len = build_txt_response("_dnsaddr.example.com", outer_txts, 1, outer, sizeof(outer));
	size_t inner_len = build_txt_response("_dnsaddr.inner.example.com", inner_txts, 1, inner, sizeof(inner));
	if (outer_len == 0 || inner_len == 0)
		return 0;

	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(mock_res_query);
	ipfs_dnsaddr_resolver_set_getaddrinfo(mock_getaddrinfo);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(mock_freeaddrinfo);
	mock_dns_add_response("_dnsaddr.example.com", outer, (int)outer_len);
	mock_dns_add_response("_dnsaddr.inner.example.com", inner, (int)inner_len);

	if (ipfs_resolve_dnsaddr("/dnsaddr/example.com", &results, &count) != 0)
		goto exit;
	if (count != 1 || !has_result(results, count, "/ip4/5.6.7.8/tcp/4001/p2p/QmInnerPeer"))
		goto exit;
	retVal = 1;

exit:
	ipfs_free_resolved_multiaddrs(results, count);
	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(NULL);
	ipfs_dnsaddr_resolver_set_getaddrinfo(NULL);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(NULL);
	return retVal;
}

int test_dnsaddr_resolver_dns4_materialize(void) {
	int retVal = 0;
	char **results = NULL;
	size_t count = 0;
	uint8_t answer[512];
	const char *txts[] = { "dnsaddr=/dns4/node.example.com/tcp/4001/p2p/QmNodePeer" };
	size_t len = build_txt_response("_dnsaddr.example.com", txts, 1, answer, sizeof(answer));
	if (len == 0)
		return 0;

	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(mock_res_query);
	ipfs_dnsaddr_resolver_set_getaddrinfo(mock_getaddrinfo);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(mock_freeaddrinfo);
	mock_dns_add_response("_dnsaddr.example.com", answer, (int)len);
	snprintf(mock_getaddrinfo_ip, sizeof(mock_getaddrinfo_ip), "9.8.7.6");

	if (ipfs_resolve_dnsaddr("/dnsaddr/example.com", &results, &count) != 0)
		goto exit;
	if (count != 1 || !has_result(results, count, "/ip4/9.8.7.6/tcp/4001/p2p/QmNodePeer"))
		goto exit;
	if (strcmp(mock_getaddrinfo_host, "node.example.com") != 0)
		goto exit;
	retVal = 1;

exit:
	ipfs_free_resolved_multiaddrs(results, count);
	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(NULL);
	ipfs_dnsaddr_resolver_set_getaddrinfo(NULL);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(NULL);
	return retVal;
}

int test_dnsaddr_resolver_no_records(void) {
	int retVal = 0;
	char **results = NULL;
	size_t count = 0;
	uint8_t answer[512];
	const char *txts[] = { "foo=bar" };
	size_t len = build_txt_response("_dnsaddr.example.com", txts, 1, answer, sizeof(answer));
	if (len == 0)
		return 0;

	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(mock_res_query);
	ipfs_dnsaddr_resolver_set_getaddrinfo(mock_getaddrinfo);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(mock_freeaddrinfo);
	mock_dns_add_response("_dnsaddr.example.com", answer, (int)len);

	if (ipfs_resolve_dnsaddr("/dnsaddr/example.com", &results, &count) == 0)
		goto exit;
	retVal = 1;

exit:
	ipfs_free_resolved_multiaddrs(results, count);
	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(NULL);
	ipfs_dnsaddr_resolver_set_getaddrinfo(NULL);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(NULL);
	return retVal;
}

int test_dnsaddr_resolver_missing_dnsaddr_prefix(void) {
	int retVal = 0;
	char **results = NULL;
	size_t count = 0;
	uint8_t answer[512];
	const char *txts[] = { "/ip4/1.2.3.4/tcp/4001/p2p/QmX" };
	size_t len = build_txt_response("_dnsaddr.example.com", txts, 1, answer, sizeof(answer));
	if (len == 0)
		return 0;

	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(mock_res_query);
	ipfs_dnsaddr_resolver_set_getaddrinfo(mock_getaddrinfo);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(mock_freeaddrinfo);
	mock_dns_add_response("_dnsaddr.example.com", answer, (int)len);

	if (ipfs_resolve_dnsaddr("/dnsaddr/example.com", &results, &count) == 0)
		goto exit;
	retVal = 1;

exit:
	ipfs_free_resolved_multiaddrs(results, count);
	mock_dns_reset();
	ipfs_dnsaddr_resolver_set_res_query(NULL);
	ipfs_dnsaddr_resolver_set_getaddrinfo(NULL);
	ipfs_dnsaddr_resolver_set_freeaddrinfo(NULL);
	return retVal;
}

#endif
