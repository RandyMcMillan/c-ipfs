/**
 * Methods for lightweight/specific HTTP for API communication.
 */
#define _GNU_SOURCE
#define __USE_GNU
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#include <fcntl.h>

#include "libp2p/net/p2pnet.h"
#include "libp2p/os/memstream.h"
#include "libp2p/utils/logger.h"
#include "libp2p/utils/urlencode.h"
#include "ipfs/core/api.h"
#include "ipfs/cid/cid.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/core/http_request.h"

/* Forward declaration from core/api_gzip.c */
int api_handle_post_request(const char *content_encoding, const unsigned char *body, size_t body_len);

//pthread_mutex_t conns_lock;
//int conns_count;

struct ApiContext api_list;

/**
 * Write two strings on one write.
 * @param fd file descriptor to write.
 * @param str1 first string to write.
 * @param str2 second string to write.
 */
size_t write_dual(int fd, char *str1, char *str2)
{
	struct iovec iov[2];

	iov[0].iov_base = str1;
	iov[0].iov_len = strlen(str1);
	iov[1].iov_base = str2;
	iov[1].iov_len = strlen(str2);

	return writev(fd, iov, 2);
}

int find_chunk(char *buf, const size_t buf_size, size_t *pos, size_t *size)
{
	char *p = NULL;

	*size = strtol(buf, &p, 16);
	if (!p || p < buf || p > (buf + 10)) {
		return 0;
	}
	*pos = (int)(p - buf);
	if (p[0] == '\r' && p[1] == '\n') {
		*pos += 2;
		return 1;
	}
	return 0;
}

int read_chunked(int fd, struct s_request *req, char *already, size_t already_size)
{
	char buf[MAX_READ], *p;
	size_t pos, nsize, buf_size = 0, r;

	if (already_size > 0) {
		if (already_size <= sizeof(buf)) {
			memcpy(buf, already, already_size);
			buf_size += already_size;
			already_size = 0;
		} else {
			memcpy(buf, already, sizeof(buf));
			already += sizeof(buf);
			buf_size += sizeof(buf);
			already_size -= sizeof(buf);
		}
	}

	while(buf_size) {
		if (!find_chunk(buf, buf_size, &pos, &nsize)) {
			libp2p_logger_error("api", "fail find_chunk.\n");
			libp2p_logger_error("api", "nsize = %d.\n", nsize);
			return 0;
		}
		if (nsize == 0) {
			break;
		}
		p = realloc(req->buf, req->size + nsize);
		if (!p) {
			libp2p_logger_error("api", "fail realloc.\n");
			return 0;
		}
		req->buf = p;
		req->size += nsize;

CPCHUNK:
		r = nsize;
		buf_size -= pos;
		if (r > buf_size) {
			r = buf_size;
		}
		memcpy(req->buf + req->body + req->body_size, buf + pos, r);
		req->body_size += r;
		nsize -= r;
		buf_size -= r;
		if (buf_size > 0) {
			memmove(buf, buf + pos + r, buf_size);
		}
		pos = 0;
		if (already_size > 0) {
			r = sizeof(buf) - buf_size;
			if (already_size <= r) {
				memcpy(buf, already, already_size);
				buf_size += already_size;
				already_size = 0;
			} else {
				memcpy(buf, already, r);
				already += r;
				buf_size += r;
				already_size -= r;
			}
		}

		if (socket_read_select4(fd, 5) > 0) {
			r = sizeof(buf) - buf_size;
			r = read(fd, buf+buf_size, r);
			buf_size += r;
			if (r == 0 && nsize == 0) {
				break;
			}

			if (r <= 0) {
				libp2p_logger_error("api", "read fail.\n");
				return 0;
			}
		}

		if (nsize > 0)
			goto CPCHUNK; // still have data to transfer on current chunk.

		if (memcmp (buf, "\r\n", 2)!=0) {
			libp2p_logger_error("api", "fail CRLF.\n");
			return 0;
		}
	}
	return 1;
}

