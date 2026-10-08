/* Offline tests use only synthetic keys. SPDX-License-Identifier: LGPL-2.1-or-later */
#include "libfprint/drivers/goodixtls/goodix_psk.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static void expect_failure(const char *path)
{
    guint8 output[32];
    guint8 zero[32] = {0};
    GError *error = NULL;
    memset(output, 0xaa, sizeof(output));
    g_assert_false(goodix_psk_load(path, output, &error));
    g_assert_nonnull(error);
    g_assert_cmpmem(output, sizeof(output), zero, sizeof(zero));
    g_error_free(error);
}

int main(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("goodix-key-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "key", NULL);
    char *link = g_build_filename(dir, "link", NULL);
    char *hard = g_build_filename(dir, "hard", NULL);
    char *fifo = g_build_filename(dir, "fifo", NULL);
    guint8 synthetic[33], output[32], digest[32];
    for (int i = 0; i < 33; i++) synthetic[i] = i + 1;
    expect_failure(path);
    expect_failure("relative/key");
    g_assert_true(g_file_set_contents(path, (char *)synthetic, 32, &error));
    g_assert_no_error(error);
    g_assert_cmpint(chmod(path, 0600), ==, 0);
    g_assert_true(goodix_psk_load(path, output, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(output, 32, synthetic, 32);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    gsize length = sizeof(digest);
    g_checksum_update(sum, synthetic, 32);
    g_checksum_get_digest(sum, digest, &length);
    g_checksum_free(sum);
    g_assert_true(goodix_psk_matches(output, digest, 32));
    digest[0] ^= 1;
    g_assert_false(goodix_psk_matches(output, digest, 32));
    g_assert_false(goodix_psk_matches(output, digest, 31));
    g_assert_false(goodix_psk_matches(output, NULL, 32));
    g_assert_cmpint(chmod(path, 0644), ==, 0);
    expect_failure(path);
    g_assert_cmpint(chmod(path, 0600), ==, 0);
    g_assert_cmpint(linkat(AT_FDCWD, path, AT_FDCWD, hard, 0), ==, 0);
    expect_failure(path);
    g_assert_cmpint(unlink(hard), ==, 0);
    g_assert_cmpint(symlink(path, link), ==, 0);
    expect_failure(link);
    g_assert_cmpint(unlink(link), ==, 0);
    g_assert_cmpint(mkfifo(fifo, 0600), ==, 0);
    expect_failure(fifo);
    expect_failure(dir);
    for (int n = 31; n <= 33; n += 2) {
        g_assert_true(g_file_set_contents(path, (char *)synthetic, n, &error));
        g_assert_no_error(error);
        g_assert_cmpint(chmod(path, 0600), ==, 0);
        expect_failure(path);
    }
    memset(synthetic, 0, 32);
    g_assert_true(g_file_set_contents(path, (char *)synthetic, 32, &error));
    g_assert_no_error(error);
    g_assert_cmpint(chmod(path, 0600), ==, 0);
    expect_failure(path);
    g_assert_cmpint(unlink(path), ==, 0);
    g_assert_cmpint(unlink(fifo), ==, 0);
    g_assert_cmpint(rmdir(dir), ==, 0);
    g_free(hard); g_free(path); g_free(link); g_free(fifo); g_free(dir);
    g_print("Offline existing-key tests passed (synthetic data only).\n");
    return 0;
}
