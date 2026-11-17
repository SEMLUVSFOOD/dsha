// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// TLS client with Mbed TLS 3.6. The handshake is the expensive part on a DS
// (a few seconds of public key maths at 67 MHz), so the config prefers the
// fastest key exchange (X25519) and runs the handshake on a big stack (see
// platform_run_big_stack()). Afterwards, the HTTP client keeps the connection
// alive, so it's only paid once.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

#include "ca_roots.h"
#include "platform.h"
#include "tls.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// Certificates are checked for expiry only if the clock looks set (the DS
// has no network time, and a never set clock says 2000 or so).
#define CLOCK_PLAUSIBLE ((time_t)1735689600) // 2025-01-01

// The handshake does seconds of maths between network packets on a DS
#define HANDSHAKE_MIN_TIMEOUT_MS 30000

struct tls_conn {
    mbedtls_ssl_context ssl;
    int fd;
};

static struct {
    bool certs_ready;
    bool rng_ready;
    bool conf_ready;
    bool use_builtin;
    mbedtls_x509_crt cas;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config conf;
} g = { .use_builtin = true };

static void set_err(char *err, size_t len, const char *fmt, const char *a,
                    int code)
{
    if (err != NULL && len > 0)
        snprintf(err, len, fmt, a ? a : "", code);
}

static void certs_init(void)
{
    if (g.certs_ready)
        return;
    g.certs_ready = true;
    mbedtls_x509_crt_init(&g.cas);
    if (!g.use_builtin)
        return;
    for (int i = 0; i < ca_roots_count; i++)
    {
        // The DER data stays in the ROM image: don't copy it to the heap
        mbedtls_x509_crt_parse_der_nocopy(&g.cas, ca_roots[i].der,
                                          ca_roots[i].len);
    }
}

void tls_use_builtin_roots(bool use)
{
    g.use_builtin = use;
}

int tls_trusted_count(void)
{
    certs_init();
    int n = 0;
    for (const mbedtls_x509_crt *c = &g.cas; c != NULL && c->raw.len > 0;
         c = c->next)
        n++;
    return n;
}

bool tls_set_ca_file(const char *path)
{
    certs_init();
    if (path == NULL)
        return true;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return true; // No custom certificates
    fclose(f);
    // > 0: some certificates of the file couldn't be parsed (the others are
    // used)
    return mbedtls_x509_crt_parse_file(&g.cas, path) == 0;
}

// DSHA's own entropy (platform_entropy()) is added to Mbed TLS's default
// sources: on the DS those are only the WiFi chip's weak random register.
static int platform_source(void *data, unsigned char *out, size_t len,
                           size_t *olen)
{
    (void)data;
    platform_entropy(out, len);
    *olen = len;
    return 0;
}

static void entropy_setup(mbedtls_entropy_context *ctx)
{
    mbedtls_entropy_init(ctx);
    mbedtls_entropy_add_source(ctx, platform_source, NULL, 32,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);
}

#if defined(MBEDTLS_PSA_CRYPTO_C) && !defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
// The PSA API (used for the TLS 1.3 key exchange) has its own random
// generator. This Mbed TLS 3.6 function (library/psa_crypto_invasive.h, not
// in the public headers) chooses its entropy sources.
psa_status_t mbedtls_psa_crypto_configure_entropy_sources(
    void (*entropy_init)(mbedtls_entropy_context *ctx),
    void (*entropy_free)(mbedtls_entropy_context *ctx));
#define CONFIGURE_PSA_ENTROPY 1
#endif

static bool rng_init(char *err, size_t err_len)
{
    if (g.rng_ready)
        return true;

#if defined(MBEDTLS_USE_PSA_CRYPTO) || defined(MBEDTLS_SSL_PROTO_TLS1_3)
#ifdef CONFIGURE_PSA_ENTROPY
    mbedtls_psa_crypto_configure_entropy_sources(entropy_setup,
                                                 mbedtls_entropy_free);
#endif
    // TLS 1.3 does its crypto through the PSA API
    if (psa_crypto_init() != PSA_SUCCESS)
    {
        set_err(err, err_len, "Can't start the crypto library%s", NULL, 0);
        return false;
    }
#endif

    // Seeded on the first connection, not at startup: by then the DS has
    // gathered more entropy (timing of WiFi packets and of the user's input).
    entropy_setup(&g.entropy);
    mbedtls_ctr_drbg_init(&g.drbg);
    static const char pers[] = "DSHA TLS client";
    int rc = mbedtls_ctr_drbg_seed(&g.drbg, mbedtls_entropy_func, &g.entropy,
                                   (const unsigned char *)pers,
                                   sizeof(pers) - 1);
    if (rc != 0)
    {
        set_err(err, err_len, "No random numbers for TLS%s (-0x%04x)", NULL,
                -rc);
        return false;
    }
    g.rng_ready = true;
    return true;
}

static int verify_cb(void *user, mbedtls_x509_crt *crt, int depth,
                     uint32_t *flags)
{
    (void)user;
    (void)crt;
    (void)depth;
    if (time(NULL) < CLOCK_PLAUSIBLE)
        *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED
                              | MBEDTLS_X509_BADCERT_FUTURE);
    return 0;
}