int read_all(int fd, struct s_request *req, char *already, size_t alread_size)
{
	char buf[MAX_READ], *p;
	size_t size = 0;

	if (alread_size > 0) {
		p = realloc(req->buf, req->size + alread_size);
		if (!p) {
			return 0;
		}
		req->buf = p;
		req->size += alread_size;
		memcpy(req->buf + req->body + req->body_size, already, alread_size);
		req->body_size += alread_size;
	}
	for(;;) {
		if (socket_read_select4(fd, 5) <= 0) {
			break;
		}
		size = read(fd, buf, sizeof buf);
		if (size <= 0) {
			break;
		}
		p = realloc(req->buf, req->size + size);
		if (!p) {
			return 0;
		}
		req->buf = p;
		req->size += size;
		memcpy(req->buf + req->body + req->body_size, buf, size);
		req->body_size += size;
	}
	return 1;
}

/**
 * Find a token in a string array.
 * @param string array and token string.
 * @returns the pointer after where the token was found or NULL if it fails.
 */
char *str_tok(char *str, char *tok)
{
	char *p = strstr(str, tok);
	if (p) {
		p += strlen(tok);
		while(*p == ' ') p++;
	}
	return p;
}

/**
 * Find a token in a binary array.
 * @param array, size of array, token and size of token.
 * @returns the pointer after where the token was found or NULL if it fails.
 */
char *bin_tok(char *bin, size_t limit, char *tok, size_t tok_size)
{
	char *p = memmem(bin, limit, tok, tok_size);
	if (p) {
		p += tok_size;
	}
	return p;
}

/**
 * Check if header contain a especific value.
 * @param request structure, header name and value to check.
 * @returns the pointer where the value was found or NULL if it fails.
 */
char *header_value_cmp(struct s_request *req, char *header, char *value)
{
	char *p = str_tok(req->buf + req->header, header);
	if (p) {
		if (strstart(p, value)) {
			return p;
		}
	}
	return NULL;
}

/**
 * Lookup for boundary at buffer string.
 * @param body buffer string, boundary id, filename and content-type string.
 * @returns the pointer where the multipart start.
 */
char *boundary_find(char *str, char *boundary, char **filename, char **contenttype)
{
	char *p = str_tok(str, "--");
	while (p) {
		if (strstart(p, boundary)) {
			/* Parse headers to extract filename and content-type */
			char *hdr_start = p + strlen(boundary);
			while (*hdr_start == '\r' || *hdr_start == '\n') hdr_start++;
			char *body = strstr(hdr_start, "\r\n\r\n");
			if (body) {
				*body = '\0';
				if (filename) {
					char *fn = strstr(hdr_start, "filename=\"");
					if (fn) {
						fn += 10;
						char *end = strchr(fn, '"');
						if (end) {
							*end = '\0';
							*filename = fn;
						}
					}
				}
				if (contenttype) {
					char *ct = strstr(hdr_start, "Content-Type:");
					if (ct) {
						ct += 13;
						while (*ct == ' ') ct++;
						char *end = strstr(ct, "\r\n");
						if (end) *end = '\0';
						*contenttype = ct;
					}
				}
				return body + 4; /* skip CRLF CRLF */
			}
			break;
		}
		p = str_tok(str, "--");
	}
	return NULL;
}

/**
 * Return the size of boundary.
 * @param boundary buffer, boundary id.
 * @returns the size of boundary or 0 if fails.
 */
size_t boundary_size(char *str, char *boundary, size_t limit)
{
	char *p = bin_tok(str, limit, "\r\n--", 4);
	while (p) {
		if (strstart(p, boundary)) {
			if (cstrstart(p + strlen(boundary), "--\r\n")) {
				p -= 4;
				return (size_t)(p - str);
			}
		}
		p = bin_tok(p, limit, "\r\n--", 4);
	}
	return 0;
}

struct ApiConnectionParam {
	int index;
	struct IpfsNode* this_node;
};

