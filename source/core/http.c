// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "http.h"
#include "platform.h"
#include "validate.h"

#define HTTP_HEADER_MAX 4096

// Largest body/chunk accepted (the DS has 4 MB of RAM, but bodies are
// streamed, so this is only a sanity limit)
#define HTTP_MAX_CHUNK 0x7FFFFFFL

void http_client_init(http_client *c, const char *host, int port)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->port = port;
    c->timeout_ms = 10000;
    c->fd = -1;
}

void http_client_close(http_client *c)
{
    if (c->fd >= 0)
    {
        shutdown(c->fd, SHUT_RDWR);
        close(c->fd);
        c->fd = -1;
    }
}

static void set_error(http_result *res, const char *fmt, const char *detail)
{
    res->status = 0;
    snprintf(res->error, sizeof(res->error), fmt, detail ? detail : "");
}

static bool resolve(http_client *c, http_result *res)
{
    if (c->addr_valid)
        return true;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)c->port);

    if (inet_aton(c->host, &sin.sin_addr) == 0)
    {
        struct addrinfo hint;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        hint.ai_socktype = SOCK_STREAM;

        struct addrinfo *result = NULL;
        int err = getaddrinfo(c->host, NULL, &hint, &result);
        if (err != 0 || result == NULL)
        {
            set_error(res, "Can't resolve host %s", c->host);
            return false;
        }
        struct sockaddr_in *found = (struct sockaddr_in *)result->ai_addr;
        sin.sin_addr = found->sin_addr;
        freeaddrinfo(result);
    }

    if (!c->allow_public && !is_private_ipv4(ntohl(sin.sin_addr.s_addr)))
    {
        // The connection isn't encrypted: never send credentials over the
        // internet in clear text.
        set_error(res, "%s isn't on your local network. DSHA only talks to "
                       "local servers (no HTTPS).", c->host);
        return false;
    }

    memcpy(c->addr, &sin, sizeof(sin));
    c->addr_len = sizeof(sin);
    c->addr_valid = true;
    return true;
}

// Waits until the socket is readable/writable. Note: poll() is used instead of
// select() because lwIP on the DS has its own fd_set layout.
static bool wait_socket(int fd, bool for_write, uint32_t start, int timeout_ms)
{
    while (1)
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = for_write ? POLLOUT : POLLIN;
        pfd.revents = 0;

        int rc = poll(&pfd, 1, 0);
        if (rc > 0)
            return true; // Ready, or an error that connect() will report
        if (rc < 0 && errno != EINTR)
            return false;

        if ((int32_t)(platform_millis() - start) > timeout_ms)
            return false;

        platform_yield();
    }
}

static bool do_connect(http_client *c, http_result *res)
{
    if (!resolve(c, res))
        return false;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        set_error(res, "Can't create socket%s", NULL);
        return false;
    }

    int opt = 1;
    ioctl(fd, FIONBIO, (char *)&opt);

    int rc = connect(fd, (struct sockaddr *)c->addr, (socklen_t)c->addr_len);
    if (rc != 0)
    {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EAGAIN)
        {
            close(fd);
            set_error(res, "Connection refused by %s", c->host);
            return false;
        }

        if (!wait_socket(fd, true, platform_millis(), c->timeout_ms))
        {
            close(fd);
            set_error(res, "Timeout connecting to %s", c->host);
            return false;
        }

        int so_err = 0;
        socklen_t len = sizeof(so_err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) == 0
            && so_err != 0)
        {
            close(fd);
            set_error(res, "Can't connect to %s", c->host);
            return false;
        }
    }

    c->fd = fd;
    return true;
}

static bool send_all(http_client *c, const char *data, size_t len)
{
    uint32_t start = platform_millis();

    while (len > 0)
    {
        int n = send(c->fd, data, len, 0);
        if (n > 0)
        {
            data += n;
            len -= (size_t)n;
            start = platform_millis();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            if ((int32_t)(platform_millis() - start) > c->timeout_ms)
                return false;
            platform_yield();
            continue;
        }
        return false;
    }
    return true;
}

// Response parser
// ---------------

