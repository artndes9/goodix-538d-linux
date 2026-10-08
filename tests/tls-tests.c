/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Offline synthetic TLS peer. No real key, USB, or device access. */
#include "goodixtls.h"
#include <string.h>
#include <signal.h>

static guint8 client_key[32];
static unsigned int client_psk(SSL *ssl, const char *hint, char *identity,
                               unsigned int identity_len, unsigned char *psk,
                               unsigned int max_psk)
{
    (void)ssl; (void)hint;
    g_assert_cmpuint(identity_len, >, 7);
    g_assert_cmpuint(max_psk, >=, 32);
    strcpy(identity, "Client");
    memcpy(psk, client_key, 32);
    return 32;
}
static void init_server(GoodixTlsServer *server)
{
    for (int i=0; i<32; i++) server->existing_psk[i]=i+1;
    server->has_existing_psk=TRUE;
    GError *error=NULL;
    g_assert_true(goodix_tls_server_init(server, &error));
    g_assert_no_error(error);
}
static SSL *client_new(gboolean wrong)
{
    for (int i=0; i<32; i++) client_key[i]=i+1;
    if (wrong) client_key[0]^=0x80;
    SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());
    g_assert_nonnull(ctx);
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    g_assert_cmpint(SSL_CTX_set_cipher_list(ctx,"PSK-AES128-CBC-SHA256"),==,1);
    SSL_CTX_set_psk_client_callback(ctx,client_psk);
    SSL *ssl=SSL_new(ctx);
    SSL_CTX_free(ctx);
    BIO *in=BIO_new(BIO_s_mem()), *out=BIO_new(BIO_s_mem());
    BIO_set_mem_eof_return(in,-1); BIO_set_mem_eof_return(out,-1);
    SSL_set_bio(ssl,in,out);
    SSL_set_connect_state(ssl);
    return ssl;
}
static gboolean handshake(GoodixTlsServer *server, SSL *client, int fragment)
{
    int state=0;
    for (int n=0;n<20000;n++) {
        int rc=SSL_do_handshake(client);
        if (rc!=1) {
            int err=SSL_get_error(client,rc);
            if (err!=SSL_ERROR_WANT_READ && err!=SSL_ERROR_WANT_WRITE) return FALSE;
        }
        guint8 buffer[4096];
        if (BIO_ctrl_pending(SSL_get_wbio(client))) {
            int size=BIO_read(SSL_get_wbio(client),buffer,fragment);
            g_assert_cmpint(goodix_tls_client_send(server,buffer,size),==,size);
        }
        GError *error=NULL;
        state=goodix_tls_handshake_step(server,&error);
        if (state<0) { g_assert_nonnull(error); g_error_free(error); return FALSE; }
        int size=goodix_tls_client_recv(server,buffer,fragment);
        g_assert_cmpint(size,>=,0);
        if (size) g_assert_cmpint(BIO_write(SSL_get_rbio(client),buffer,size),==,size);
        if (state==1 && SSL_is_init_finished(client)) return TRUE;
    }
    g_error("Handshake did not terminate");
    return FALSE;
}
static void test_valid(void)
{
    for (int round=0;round<6;round++) {
        GoodixTlsServer server={0}; init_server(&server);
        SSL *client=client_new(FALSE);
        g_assert_true(handshake(&server,client,round%2?4096:1));
        g_assert_true(SSL_is_init_finished(server.ssl_layer));
        g_assert_cmpstr(SSL_get_cipher_name(server.ssl_layer),==,"PSK-AES128-CBC-SHA256");
        const char plain[]="synthetic fingerprint payload";
        g_assert_cmpint(SSL_write(client,plain,sizeof(plain)),==,sizeof(plain));
        guint8 encrypted[256], output[256];
        int size=BIO_read(SSL_get_wbio(client),encrypted,sizeof(encrypted));
        g_assert_cmpint(size,>,2);
        g_assert_cmpint(goodix_tls_client_send(&server,encrypted,2),==,2);
        GError *error=NULL;
        g_assert_cmpint(goodix_tls_server_receive(&server,output,sizeof(output),&error),==,-1);
        g_assert_error(error,G_IO_ERROR,G_IO_ERROR_PARTIAL_INPUT);g_clear_error(&error);
        g_assert_cmpint(goodix_tls_client_send(&server,encrypted+2,size-2),==,size-2);
        g_assert_cmpint(goodix_tls_server_receive(&server,output,sizeof(output),&error),==,sizeof(plain));
        g_assert_no_error(error);g_assert_cmpmem(output,sizeof(plain),plain,sizeof(plain));
        SSL_free(client);
        goodix_tls_server_deinit(&server,NULL);goodix_tls_server_deinit(&server,NULL);
        for (guint i=0;i<32;i++) g_assert_cmpuint(server.existing_psk[i],==,0);
        init_server(&server);goodix_tls_server_deinit(&server,NULL);
    }
}
static void test_wrong(void)
{
    GoodixTlsServer server={0};init_server(&server);
    SSL *client=client_new(TRUE);
    g_assert_false(handshake(&server,client,17));
    g_assert_false(SSL_is_init_finished(server.ssl_layer));
    SSL_free(client);goodix_tls_server_deinit(&server,NULL);
}
static void test_tamper(void)
{
    GoodixTlsServer server={0};init_server(&server);
    SSL *client=client_new(FALSE);g_assert_true(handshake(&server,client,4096));
    g_assert_cmpint(SSL_write(client,"test",4),==,4);
    guint8 record[256],output[256];int size=BIO_read(SSL_get_wbio(client),record,sizeof(record));
    record[size-1]^=1;
    goodix_tls_client_send(&server,record,size);
    GError *error=NULL;
    g_assert_cmpint(goodix_tls_server_receive(&server,output,sizeof(output),&error),==,-1);
    g_assert_error(error,G_IO_ERROR,G_IO_ERROR_FAILED);g_clear_error(&error);
    g_assert_true(server.failed);
    SSL_free(client);goodix_tls_server_deinit(&server,NULL);
}
static void test_timeout_cancel(void)
{
    GoodixTlsServer server={0};init_server(&server);
    GError *error=NULL;gint64 start=g_get_monotonic_time();
    g_assert_cmpint(goodix_tls_handshake_step(&server,&error),==,0);
    g_assert_no_error(error);
    g_assert_cmpint(g_get_monotonic_time()-start,<,100000);
    server.handshake_deadline=g_get_monotonic_time()-1;
    g_assert_cmpint(goodix_tls_handshake_step(&server,&error),==,-1);
    g_assert_error(error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT);g_clear_error(&error);
    goodix_tls_server_deinit(&server,NULL);init_server(&server);
    goodix_tls_server_deinit(&server,NULL);
    g_assert_cmpint(goodix_tls_handshake_step(&server,&error),==,-1);
    g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CLOSED);g_clear_error(&error);
}
static void test_missing_zero(void)
{
    GoodixTlsServer server={0};GError *error=NULL;
    g_assert_false(goodix_tls_server_init(&server,&error));g_clear_error(&error);
    server.has_existing_psk=TRUE;
    g_assert_false(goodix_tls_server_init(&server,&error));g_clear_error(&error);
    goodix_tls_server_deinit(&server,NULL);
}
int main(int argc,char **argv)
{
    signal(SIGPIPE,SIG_DFL);
    g_test_init(&argc,&argv,NULL);
    g_test_add_func("/tls/fragmented-valid-reopen",test_valid);
    g_test_add_func("/tls/wrong-key",test_wrong);
    g_test_add_func("/tls/tampered-record",test_tamper);
    g_test_add_func("/tls/timeout-cancel",test_timeout_cancel);
    g_test_add_func("/tls/missing-zero-key",test_missing_zero);
    return g_test_run();
}
