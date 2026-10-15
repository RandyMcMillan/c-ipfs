#include "../test_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "ipfs/core/api.h"
#include "ipfs/core/ipfs_node.h"
#include "ipfs/importer/importer.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/namesys/publisher.h"
#include "ipfs/dnslink/dnslink.h"
#include "ipfs/cid/cid.h"

static char* test_dnslink_expected_path = NULL;

static int test_dnslink_mock_lookup(char*** txt, char* name)
{
    (void)name;
    if (!test_dnslink_expected_path)
        return 1;
    char* record = malloc(strlen(test_dnslink_expected_path) + 10);
    if (!record)
        return 1;
    sprintf(record, "dnslink=%s", test_dnslink_expected_path);
    *txt = calloc(2, sizeof(char*));
    if (!*txt) {
        free(record);
        return 1;
    }
    (*txt)[0] = record;
    (*txt)[1] = NULL;
    return 0;
}

int test_api_gateway_cat(void) {
    const char* ipfs_path = "./tmp/test_api_gateway";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src = "./tmp/test_api_gateway_src.txt";
    FILE* f = fopen(src, "w");
    if (!f) return 0;
    fputs("gateway-content", f);
    fclose(f);

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src, &node, local_node, &bytes_written, 0);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);

    char path[320] = "";
    snprintf(path, sizeof(path), "/ipfs/%s", hash);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_hashtable_node_free(node);
        ipfs_node_free(local_node);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway(sv[1], path, local_node);

    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = 0;
    if (ret && strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "gateway-content")) {
        ok = 1;
    }

    ipfs_hashtable_node_free(node);
    ipfs_node_free(local_node);
    return ok;
}


int test_api_gateway_ipns(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_ipns";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src = "./tmp/test_api_gateway_ipns_src.txt";
    FILE* f = fopen(src, "w");
    if (!f) return 0;
    fputs("gateway-ipns-content", f);
    fclose(f);

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        if (peer_id) free(peer_id);
        return 0;
    }
    if (!peer_id || strlen(peer_id) == 0) {
        if (peer_id) free(peer_id);
        return 0;
    }

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        free(peer_id);
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src, &node, local_node, &bytes_written, 0);
    if (!node) {
        ipfs_node_free(local_node);
        free(peer_id);
        return 0;
    }
    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path_prefix[640];
    snprintf(path_prefix, sizeof(path_prefix), "/ipfs/%s", hash);
    if (!ipfs_namesys_publisher_publish(local_node, path_prefix)) {
        fprintf(stderr, "PUBLISH FAIL\n");
        ipfs_node_free(local_node);
        free(peer_id);
        return 0;
    }

    char gateway_path[512];
    snprintf(gateway_path, sizeof(gateway_path), "/ipns/%s", peer_id);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        free(peer_id);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway(sv[1], gateway_path, local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    ipfs_node_free(local_node);
    free(peer_id);

    int ok = ret && strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "gateway-ipns-content");
    return ok;
}

int test_api_gateway_directory_redirect(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_dir_redirect";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src_dir = "./tmp/test_api_gateway_dir_redirect_src";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", src_dir, src_dir);
        system(cmd);
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/file.txt", src_dir);
        FILE* f = fopen(file_path, "w");
        if (!f) return 0;
        fputs("directory-file-content", f);
        fclose(f);
    }

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src_dir, &node, local_node, &bytes_written, 1);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path[320] = "";
    snprintf(path, sizeof(path), "/ipfs/%s", hash);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway(sv[1], path, local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = 0;
    if (ret && strstr(buf, "HTTP/1.1 301") && strstr(buf, "Moved Permanently")) {
        char expected[340];
        snprintf(expected, sizeof(expected), "Location: %s/", path);
        if (strstr(buf, expected)) {
            ok = 1;
        }
    }

    ipfs_node_free(local_node);
    return ok;
}

int test_api_gateway_directory_index(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_dir_index";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src_dir = "./tmp/test_api_gateway_dir_index_src";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", src_dir, src_dir);
        system(cmd);
        char index_path[512];
        snprintf(index_path, sizeof(index_path), "%s/index.html", src_dir);
        FILE* f = fopen(index_path, "w");
        if (!f) return 0;
        fputs("<h1>index-html-content</h1>", f);
        fclose(f);
    }

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src_dir, &node, local_node, &bytes_written, 1);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path[340] = "";
    snprintf(path, sizeof(path), "/ipfs/%s/", hash);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway(sv[1], path, local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = ret && strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "index-html-content");

    ipfs_node_free(local_node);
    return ok;
}

