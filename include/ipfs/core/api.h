#pragma once

#include <pthread.h>
#include "ipfs/core/ipfs_node.h"

#ifdef __x86_64__
	#define INT_TYPE uint64_t
#else
	#define INT_TYPE uint32_t
#endif

#define MAX_READ (32*1024) // 32k
#define MAX_CHUNK (32*1024) // 32k

struct ApiContext {
	int socket;
	uint32_t ipv4;
	uint16_t port;
	int max_conns;
	int timeout;
	pthread_mutex_t conns_lock;
	int conns_count;
	pthread_t api_thread;
	struct s_conns {
		int socket;
		uint32_t ipv4;
		uint16_t port;
		pthread_t pthread;
	} **conns;
};

struct s_request {
	char *buf;
	size_t size;

	int method;
	int path;
	int request;
	int query;
	int http_ver;
	int header;
	int body;
	size_t body_size;
	int boundary;
	size_t boundary_size;
};

#define API_V0_START	"/api/v0/"

#define WEBUI_ADDR	"/ipfs/QmPhnvn747LqwPYMJmQVorMaGbMSgA7mRRoyyZYz3DoZRQ/"

#define HTTP_301	"HTTP/1.1 301 Moved Permanently\r\n" \
			"Location: %s\r\n" \
			"Content-Type: text/html\r\n\r\n" \
			"<a href=\"%s\">Moved Permanently</a>.\r\n\r\n"

#define HTTP_302	"HTTP/1.1 302 Found\r\n" \
			"Content-Type: text/html\r\n" \
			"Connection: close\r\n" \
			"Location: %s\r\n" \
			"X-Ipfs-Path: %s\r\n\r\n" \
			"<a href=\"%s\">Found</a>.\r\n\r\n"

#define HTTP_400	"HTTP/1.1 400 Bad Request\r\n" \
			"Content-Type: text/plain\r\n" \
			"Connection: close\r\n\r\n" \
			"400 Bad Request"

#define HTTP_404	"HTTP/1.1 404 Not Found\r\n" \
			"Content-Type: text/plain\r\n" \
			"Connection: close\r\n\r\n" \
			"404 page not found"

#define HTTP_500	"HTTP/1.1 500 Internal server error\r\n" \
			"Content-Type: text/plain\r\n" \
			"Connection: close\r\n\r\n" \
			"500 Internal server error"

#define HTTP_501	"HTTP/1.1 501 Not Implemented\r\n" \
			"Content-Type: text/plain\r\n" \
			"Connection: close\r\n\r\n" \
			"501 Not Implemented"

#define write_cstr(f,s)	write(f,s,sizeof(s)-1)
#define write_str(f,s)	write(f,s,strlen(s))

#define cstrstart(a,b) (memcmp(a,b,sizeof(b)-1)==0)
#define strstart(a,b) (memcmp(a,b,strlen(b))==0)

int api_send_resp_chunks(int fd, void *buf, size_t size);
void *api_connection_thread (void *ptr);
void api_connections_cleanup (struct IpfsNode* node);
void *api_listen_thread (void *ptr);
int api_start (struct IpfsNode* local_node, int max_conns, int timeout);
int ipfs_core_api_serve_gateway(int fd, const char* path, struct IpfsNode* local_node);
int api_stop (struct IpfsNode* local_node);
