#include <openssl/ssl.h>
#include <pthread.h>
#include "drivers/goodixtls/goodixtls.h"

static void discard_result(GoodixTlsServer *server, GError *error, gpointer data)
{
  g_clear_error(&error);
}

static unsigned int client_psk(SSL *ssl, const char *hint, char *identity,
                               unsigned int identity_len, unsigned char *psk,
                               unsigned int psk_len)
{
  g_assert_cmpuint(identity_len, >, 4);
  g_assert_cmpuint(psk_len, >=, 32);
  memcpy(identity, "test", 5);
  memset(psk, 0, 32);
  return 32;
}

static void close_during_handshake(void)
{
  for (int i = 0; i < 10; i++) {
    GoodixTlsServer server = { .connection_callback = discard_result };
    g_autoptr(GError) error = NULL;
    g_assert_true(goodix_tls_server_init(&server, &error));
    g_assert_no_error(error);
    g_assert_true(goodix_tls_server_deinit(&server, &error));
    g_assert_false(server.thread_started);
    g_assert_cmpint(server.sock_fd, ==, -1);
    g_assert_cmpint(server.client_fd, ==, -1);
  }
}

static void stalled_handshake_times_out(void)
{
  GoodixTlsServer server = { .connection_callback = discard_result };
  g_autoptr(GError) error = NULL;
  guint8 buffer[64];
  g_assert_true(goodix_tls_server_init(&server, &error));
  gint64 start = g_get_monotonic_time();
  g_assert_cmpint(goodix_tls_client_recv(&server, buffer, sizeof buffer), ==, -1);
  g_assert_cmpint(g_get_monotonic_time() - start, <, 5000000);
  goodix_tls_server_deinit(&server, &error);
}

static void valid_handshake_and_image(void)
{
  GoodixTlsServer server = { .connection_callback = discard_result };
  g_autoptr(GError) error = NULL;
  g_assert_true(goodix_tls_server_init(&server, &error));
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
  SSL_CTX_set_cipher_list(ctx, "PSK:@SECLEVEL=0");
  SSL_CTX_set_psk_client_callback(ctx, client_psk);
  SSL *client = SSL_new(ctx);
  SSL_set_fd(client, server.client_fd);
  g_assert_cmpint(SSL_connect(client), ==, 1);
  g_assert_cmpint(SSL_write(client, "image", 5), ==, 5);
  guint8 buffer[16];
  pthread_join(server.serve_thread, NULL);
  server.thread_started = FALSE;
  g_assert_true(server.connected);
  g_assert_cmpint(goodix_tls_server_receive(&server, buffer, sizeof buffer, &error), ==, 5);
  g_assert_no_error(error);
  g_assert_cmpmem(buffer, 5, "image", 5);
  SSL_free(client);
  SSL_CTX_free(ctx);
  goodix_tls_server_deinit(&server, &error);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/goodix-tls/close-pending", close_during_handshake);
  g_test_add_func("/goodix-tls/timeout", stalled_handshake_times_out);
  g_test_add_func("/goodix-tls/valid-image", valid_handshake_and_image);
  return g_test_run();
}
