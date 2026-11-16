// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ha.h"
#include "platform.h"
#include "setup_server.h"

#define REQUEST_MAX 4096
#define REQUEST_TIMEOUT_MS 3000

bool setup_server_start(setup_server *s, int port, const char *default_server)
{
    memset(s, 0, sizeof(*s));
    s->listen_fd = -1;
    s->port = port;
    snprintf(s->default_server, sizeof(s->default_server), "%s",
             default_server ? default_server : "");
    snprintf(s->status, sizeof(s->status), "Waiting for you to sign in...");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0
        || listen(fd, 2) != 0)
    {
        close(fd);
        return false;
    }

    opt = 1;
    ioctl(fd, FIONBIO, (char *)&opt);
    s->listen_fd = fd;
    return true;
}

void setup_server_stop(setup_server *s)
{
    if (s->listen_fd >= 0)
        close(s->listen_fd);
    s->listen_fd = -1;
}

// Helpers
// -------

static void url_decode(char *s)
{
    char *out = s;
    for (char *p = s; *p; p++)
    {
        if (*p == '+')
        {
            *out++ = ' ';
        }
        else if (*p == '%' && isxdigit((unsigned char)p[1])
                 && isxdigit((unsigned char)p[2]))
        {
            char hex[3] = { p[1], p[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            p += 2;
        }
        else
        {
            *out++ = *p;
        }
    }
    *out = '\0';
}

// Finds "name=value" in a form body and decodes it into out
static void form_value(const char *body, const char *name, char *out,
                       size_t out_len)
{
    out[0] = '\0';
    size_t nlen = strlen(name);
    const char *p = body;

    while (p && *p)
    {
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=')
        {
            const char *v = p + nlen + 1;
            const char *end = strchr(v, '&');
            size_t len = end ? (size_t)(end - v) : strlen(v);
            if (len >= out_len)
                len = out_len - 1;
            memcpy(out, v, len);
            out[len] = '\0';
            url_decode(out);
            // Trim spaces (very common when pasting)
            char *t = out;
            while (isspace((unsigned char)*t))
                t++;
            memmove(out, t, strlen(t) + 1);
            size_t l = strlen(out);
            while (l > 0 && isspace((unsigned char)out[l - 1]))
                out[--l] = '\0';
            return;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
}

// Escapes text for HTML
static void html_escape(char *out, size_t out_len, const char *in)
{
    size_t len = 0;
    for (; *in && len + 7 < out_len; in++)
    {
        const char *rep = NULL;
        switch (*in)
        {
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '&': rep = "&amp;"; break;
            case '"': rep = "&quot;"; break;
            default: break;
        }
        if (rep)
        {
            size_t rl = strlen(rep);
            memcpy(out + len, rep, rl);
            len += rl;
        }
        else
        {
            out[len++] = *in;
        }
    }
    out[len] = '\0';
}

static bool send_all(int fd, const char *data, size_t len)
{
    uint32_t start = platform_millis();
    while (len > 0)
    {
        int n = send(fd, data, len, 0);
        if (n > 0)
        {
            data += n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            && (int32_t)(platform_millis() - start) < REQUEST_TIMEOUT_MS)
        {
            platform_yield();
            continue;
        }
        return false;
    }
    return true;
}

// Pages
// -----

static const char page_head[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>DSHA setup</title><style>"
    "body{font-family:system-ui,sans-serif;background:#111827;color:#f3f4f6;"
    "margin:0;padding:16px}main{max-width:420px;margin:auto}"
    "h1{font-size:22px;color:#03a9f4}"
    "label{display:block;margin:14px 0 4px;color:#9ca3af;font-size:14px}"
    "input,textarea{width:100%;box-sizing:border-box;padding:12px;"
    "font-size:16px;border-radius:8px;border:1px solid #374151;"
    "background:#1f2937;color:#f3f4f6}"
    "button{margin-top:18px;width:100%;padding:14px;font-size:17px;"
    "border:0;border-radius:8px;background:#03a9f4;color:#fff}"
    "details{margin-top:22px;color:#9ca3af}"
    ".msg{padding:12px;border-radius:8px;background:#1f2937;margin:12px 0}"
    ".err{background:#451a1a;color:#fca5a5}.ok{background:#14532d}"
    "small{color:#9ca3af}"
    "</style>";

static void respond(int fd, int code, const char *body)
{
    char header[256];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %u\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     code, code == 200 ? "OK" : code == 303 ? "See Other"
                                                            : "Not Found",
                     (unsigned)strlen(body));
    send_all(fd, header, (size_t)n);
    send_all(fd, body, strlen(body));
}

static void redirect(int fd, const char *location)
{
    char header[256];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 303 See Other\r\nLocation: %s\r\n"
                     "Content-Length: 0\r\nConnection: close\r\n\r\n",
                     location);
    send_all(fd, header, (size_t)n);
}

static char page[6144];

static void page_form(setup_server *s, int fd)
{
    char server[200], status[400];
    html_escape(server, sizeof(server),
                s->server[0] ? s->server : s->default_server);
    html_escape(status, sizeof(status), s->status);

    snprintf(page, sizeof(page),
             "%s</head><body><main>"
             "<h1>&#127968; DSHA setup</h1>"
             "<p>Sign in to Home Assistant for your Nintendo DS.</p>"
             "%s%s%s"
             "<form method=\"post\" action=\"/\">"
             "<label>Home Assistant address</label>"
             "<input name=\"server\" value=\"%s\" "
             "placeholder=\"192.168.1.10:8123\" autocapitalize=\"off\">"
             "<label>User name</label>"
             "<input name=\"username\" autocapitalize=\"off\" "
             "autocomplete=\"username\" autofocus>"
             "<label>Password</label>"
             "<input name=\"password\" type=\"password\" "
             "autocomplete=\"current-password\">"
             "<details><summary>Use a long-lived access token instead"
             "</summary><label>Token (leave the user name empty)</label>"
             "<textarea name=\"token\" rows=\"4\"></textarea></details>"
             "<button type=\"submit\">Sign in</button>"
             "</form>"
             "<p><small>Your password is only used to sign in; it isn't "
             "stored on the DS. DSHA keeps a session that you can revoke in "
             "your Home Assistant profile.</small></p>"
             "</main></body></html>",
             page_head,
             s->status_error ? "<div class=\"msg err\">" : "",
             s->status_error ? status : "",
             s->status_error ? "</div>" : "",
             server);
    respond(fd, 200, page);
}

static void page_status(setup_server *s, int fd)
{
    char status[400];
    html_escape(status, sizeof(status), s->status);

    if (s->state == SETUP_STATE_MFA)
    {
        snprintf(page, sizeof(page),
                 "%s</head><body><main><h1>Two-factor code</h1>"
                 "%s%s%s"
                 "<form method=\"post\" action=\"/mfa\">"
                 "<label>Code from your authenticator app</label>"
                 "<input name=\"code\" inputmode=\"numeric\" "
                 "autocomplete=\"one-time-code\" autofocus>"
                 "<button type=\"submit\">Continue</button></form>"
                 "</main></body></html>",
                 page_head,
                 s->status_error ? "<div class=\"msg err\">" : "",
                 s->status_error ? status : "",
                 s->status_error ? "</div>" : "");
    }
    else if (s->state == SETUP_STATE_DONE)
    {
        snprintf(page, sizeof(page),
                 "%s</head><body><main><h1>&#9989; All set!</h1>"
                 "<div class=\"msg ok\">%s</div>"
                 "<p>You can close this page and pick up your DS.</p>"
                 "</main></body></html>",
                 page_head, status);
    }
    else if (s->status_error)
    {
        page_form(s, fd); // Show the error above the form
        return;
    }
    else
    {
        // Still working: refresh until the DS has an answer
        snprintf(page, sizeof(page),
                 "%s<meta http-equiv=\"refresh\" content=\"1\"></head>"
                 "<body><main><h1>DSHA setup</h1>"
                 "<div class=\"msg\">%s</div></main></body></html>",
                 page_head, status);
    }
    respond(fd, 200, page);
}

// Request handling
// ----------------

static char request[REQUEST_MAX];

// Case insensitive strstr() (not available everywhere)
static const char *find_nocase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
    {
        if (strncasecmp(hay, needle, n) == 0)
            return hay;
    }
    return NULL;
}

// Reads a whole request (headers and body). Returns its length or -1.
static int read_request(int fd)
{
    size_t len = 0;
    uint32_t start = platform_millis();
    long body_len = -1;
    size_t header_end = 0;

    while (len < sizeof(request) - 1)
    {
        int n = recv(fd, request + len, sizeof(request) - 1 - len, 0);
        if (n > 0)
        {
            len += (size_t)n;
            request[len] = '\0';

            if (header_end == 0)
            {
                char *e = strstr(request, "\r\n\r\n");
                if (e)
                {
                    header_end = (size_t)(e - request) + 4;
                    const char *cl = find_nocase(request,
                                                 "\r\nContent-Length:");
                    body_len = (cl && cl < e) ? atol(cl + 17) : 0;
                    if (body_len < 0)
                        body_len = 0;
                }
            }
            if (header_end && len >= header_end + (size_t)body_len)
                return (int)len;
            continue;
        }
        if (n == 0)
            break;
        if ((errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            && (int32_t)(platform_millis() - start) < REQUEST_TIMEOUT_MS)
        {
            platform_yield();
            continue;
        }
        break;
    }
    return header_end ? (int)len : -1;
}

setup_event setup_server_poll(setup_server *s)
{
    if (s->listen_fd < 0)
        return SETUP_NOTHING;

    struct pollfd pfd = { s->listen_fd, POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0)
        return SETUP_NOTHING;

    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0)
        return SETUP_NOTHING;

    int opt = 1;
    ioctl(fd, FIONBIO, (char *)&opt);

    setup_event ev = SETUP_NOTHING;
    int len = read_request(fd);
    if (len > 0)
    {
        char *body = strstr(request, "\r\n\r\n");
        body = body ? body + 4 : request + len;
        bool post = strncmp(request, "POST ", 5) == 0;
        bool get = strncmp(request, "GET ", 4) == 0;
        const char *path = request + (post ? 5 : 4);

        if (get && (strncmp(path, "/ ", 2) == 0 || strncmp(path, "/?", 2) == 0))
        {
            if (s->state == SETUP_STATE_WAITING)
                page_form(s, fd);
            else
                page_status(s, fd);
        }
        else if (get && strncmp(path, "/status", 7) == 0)
        {
            page_status(s, fd);
        }
        else if (post && strncmp(path, "/mfa", 4) == 0
                 && s->state == SETUP_STATE_MFA)
        {
            form_value(body, "code", s->code, sizeof(s->code));
            s->is_mfa = true;
            s->status_error = false;
            snprintf(s->status, sizeof(s->status), "Checking the code...");
            redirect(fd, "/status");
            ev = SETUP_SUBMITTED;
        }
        else if (post && strncmp(path, "/ ", 2) == 0
                 && s->state == SETUP_STATE_WAITING)
        {
            form_value(body, "server", s->server, sizeof(s->server));
            form_value(body, "username", s->username, sizeof(s->username));
            form_value(body, "password", s->password, sizeof(s->password));
            form_value(body, "token", s->token, sizeof(s->token));
            s->is_mfa = false;
            s->status_error = false;
            snprintf(s->status, sizeof(s->status), "Signing in...");
            redirect(fd, "/status");
            ev = SETUP_SUBMITTED;
        }
        else if (get && strncmp(path, "/favicon", 8) == 0)
        {
            respond(fd, 404, "");
        }
        else
        {
            redirect(fd, "/");
        }
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);
    return ev;
}

// Sign in logic
// -------------

void setup_parse_server(const char *server, char *host, size_t host_len,
                        int *port)
{
    char tmp[160];
    const char *v = server;
    if (strncasecmp(v, "http://", 7) == 0)
        v += 7;
    else if (strncasecmp(v, "https://", 8) == 0)
        v += 8;
    snprintf(tmp, sizeof(tmp), "%s", v);

    char *slash = strchr(tmp, '/');
    if (slash)
        *slash = '\0';

    *port = 8123;
    char *colon = strrchr(tmp, ':');
    if (colon && strchr(tmp, ':') == colon)
    {
        *colon = '\0';
        int p = atoi(colon + 1);
        if (p > 0 && p < 65536)
            *port = p;
    }
    snprintf(host, host_len, "%s", tmp);
}

static void set_status(setup_server *s, bool error, const char *fmt,
                       const char *arg)
{
    snprintf(s->status, sizeof(s->status), fmt, arg ? arg : "");
    s->status_error = error;
}

static void finish(setup_server *s, ha_config *cfg)
{
    snprintf(cfg->host, sizeof(cfg->host), "%s", s->login_host);
    cfg->port = s->login_port;
    cfg->demo = false;
    s->state = SETUP_STATE_DONE;
    set_status(s, false, "Signed in to %s. Your DS is ready!", s->login_host);
}

login_result setup_sign_in(setup_server *s, ha_config *cfg,
                           const char *server, const char *username,
                           const char *password, const char *token)
{
    static http_client c;

    setup_parse_server(server, s->login_host, sizeof(s->login_host),
                       &s->login_port);
    if (s->login_host[0] == '\0')
    {
        set_status(s, true, "Please enter the address of Home Assistant%s",
                   NULL);
        return LOGIN_INVALID;
    }

    http_client_init(&c, s->login_host, s->login_port);
    c.timeout_ms = 5000;

    if (!auth_probe(&c))
    {
        http_client_close(&c);
        set_status(s, true,
                   "Can't find Home Assistant at %s. Check the address "
                   "(use the IP, like 192.168.1.10:8123).", server);
        return LOGIN_ERROR;
    }

    login_result r;
    if (token != NULL && token[0] != '\0' && (username == NULL
                                              || username[0] == '\0'))
    {
        // Long-lived access token: check it with GET /api/
        static ha_config tmp;
        static ha_client ha;
        config_defaults(&tmp);
        snprintf(tmp.host, sizeof(tmp.host), "%s", s->login_host);
        tmp.port = s->login_port;
        snprintf(tmp.token, sizeof(tmp.token), "%s", token);
        tmp.num_entities = 1; // Don't care about entities here
        ha_init(&ha, &tmp);
        bool ok = ha_check(&ha);
        http_client_close(&ha.http);
        if (!ok)
        {
            set_status(s, true, "The token doesn't work: %s", ha.error);
            http_client_close(&c);
            return LOGIN_INVALID;
        }
        snprintf(cfg->token, sizeof(cfg->token), "%s", token);
        cfg->refresh_token[0] = '\0';
        finish(s, cfg);
        http_client_close(&c);
        return LOGIN_OK;
    }

    if (username == NULL || username[0] == '\0')
    {
        set_status(s, true, "Please enter your user name%s", NULL);
        http_client_close(&c);
        return LOGIN_INVALID;
    }

    r = auth_login(&c, &s->login, username, password ? password : "");
    http_client_close(&c);

    switch (r)
    {
        case LOGIN_OK:
            snprintf(cfg->refresh_token, sizeof(cfg->refresh_token), "%s",
                     s->login.refresh_token);
            cfg->token[0] = '\0';
            finish(s, cfg);
            break;
        case LOGIN_NEED_MFA:
            s->state = SETUP_STATE_MFA;
            set_status(s, false, "Enter your two-factor code%s", NULL);
            break;
        default:
            set_status(s, true, "%s", s->login.error);
            break;
    }
    return r;
}

login_result setup_sign_in_mfa(setup_server *s, ha_config *cfg,
                               const char *code)
{
    static http_client c;
    http_client_init(&c, s->login_host, s->login_port);
    c.timeout_ms = 5000;

    login_result r = auth_login_mfa(&c, &s->login, code);
    http_client_close(&c);

    switch (r)
    {
        case LOGIN_OK:
            snprintf(cfg->refresh_token, sizeof(cfg->refresh_token), "%s",
                     s->login.refresh_token);
            cfg->token[0] = '\0';
            finish(s, cfg);
            break;
        case LOGIN_NEED_MFA:
            s->state = SETUP_STATE_MFA;
            set_status(s, true, "%s", s->login.error);
            break;
        default:
            // The flow is over (for example, too many attempts): start again
            s->state = SETUP_STATE_WAITING;
            set_status(s, true, "%s", s->login.error);
            break;
    }
    return r;
}

bool setup_process(setup_server *s, ha_config *cfg)
{
    login_result r;
    if (s->is_mfa)
        r = setup_sign_in_mfa(s, cfg, s->code);
    else
        r = setup_sign_in(s, cfg, s->server, s->username, s->password,
                          s->token);

    // Don't keep the password in memory longer than needed
    memset(s->password, 0, sizeof(s->password));
    return r == LOGIN_OK;
}