int test_api_gateway_format_raw(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_raw";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src = "./tmp/test_api_gateway_raw_src.txt";
    FILE* f = fopen(src, "w");
    if (!f) return 0;
    fputs("raw-block-content", f);
    fclose(f);

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src, &node, local_node, &bytes_written, 0);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path[320] = "";
    snprintf(path, sizeof(path), "/ipfs/%s", hash);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway_ex(sv[1], path, "format=raw", NULL, 0, local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = ret && strstr(buf, "HTTP/1.1 200 OK") &&
             strstr(buf, "Content-Type: application/vnd.ipld.raw") &&
             strstr(buf, "raw-block-content");

    ipfs_node_free(local_node);
    return ok;
}

int test_api_gateway_head(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_head";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src = "./tmp/test_api_gateway_head_src.txt";
    FILE* f = fopen(src, "w");
    if (!f) return 0;
    fputs("head-content", f);
    fclose(f);

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src, &node, local_node, &bytes_written, 0);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path[320] = "";
    snprintf(path, sizeof(path), "/ipfs/%s", hash);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway_ex(sv[1], path, NULL, NULL, 1, local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = ret && strstr(buf, "HTTP/1.1 200 OK") &&
             strstr(buf, "Accept-Ranges: bytes") &&
             strstr(buf, "ETag:") &&
             strstr(buf, "X-Ipfs-Path:") &&
             strstr(buf, "X-Ipfs-Roots:") &&
             !strstr(buf, "head-content");

    ipfs_node_free(local_node);
    return ok;
}

int test_api_gateway_dnslink(void) {
    const char* ipfs_path = "./tmp/test_api_gateway_dnslink";
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", ipfs_path);
        system(cmd);
    }

    const char* src = "./tmp/test_api_gateway_dnslink_src.txt";
    FILE* f = fopen(src, "w");
    if (!f) return 0;
    fputs("dnslink-content", f);
    fclose(f);

    char* peer_id = NULL;
    if (!drop_and_build_repository_without_bootstrap(ipfs_path, 0, &peer_id)) {
        fprintf(stderr, "DROP FAIL\n");
        return 0;
    }
    if (peer_id) free(peer_id);

    struct IpfsNode* local_node = NULL;
    if (!ipfs_node_offline_new(ipfs_path, &local_node)) {
        fprintf(stderr, "OFFLINE FAIL\n");
        return 0;
    }
    size_t bytes_written = 0;
    struct HashtableNode* node = NULL;
    ipfs_import_file(NULL, src, &node, local_node, &bytes_written, 0);
    if (!node) {
        ipfs_node_free(local_node);
        return 0;
    }

    char hash[256] = "";
    ipfs_cid_hash_to_base58(node->hash, node->hash_size, (unsigned char*)hash, 256);
    ipfs_hashtable_node_free(node);

    char path_buf[320];
    snprintf(path_buf, sizeof(path_buf), "/ipfs/%s", hash);
    test_dnslink_expected_path = path_buf;

    int (*saved_lookup)(char***, char*) = ipfs_dnslink_lookup_txt;
    ipfs_dnslink_lookup_txt = test_dnslink_mock_lookup;

    char* resolved = NULL;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        ipfs_node_free(local_node);
        test_dnslink_expected_path = NULL;
        ipfs_dnslink_lookup_txt = saved_lookup;
        return 0;
    }

    int ret = ipfs_core_api_serve_gateway(sv[1], "/ipns/example.com", local_node);
    close(sv[1]);
    shutdown(sv[0], SHUT_WR);

    char buf[4096] = "";
    size_t total = 0;
    for (;;) {
        if (total >= sizeof(buf) - 1) break;
        ssize_t n = read(sv[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sv[0]);

    int ok = ret && strstr(buf, "HTTP/1.1 200 OK") && strstr(buf, "dnslink-content");

    ipfs_node_free(local_node);
    test_dnslink_expected_path = NULL;
    ipfs_dnslink_lookup_txt = saved_lookup;
    return ok;
}
