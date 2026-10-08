/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Exercise cancellation without saving any fingerprint template or image. */
#include <fprint.h>
#include <stdio.h>
#include <sys/prctl.h>

typedef struct { GCancellable *cancel; gboolean fired; } CancelState;
static gboolean cancel_now(gpointer data)
{
    CancelState *state = data;
    state->fired = TRUE;
    g_cancellable_cancel(state->cancel);
    return G_SOURCE_REMOVE;
}
int main(void)
{
    if (prctl(PR_SET_DUMPABLE, 0) != 0) return 70;
    setvbuf(stdout, NULL, _IONBF, 0);
    g_autoptr(FpContext) ctx = fp_context_new();
    fp_context_enumerate(ctx);
    GPtrArray *devices = fp_context_get_devices(ctx);
    FpDevice *dev = NULL;
    for (guint i = 0; i < devices->len; i++) {
        FpDevice *candidate = g_ptr_array_index(devices, i);
        if (!g_strcmp0(fp_device_get_driver(candidate), "goodixtls53xd")) dev = candidate;
    }
    if (!dev) { fputs("Sensor not found\n", stderr); return 1; }
    for (guint round = 0; round < 2; round++) {
        g_autoptr(GError) error = NULL;
        printf("Opening cancellation cycle %u\n", round + 1);
        if (!fp_device_open_sync(dev, NULL, &error)) {
            fprintf(stderr, "Open failed: %s\n", error->message); return 2;
        }
        g_autoptr(GCancellable) cancel = g_cancellable_new();
        CancelState state = { cancel, FALSE };
        /* Enroll consumes a floating reference; retain our own nonfloating one. */
        g_autoptr(FpPrint) template = g_object_ref_sink(fp_print_new(dev));
        guint timer = g_timeout_add(round ? 1200 : 250, cancel_now, &state);
        gint64 start = g_get_monotonic_time();
        printf("Starting cancellable enrollment cycle %u\n", round + 1);
        g_autoptr(FpPrint) enrolled = fp_device_enroll_sync(dev, template, cancel, NULL, NULL, &error);
        if (!state.fired) g_source_remove(timer);
        gboolean passed = enrolled == NULL && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (!passed) fprintf(stderr, "Cancellation result: %s\n", error ? error->message : "unexpected enrollment");
        g_clear_error(&error);
        if (!fp_device_close_sync(dev, NULL, &error)) {
            fprintf(stderr, "Close failed: %s\n", error->message); return 3;
        }
        if (!passed) return 4;
        printf("Cancellation and close cycle %u passed (%" G_GINT64_FORMAT " ms).\n", round + 1,
               (g_get_monotonic_time() - start) / 1000);
    }
    g_autoptr(GError) error = NULL;
    if (!fp_device_open_sync(dev, NULL, &error) || !fp_device_close_sync(dev, NULL, &error)) {
        fprintf(stderr, "Final reopen failed: %s\n", error->message); return 5;
    }
    puts("Cancellation/reopen passed. No fingerprint template or image saved.");
    return 0;
}