/**
 * Serve gateway requests for /ipfs/<cid> paths.
 * @param fd the client socket
 * @param path the HTTP path (e.g. "/ipfs/Qm...")
 * @param local_node the IPFS node context
 * @returns 1 on success, 0 on failure
 */
static int api_serve_gateway(int fd, const char* path, struct IpfsNode* local_node) {
	if (strncmp(path, "/ipfs/", 6) != 0)
		return 0;

	const char* hash = path + 6;
	if (!hash || strlen(hash) == 0)
		return 0;

	struct Cid* cid = NULL;
	if (!ipfs_cid_decode_hash_from_base58((unsigned char*)hash, strlen(hash), &cid)) {
		return 0;
	}

	char* content = NULL;
	size_t content_size = 0;
	FILE* mem = open_memstream(&content, &content_size);
	int ret = ipfs_exporter_object_cat_to_file(local_node, cid->hash, cid->hash_length, mem);
	ipfs_cid_free(cid);
	fclose(mem);

	if (!ret) {
		if (content) free(content);
		return 0;
	}

	char header[256];
	snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n", content_size);
	write_str(fd, header);
	write(fd, content, content_size);
	free(content);
	return 1;
}

/***
 * Take an s_request and turn it into an HttpRequest
 * @param req the incoming s_request
 * @returns the resultant HttpRequest or NULL on error
 */
struct HttpRequest* api_build_http_request(struct s_request* req) {
	struct HttpRequest* request = ipfs_core_http_request_new();
	if (request != NULL) {
		char *segs = malloc (strlen(req->buf + req->request) + 1);
		if (segs) {
			strcpy(segs, req->buf + req->request);
			request->command = segs;
			segs = strchr(segs, '/');
			if (segs) {
				*segs++ = '\0';
				request->sub_command = segs; // sub_command can contain another level as filters/add
			}
			if (req->query) {
				segs = libp2p_utils_url_decode(req->buf + req->query);
				if (segs) {
					while (segs) {
						char *value, *name = segs;
						segs = strchr(segs, '&');
						if (segs) { // calc next to split before search for = on another parameter.
							*segs++ = '\0';
						}

						value = strchr(name, '=');
						if (value) {
							*value++ = '\0';
						}
						if (value && (strcmp(name, "arg")==0)) {
							libp2p_utils_vector_add(request->arguments, strdup(value));
						} else {
							struct HttpParam *hp = ipfs_core_http_param_new();
							if (hp) {
								hp->name = strdup(name);
								hp->value = value ? strdup(value) : NULL;
								libp2p_utils_vector_add(request->params, hp);
							}
						}
					}
					free(segs);
				}
			}
		}
		// Copy multipart boundary content or raw body into request->data
		if (req->boundary > 0 && req->boundary_size > 0) {
			request->data = (uint8_t*)malloc(req->boundary_size);
			if (request->data) {
				memcpy(request->data, req->buf + req->boundary, req->boundary_size);
				request->data_size = req->boundary_size;
			}
		} else if (req->body_size > 0) {
			request->data = (uint8_t*)malloc(req->body_size);
			if (request->data) {
				memcpy(request->data, req->buf + req->body, req->body_size);
				request->data_size = req->body_size;
			}
		}
	}
	return request;
}

/**
 * Write bytes into chunks.
 * @param socket, buffer array, length
 * @returns 1 when success or 0 if it fails.
 */
int api_send_resp_chunks(int fd, void *buf, size_t size)
{
	char head[20];
	size_t s;
	int l;
	struct iovec iov[3];

	// will be reused in each write, so defined only once.
	iov[2].iov_base = "\r\n";
	iov[2].iov_len = 2;

	while (size > 0) {
		s = size > MAX_CHUNK ? MAX_CHUNK : size; // write only MAX_CHUNK at once
		l = snprintf(head, sizeof head, "%x\r\n", (unsigned int)s);
		if (l <= 0)
			return 0; // fail at snprintf

		iov[0].iov_base = head;
		iov[0].iov_len = l; // head length.
		iov[1].iov_base = buf;
		iov[1].iov_len = s;

		buf += s;
		size -= s;

		if (size == 0) { // last chunk
			iov[2].iov_base = "\r\n0\r\n\r\n";
			iov[2].iov_len = 7;
		}
		libp2p_logger_debug("api", "writing chunk block of %d bytes\n", s);
		if (writev(fd, iov, 3) == -1)
			return 0; // fail writing.
	}
	return 1;
}