enum {
    BODY_IDENTITY,     // Read until Content-Length bytes or connection close
    BODY_CHUNK_SIZE,
    BODY_CHUNK_EXT,    // Skipping chunk extensions until LF
    BODY_CHUNK_DATA,
    BODY_CHUNK_CRLF,
    BODY_TRAILER,      // After the last chunk, until an empty line
    BODY_DONE,
};

typedef struct {
    http_data_cb cb;
    void *user;
    bool aborted;
    bool error_body; // Deliver the body of non-2xx responses too

    bool headers_done;
    char header[HTTP_HEADER_MAX];
    size_t header_len;

    int status;
    long content_length; // -1 if unknown
    bool chunked;
    bool conn_close;

    int body_state;
    long remaining; // Bytes left in the body (identity) or in the chunk
    long chunk_size;
    int trailer_line_len;
} resp_parser;

static void deliver(resp_parser *p, const char *data, size_t len)
{
    if (len == 0 || p->aborted || p->cb == NULL)
        return;
    if (!p->error_body && (p->status < 200 || p->status >= 300))
        return;
    if (!p->cb(p->user, data, len))
        p->aborted = true;
}

static bool parse_headers(resp_parser *p)
{
    char *line = p->header;
    char *end = strstr(line, "\r\n");
    if (end == NULL)
        return false;
    *end = '\0';

    // "HTTP/1.1 200 OK"
    if (strncmp(line, "HTTP/", 5) != 0)
        return false;
    char *sp = strchr(line, ' ');
    if (sp == NULL)
        return false;
    p->status = atoi(sp + 1);
    if (strncmp(line, "HTTP/1.0", 8) == 0)
        p->conn_close = true;

    line = end + 2;
    while ((end = strstr(line, "\r\n")) != NULL)
    {
        *end = '\0';
        if (*line == '\0')
            break;

        char *colon = strchr(line, ':');
        if (colon)
        {
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ' || *value == '\t')
                value++;

            if (strcasecmp(line, "Content-Length") == 0)
            {
                char *end;
                long v = strtol(value, &end, 10);
                if (end == value || v < 0 || v > HTTP_MAX_CHUNK)
                    return false; // Invalid or absurd length
                p->content_length = v;
            }
            else if (strcasecmp(line, "Transfer-Encoding") == 0
                     && strstr(value, "chunked") != NULL)
                p->chunked = true;
            else if (strcasecmp(line, "Connection") == 0
                     && strncasecmp(value, "close", 5) == 0)
                p->conn_close = true;
        }
        line = end + 2;
    }

    if (p->chunked)
    {
        p->body_state = BODY_CHUNK_SIZE;
        p->chunk_size = 0;
    }
    else
    {
        p->body_state = BODY_IDENTITY;
        p->remaining = p->content_length;
        if (p->content_length == 0)
            p->body_state = BODY_DONE;
    }

    // Responses without a body
    if (p->status == 204 || p->status == 304 || (p->status >= 100 && p->status < 200))
        p->body_state = BODY_DONE;

    return true;
}