static bool conf_init(char *err, size_t err_len)
{
    if (g.conf_ready)
        return true;

    certs_init();
    mbedtls_ssl_config_init(&g.conf);
    int rc = mbedtls_ssl_config_defaults(&g.conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0)
    {
        set_err(err, err_len, "TLS setup failed%s (-0x%04x)", NULL, -rc);
        return false;
    }

    // Always verify the server: an unverified HTTPS connection would hand the
    // password to whoever answers.
    mbedtls_ssl_conf_authmode(&g.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g.conf, &g.cas, NULL);
    mbedtls_ssl_conf_verify(&g.conf, verify_cb, NULL);
    mbedtls_ssl_conf_rng(&g.conf, mbedtls_ctr_drbg_random, &g.drbg);

    // Fastest key exchanges first
    static const uint16_t groups[] = {
        MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
        MBEDTLS_SSL_IANA_TLS_GROUP_NONE,
    };
    mbedtls_ssl_conf_groups(&g.conf, groups);

    g.conf_ready = true;
    return true;
}

// Socket callbacks (the socket is non-blocking)
// ---------------------------------------------

static bool would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
           || errno == EINPROGRESS;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_conn *t = ctx;
    int n = (int)send(t->fd, buf, len, MSG_NOSIGNAL);
    if (n > 0)
        return n;
    if (n < 0 && would_block())
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_conn *t = ctx;
    int n = (int)recv(t->fd, buf, len, 0);
    if (n >= 0)
        return n; // 0: end of the connection
    if (would_block())
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

// Handshake
// ---------

typedef struct {
    tls_conn *t;
    int timeout_ms;
    int rc;
    bool timed_out;
} handshake_job;

static int handshake(void *arg)
{
    handshake_job *job = arg;
    uint32_t start = platform_millis();
    int rc;
    while ((rc = mbedtls_ssl_handshake(&job->t->ssl)) != 0)
    {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            break;
        if ((int32_t)(platform_millis() - start) > job->timeout_ms)
        {
            job->timed_out = true;
            break;
        }
        platform_yield();
    }
    job->rc = rc;
    return 0;
}

static void verify_error(uint32_t flags, const char *host, char *err,
                         size_t err_len)
{
    if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
        set_err(err, err_len, "The HTTPS certificate isn't valid for %s",
                host, 0);
    else if (flags & (MBEDTLS_X509_BADCERT_EXPIRED
                      | MBEDTLS_X509_BADCERT_FUTURE))
        set_err(err, err_len, "The HTTPS certificate of %s has expired, or "
                "the DS clock is wrong", host, 0);
    else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
        set_err(err, err_len, "The HTTPS certificate of %s isn't trusted "
                "(self-signed? see /dsha/ca.pem)", host, 0);
    else
        set_err(err, err_len, "Invalid HTTPS certificate%s (0x%x)", NULL,
                (int)flags);
}

tls_conn *tls_connect(int fd, const char *host, int timeout_ms, char *err,
                      size_t err_len)
{
    if (!rng_init(err, err_len) || !conf_init(err, err_len))
        return NULL;

    tls_conn *t = calloc(1, sizeof(*t));
    if (t == NULL)
    {
        set_err(err, err_len, "Out of memory%s", NULL, 0);
        return NULL;
    }
    t->fd = fd;
    mbedtls_ssl_init(&t->ssl);

    int rc = mbedtls_ssl_setup(&t->ssl, &g.conf);
    if (rc == 0)
        rc = mbedtls_ssl_set_hostname(&t->ssl, host); // SNI and name check
    if (rc != 0)
    {
        set_err(err, err_len, "Out of memory for TLS%s (-0x%04x)", NULL, -rc);
        tls_close(t);
        return NULL;
    }
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);

    handshake_job job = { t, timeout_ms, 0, false };
    if (job.timeout_ms < HANDSHAKE_MIN_TIMEOUT_MS)
        job.timeout_ms = HANDSHAKE_MIN_TIMEOUT_MS;
    platform_run_big_stack(handshake, &job);

    if (job.rc == 0)
        return t;

    uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
    if (job.rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0
        && flags != (uint32_t)-1)
        verify_error(flags, host, err, err_len);
    else if (job.timed_out)
        set_err(err, err_len, "Timeout in the HTTPS connection to %s", host,
                0);
    else if (job.rc == MBEDTLS_ERR_SSL_CONN_EOF
             || job.rc == MBEDTLS_ERR_NET_RECV_FAILED
             || job.rc == MBEDTLS_ERR_NET_SEND_FAILED)
        set_err(err, err_len, "%s closed the HTTPS connection (is it "
                "really HTTPS?)", host, 0);
    else if (job.rc == MBEDTLS_ERR_SSL_INVALID_RECORD
             || job.rc == MBEDTLS_ERR_SSL_UNSUPPORTED_EXTENSION
             || job.rc == MBEDTLS_ERR_SSL_DECODE_ERROR)
        set_err(err, err_len, "%s doesn't speak HTTPS on this port (try "
                "http://)", host, 0);
    else
        set_err(err, err_len, "HTTPS error with %s (-0x%04x)", host, -job.rc);
    tls_close(t);
    return NULL;
}

// Data
// ----

int tls_write(tls_conn *t, const void *data, size_t len)
{
    int rc = mbedtls_ssl_write(&t->ssl, data, len);
    if (rc >= 0)
        return rc;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return TLS_AGAIN;
    return TLS_ERROR;
}

int tls_read(tls_conn *t, void *buf, size_t len)
{
    int rc = mbedtls_ssl_read(&t->ssl, buf, len);
    if (rc > 0)
        return rc;
    if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
        || rc == MBEDTLS_ERR_SSL_CONN_EOF)
        return 0;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return TLS_AGAIN;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    // TLS 1.3 sends session tickets after the handshake: just keep reading
    if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        return TLS_AGAIN;
#endif
    return TLS_ERROR;
}

void tls_close(tls_conn *t)
{
    if (t == NULL)
        return;
    mbedtls_ssl_close_notify(&t->ssl); // Best effort, don't wait
    mbedtls_ssl_free(&t->ssl);
    free(t);
}