/**
 * Pthread to take care of each client connection.
 * @param ptr an ApiConnectionParam
 * @returns nothing
 */
void *api_connection_thread (void *ptr)
{
	int timeout, s, r;
	struct ApiConnectionParam* params = (struct ApiConnectionParam*)ptr;
	char resp[MAX_READ+1], buf[MAX_READ+1], *p, *body;
	char client[INET_ADDRSTRLEN];
	struct s_request req;
	int (*read_func)(int, struct s_request*, char*, size_t) = read_all;

	req.buf = NULL; // sanity.

	buf[MAX_READ] = '\0';

	s = params->this_node->api_context->conns[params->index]->socket;
	timeout = params->this_node->api_context->timeout;

	if (socket_read_select4(s, timeout) <= 0) {
		libp2p_logger_error("api", "Client connection timeout.\n");
		goto quit;
	}
	r = read(s, buf, sizeof buf);
	if (r <= 0) {
		// this is a common occurrence, so moved from error to debug
		libp2p_logger_debug("api", "Read from client fail.\n");
		goto quit;
	}
	buf[r] = '\0';

	p = strstr(buf, "\r\n\r\n");

	if (p) {
		body = p + 4;

		req.size = p - buf + 1;
		req.buf = malloc(req.size);
		if (!req.buf) {
			// memory allocation fail.
			libp2p_logger_error("api", "malloc fail.\n");
			write_cstr (s, HTTP_500);
			goto quit;
		}
		memcpy(req.buf, buf, req.size - 1);
		req.buf[req.size-1] = '\0';

		req.method = 0;
		p = strchr(req.buf + req.method, ' ');
		if (!p) {
			libp2p_logger_error("api", "fail looking for space on method '%s'.\n", req.buf + req.method);
			write_cstr (s, HTTP_400);
			goto quit;
		}
		*p++ = '\0'; // End of method.
		req.path = p - req.buf;
		if (strchr(p, '?')) {
			p = strchr(p, '?');
			*p++ = '\0';
			req.query = p - req.buf;
		} else {
			req.query = 0;
		}
		p = strchr(p, ' ');
		if (!p) {
			libp2p_logger_error("api", "fail looking for space on path '%s'.\n", req.buf + req.path);
			write_cstr (s, HTTP_400);
			goto quit;
		}
		*p++ = '\0'; // End of path.
		req.http_ver = p - req.buf;
		p = strchr(req.buf + req.http_ver, '\r');
		if (!p) {
			libp2p_logger_error("api", "fail looking for CR on http_ver '%s'.\n", req.buf + req.http_ver);
			write_cstr (s, HTTP_400);
			goto quit;
		}
		*p++ = '\0'; // End of http version.
		while (*p == '\r' || *p == '\n') p++;
		req.header = p - req.buf;
		req.body = req.size;
		req.body_size = 0;

		if (header_value_cmp(&req, "Transfer-Encoding:", "chunked")) {
			read_func = read_chunked;
		}

		if (!read_func(s, &req, body, r - (body - buf))) {
			libp2p_logger_error("api", "fail read_func.\n");
			write_cstr (s, HTTP_500);
			goto quit;
		}

		if (strncmp(req.buf + req.method, "GET", 3)==0) {
			if (strcmp (req.buf + req.path, "/")==0      ||
			    strcmp (req.buf + req.path, "/webui")==0 ||
			    strcmp (req.buf + req.path, "/webui/")==0) {
				char *redir;
				size_t size = sizeof(HTTP_301) + (sizeof(WEBUI_ADDR)*2);

				redir = malloc(size);
				if (redir) {
					snprintf(redir, size, HTTP_301, WEBUI_ADDR, WEBUI_ADDR);
					redir[size-1] = '\0'; // just in case
					write_dual (s, req.buf + req.http_ver, strchr (redir, ' '));
					free (redir);
				} else {
					write_cstr (s, HTTP_500);
				}
			} else if (cstrstart(req.buf + req.path, "/ipfs/")) {
				if (!api_serve_gateway(s, req.buf + req.path, params->this_node)) {
					write_cstr(s, HTTP_404);
				}
			} else if (!cstrstart(req.buf + req.path, API_V0_START)) {
				write_cstr(s, HTTP_404);
			}
			// end of GET
		} else if (strncmp(req.buf + req.method, "POST", 4)==0) {
			/* Handle gzip-compressed or JSON POST bodies */
			char *content_encoding = header_value_cmp(&req, "Content-Encoding:", "gzip");
			if (content_encoding || header_value_cmp(&req, "Content-Type:", "application/json")) {
				api_handle_post_request(content_encoding ? "gzip" : NULL,
					(const unsigned char*)(req.buf + req.body), req.body_size);
			}

			p = header_value_cmp(&req, "Content-Type:", "multipart/form-data;");
			if (p) {
				p = str_tok(p, "boundary=");
				if (p) {
					char *boundary, *l;
					int len;
					if (*p == '"') {
						p++;
						l = strchr(p, '"');
					} else {
						l = p;
						while (*l != '\r' && *l != '\0') l++;
					}
					len = l - p;
					boundary = malloc (len+1);
					if (boundary) {
						memcpy(boundary, p, len);
						boundary[len] = '\0';

						p = boundary_find(req.buf + req.body, boundary, NULL, NULL);
						if (p) {
							req.boundary_size = boundary_size(p, boundary, req.size - (p - buf));
							if (req.boundary_size > 0) {
								req.boundary = p - req.buf;
							}
						}

						free (boundary);
					}
				}
			}

			if (req.boundary > 0) {
				libp2p_logger_error("api", "boundary index = %d, size = %d\n", req.boundary, req.boundary_size);
			}

			libp2p_logger_debug("api", "method = '%s'\n"
						   "path = '%s'\n"
						   "http_ver = '%s'\n"
						   "header {\n%s\n}\n"
						   "body_size = %d\n",
			req.buf+req.method, req.buf+req.path, req.buf+req.http_ver,
			req.buf+req.header, req.body_size);
			// end of POST
		} else {
			// Unexpected???
			libp2p_logger_error("api", "fail unexpected '%s'.\n", req.buf + req.method);
			write_cstr (s, HTTP_500);
		}

		if (cstrstart(req.buf + req.path, API_V0_START)) {
			req.request = req.path + sizeof(API_V0_START) - 1;
			// now do something with the request we have built
			struct HttpRequest* http_request = api_build_http_request(&req);
			if (http_request != NULL) {
				struct HttpResponse* http_response = NULL;
				if (!ipfs_core_http_request_process(params->this_node, http_request, &http_response)) {
					libp2p_logger_error("api", "ipfs_core_http_request_process returned false.\n");
					// 404
					write_str(s, HTTP_404);
				} else {
					snprintf(resp, MAX_READ+1, "%s 200 OK\r\n" \
						"Content-Type: %s\r\n"
						"Server: c-ipfs/0.0.0-dev\r\n"
						"X-Chunked-Output: 1\r\n"
						"Connection: close\r\n"
						"Transfer-Encoding: chunked\r\n"
						"\r\n"
						,req.buf + req.http_ver, http_response->content_type);
					write_str (s, resp);
					api_send_resp_chunks(s, http_response->bytes, http_response->bytes_size);
					libp2p_logger_debug("api", "resp = {\n%s\n}\n", resp);
				}
				ipfs_core_http_request_free(http_request);
				ipfs_core_http_response_free(http_response);
			} else {
				// uh oh... something went wrong converting to the HttpRequest struct
				libp2p_logger_error("api", "Unable to build HttpRequest struct.\n");
			}
		}
	} else {
		libp2p_logger_error("api", "fail looking for body.\n");
		write_cstr (s, HTTP_400);
	}

quit:
	if (req.buf)
		free(req.buf);
	if (inet_ntop(AF_INET, &( params->this_node->api_context->conns[params->index]->ipv4), client, INET_ADDRSTRLEN) == NULL)
		strcpy(client, "UNKNOW");
	libp2p_logger_debug("api", "Closing client connection %s:%d (%d).\n", client, params->this_node->api_context->conns[params->index]->port, params->index+1);
	pthread_mutex_lock(&params->this_node->api_context->conns_lock);
	close(s);
	free ( params->this_node->api_context->conns[params->index]);
	params->this_node->api_context->conns[params->index] = NULL;
	params->this_node->api_context->conns_count--;
	pthread_mutex_unlock(&params->this_node->api_context->conns_lock);
	free(params);
	return NULL;
}