static int hexdigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Returns false on protocol error
static bool parse_body(resp_parser *p, const char *data, size_t len)
{
    size_t i = 0;

    while (i < len && p->body_state != BODY_DONE)
    {
        switch (p->body_state)
        {
            case BODY_IDENTITY:
            {
                size_t n = len - i;
                if (p->remaining >= 0 && (long)n > p->remaining)
                    n = (size_t)p->remaining;
                deliver(p, data + i, n);
                i += n;
                if (p->remaining >= 0)
                {
                    p->remaining -= (long)n;
                    if (p->remaining == 0)
                        p->body_state = BODY_DONE;
                }
                break;
            }

            case BODY_CHUNK_SIZE:
            {
                char c = data[i++];
                int h = hexdigit(c);
                if (h >= 0)
                {
                    // Reject absurd sizes instead of overflowing
                    if (p->chunk_size > (HTTP_MAX_CHUNK >> 4))
                        return false;
                    p->chunk_size = p->chunk_size * 16 + h;
                }
                else if (c == ';' || c == ' ' || c == '\t' || c == '\r')
                {
                    p->body_state = BODY_CHUNK_EXT;
                }
                else if (c == '\n')
                {
                    goto chunk_size_done;
                }
                else
                {
                    return false;
                }
                break;
            }

            case BODY_CHUNK_EXT:
            {
                char c = data[i++];
                if (c != '\n')
                    break;
            chunk_size_done:
                if (p->chunk_size == 0)
                {
                    p->body_state = BODY_TRAILER;
                    p->trailer_line_len = 0;
                }
                else
                {
                    p->remaining = p->chunk_size;
                    p->body_state = BODY_CHUNK_DATA;
                }
                break;
            }

            case BODY_CHUNK_DATA:
            {
                size_t n = len - i;
                if ((long)n > p->remaining)
                    n = (size_t)p->remaining;
                deliver(p, data + i, n);
                i += n;
                p->remaining -= (long)n;
                if (p->remaining == 0)
                    p->body_state = BODY_CHUNK_CRLF;
                break;
            }

            case BODY_CHUNK_CRLF:
            {
                char c = data[i++];
                if (c == '\n')
                {
                    p->body_state = BODY_CHUNK_SIZE;
                    p->chunk_size = 0;
                }
                else if (c != '\r')
                {
                    return false;
                }
                break;
            }

            case BODY_TRAILER:
            {
                char c = data[i++];
                if (c == '\n')
                {
                    if (p->trailer_line_len == 0)
                        p->body_state = BODY_DONE;
                    p->trailer_line_len = 0;
                }
                else if (c != '\r')
                {
                    p->trailer_line_len++;
                }
                break;
            }
        }
    }

    return true;
}

static bool parse_data(resp_parser *p, const char *data, size_t len)
{
    if (!p->headers_done)
    {
        // Copy data into the header buffer until the end of the headers
        size_t i = 0;
        while (i < len)
        {
            if (p->header_len >= HTTP_HEADER_MAX - 1)
                return false;
            p->header[p->header_len++] = data[i++];
            if (p->header_len >= 4
                && memcmp(p->header + p->header_len - 4, "\r\n\r\n", 4) == 0)
            {
                p->header[p->header_len] = '\0';
                if (!parse_headers(p))
                    return false;
                p->headers_done = true;
                break;
            }
        }
        data += i;
        len -= i;
        if (!p->headers_done)
            return true;
    }

    return parse_body(p, data, len);
}

#ifdef DSHA_TESTING
// Test hook: runs the response parser over a buffer (used by the fuzzer)
bool http_parse_response_for_test(const char *data, size_t len,
                                  size_t chunk, http_data_cb cb, void *user,
                                  int *status, bool *complete)
{
    static resp_parser p;
    memset(&p, 0, sizeof(p));
    p.cb = cb;
    p.user = user;
    p.content_length = -1;
    p.error_body = true;
    bool ok = true;
    for (size_t i = 0; i < len && ok; i += chunk)
    {
        size_t n = len - i < chunk ? len - i : chunk;
        ok = parse_data(&p, data + i, n);
    }
    *status = p.status;
    *complete = p.headers_done && p.body_state == BODY_DONE;
    return ok;
}
#endif

// Result of one attempt
enum {
    ATTEMPT_OK,
    ATTEMPT_RETRY, // Stale keep-alive connection, retry with a new one
    ATTEMPT_FAIL,
};