/**
 * Close all connections stopping respectives pthreads and free allocated memory.
 */
void api_connections_cleanup (struct IpfsNode* local_node)
{
	int i;

	pthread_mutex_lock(&local_node->api_context->conns_lock);
	if (local_node->api_context->conns_count > 0 && local_node->api_context->conns) {
		for (i = 0 ; i < local_node->api_context->max_conns ; i++) {
			if (local_node->api_context->conns[i] && local_node->api_context->conns[i]->pthread) {
				pthread_cancel (local_node->api_context->conns[i]->pthread);
				close (local_node->api_context->conns[i]->socket);
				free (local_node->api_context->conns[i]);
				local_node->api_context->conns[i] = NULL;
			}
		}
		local_node->api_context->conns_count = 0;
	}
	if (local_node->api_context->conns) {
		free (local_node->api_context->conns);
		local_node->api_context->conns = NULL;
	}
	pthread_mutex_unlock(&local_node->api_context->conns_lock);
}

/**
 * Pthread to keep in background dealing with client connections.
 * @param ptr is not used.
 * @returns nothing
 */
void *api_listen_thread (void *ptr)
{
	int s;
	INT_TYPE i;
	uint32_t ipv4;
	uint16_t port;
	char client[INET_ADDRSTRLEN];
	struct IpfsNode* local_node = (struct IpfsNode*)ptr;

	local_node->api_context->conns_count = 0;

	for (;;) {
		s = socket_accept4(local_node->api_context->socket, &ipv4, &port);
		if (s <= 0) {
			break;
		}
		if (local_node->api_context->conns_count >= local_node->api_context->max_conns) { // limit reached.
			libp2p_logger_error("api", "Limit of connections reached (%d).\n", local_node->api_context->max_conns);
			close (s);
			continue;
		}

		pthread_mutex_lock(&local_node->api_context->conns_lock);
		for (i = 0 ; i < local_node->api_context->max_conns && local_node->api_context->conns[i] ; i++);
		local_node->api_context->conns[i] = malloc (sizeof (struct s_conns));
		if (!local_node->api_context->conns[i]) {
			libp2p_logger_error("api", "Fail to allocate memory to accept connection.\n");
			pthread_mutex_unlock(&local_node->api_context->conns_lock);
			close (s);
			continue;
		}
		if (inet_ntop(AF_INET, &ipv4, client, INET_ADDRSTRLEN) == NULL)
			strcpy(client, "UNKNOW");
		local_node->api_context->conns[i]->socket = s;
		local_node->api_context->conns[i]->ipv4   = ipv4;
		local_node->api_context->conns[i]->port   = port;
		// create a struct, which the thread is responsible to destroy
		struct ApiConnectionParam* connection_param = (struct ApiConnectionParam*) malloc(sizeof(struct ApiConnectionParam));
		if (connection_param == NULL) {
			libp2p_logger_error("api", "api_listen_thread: Unable to allocate memory.\n");
			pthread_mutex_unlock(&local_node->api_context->conns_lock);
			close (s);
			continue;
		}
		connection_param->index = i;
		connection_param->this_node = local_node;
		if (pthread_create(&(local_node->api_context->conns[i]->pthread), NULL, api_connection_thread, (void*)connection_param)) {
			libp2p_logger_error("api", "Create pthread fail.\n");
			free (local_node->api_context->conns[i]);
			local_node->api_context->conns[i] = NULL;
			local_node->api_context->conns_count--;
			close(s);
		} else {
			local_node->api_context->conns_count++;
		}
		libp2p_logger_debug("api", "API for %s: Accept connection %s:%d (%d/%d), pthread %d.\n", local_node->identity->peer->id, client, port, local_node->api_context->conns_count, local_node->api_context->max_conns, i+1);
		pthread_mutex_unlock(&local_node->api_context->conns_lock);
	}
	api_connections_cleanup (local_node);
	return NULL;
}