static int attempt(http_client *c, const char *request, size_t request_len,
                   const char *body, resp_parser *p, http_result *res,
                   bool reused)
{
    if (!send_all(c, request, request_len)
        || (body != NULL && !send_all(c, body, strlen(body))))
    {
        http_client_close(c);
        if (reused)
            return ATTEMPT_RETRY;
        set_error(res, "Error sending request to %s", c->host);
        return ATTEMPT_FAIL;
    }

    static char buf[2048];
    uint32_t last_data = platform_millis();
    size_t total = 0;

    while (1)
    {
        int n = recv(c->fd, buf, sizeof(buf), 0);

        if (n > 0)
        {
            total += (size_t)n;
            c->bytes_received += (uint32_t)n;
            last_data = platform_millis();

            if (!parse_data(p, buf, (size_t)n))
            {
                http_client_close(c);
                set_error(res, "Invalid HTTP response%s", NULL);
                return ATTEMPT_FAIL;
            }
            if (p->aborted)
            {
                http_client_close(c);
                set_error(res, "Transfer aborted%s", NULL);
                return ATTEMPT_FAIL;
            }
            if (p->headers_done && p->body_state == BODY_DONE)
                break;
            continue;
        }

        if (n == 0)
        {
            // Connection closed by the server
            http_client_close(c);

            if (total == 0 && reused)
                return ATTEMPT_RETRY;

            // Without Content-Length, the end of the connection is the end of
            // the body.
            if (p->headers_done && p->body_state == BODY_IDENTITY
                && p->content_length < 0)
                break;

            set_error(res, "Connection closed by %s", c->host);
            return ATTEMPT_FAIL;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            if ((int32_t)(platform_millis() - last_data) > c->timeout_ms)
            {
                http_client_close(c);
                set_error(res, "Timeout waiting for %s", c->host);
                return ATTEMPT_FAIL;
            }
            platform_yield();
            continue;
        }

        // Other errors (connection reset...)
        http_client_close(c);
        if (total == 0 && reused)
            return ATTEMPT_RETRY;
        set_error(res, "Network error talking to %s", c->host);
        return ATTEMPT_FAIL;
    }

    if (p->conn_close)
        http_client_close(c);

    res->status = p->status;
    res->error[0] = '\0';
    return ATTEMPT_OK;
}

void http_urlencode(char *out, size_t out_len, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t len = strlen(out);

    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
    {
        if (len + 4 >= out_len)
            break;
        unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
            || c == '~')
        {
            out[len++] = (char)c;
        }
        else
        {
            out[len++] = '%';
            out[len++] = hex[c >> 4];
            out[len++] = hex[c & 15];
        }
    }
    out[len] = '\0';
}

bool http_request(http_client *c, const char *method, const char *path,
                  const char *extra_headers, const char *body,
                  http_data_cb cb, void *user, http_result *res)
{
    return http_request_ex(c, method, path, extra_headers, body,
                           "application/json", false, cb, user, res);
}

bool http_request_ex(http_client *c, const char *method, const char *path,
                     const char *extra_headers, const char *body,
                     const char *content_type, bool error_body,
                     http_data_cb cb, void *user, http_result *res)
{
    static char request[2048];
    static resp_parser parser;

    res->status = 0;
    res->error[0] = '\0';

    char host_header[160];
    if (c->port == 80)
        snprintf(host_header, sizeof(host_header), "%s", c->host);
    else
        snprintf(host_header, sizeof(host_header), "%s:%d", c->host, c->port);

    int len;
    if (body != NULL)
    {
        len = snprintf(request, sizeof(request),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "User-Agent: DSHA (Nintendo DS)\r\n"
                       "Accept: application/json\r\n"
                       "Connection: keep-alive\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %u\r\n"
                       "%s\r\n",
                       method, path, host_header, content_type,
                       (unsigned)strlen(body),
                       extra_headers ? extra_headers : "");
    }
    else
    {
        len = snprintf(request, sizeof(request),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "User-Agent: DSHA (Nintendo DS)\r\n"
                       "Accept: application/json\r\n"
                       "Connection: keep-alive\r\n"
                       "%s\r\n",
                       method, path, host_header,
                       extra_headers ? extra_headers : "");
    }

    if (len <= 0 || len >= (int)sizeof(request))
    {
        set_error(res, "Request too long%s", NULL);
        return false;
    }

    for (int tries = 0; tries < 2; tries++)
    {
        bool reused = c->fd >= 0;
        if (!reused)
        {
            if (!do_connect(c, res))
                return false;
            if (tries > 0)
                c->reconnects++;
        }

        memset(&parser, 0, sizeof(parser));
        parser.cb = cb;
        parser.user = user;
        parser.error_body = error_body;
        parser.content_length = -1;

        int r = attempt(c, request, (size_t)len, body, &parser, res, reused);
        if (r == ATTEMPT_OK)
        {
            c->requests++;
            return true;
        }
        if (r == ATTEMPT_FAIL)
            return false;
        // ATTEMPT_RETRY: loop again with a new connection
    }

    set_error(res, "Can't talk to %s", c->host);
    return false;
}