struct ApiContext* api_context_new() {
	struct ApiContext* context = (struct ApiContext*) malloc(sizeof(struct ApiContext));
	if (context != NULL) {
		context->conns = NULL;
		context->conns_count = 0;
		context->ipv4 = 0;
		context->max_conns = 0;
		context->port = 0;
		context->socket = 0;
		context->timeout = 0;
		pthread_mutex_init(&context->conns_lock, NULL);
	}
	return context;
}

/**
 * Start API interface daemon.
 * @param local_node the context
 * @param max_conns.
 * @param timeout time out of client connection.
 * @returns 0 when failure or 1 if success.
 */
int api_start (struct IpfsNode* local_node, int max_conns, int timeout)
{
	int s;
	size_t alloc_size = sizeof(void*) * max_conns;

	struct MultiAddress* my_address = multiaddress_new_from_string(local_node->repo->config->addresses->api);

	char* ip = NULL;
	multiaddress_get_ip_address(my_address, &ip);
	int port = multiaddress_get_ip_port(my_address);

	local_node->api_context = api_context_new();
	if (local_node->api_context == NULL) {
		multiaddress_free(my_address);
		return 0;
	}

	local_node->api_context->ipv4 = hostname_to_ip(ip); // api is listening only on loopback.
	if (ip != NULL)
		free(ip);
	local_node->api_context->port = port;

	if ((s = socket_listen(socket_tcp4(), &(local_node->api_context->ipv4), &(local_node->api_context->port))) <= 0) {
		libp2p_logger_error("api", "Failed to init API. port: %d\n", port);
		return 0;
	}

	local_node->api_context->socket = s;
	local_node->api_context->max_conns = max_conns;
	local_node->api_context->timeout = timeout;

	local_node->api_context->conns = malloc (alloc_size);
	if (!local_node->api_context->conns) {
		close (s);
		libp2p_logger_error("api", "Error allocating memory.\n");
		return 0;
	}
	memset(local_node->api_context->conns, 0, alloc_size);

	if (pthread_create(&local_node->api_context->api_thread, NULL, api_listen_thread, (void*)local_node)) {
		close (s);
		free (local_node->api_context->conns);
		local_node->api_context->conns = NULL;
		local_node->api_context->api_thread = 0;
		libp2p_logger_error("api", "Error creating thread for API.\n");
		return 0;
	}

	libp2p_logger_info("api", "API server listening on %d.\n", port);
	return 1;
}

/**
 * Stop API.
 * @returns 0 when failure or 1 if success.
 */
int api_stop (struct IpfsNode *local_node)
{
	if (local_node->api_context->api_thread == 0) return 0;
	shutdown(local_node->api_context->socket, SHUT_RDWR);
	pthread_cancel(local_node->api_context->api_thread);

	api_connections_cleanup (local_node);

	local_node->api_context->api_thread = 0;

	return 1;
}
