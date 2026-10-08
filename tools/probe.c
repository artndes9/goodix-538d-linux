/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <sys/prctl.h>
#include <fprint.h>

int main(int argc, char **argv)
{
    if (prctl(PR_SET_DUMPABLE, 0) != 0) return 70;
    unsigned rounds = 1;
    if (argc == 2 && g_strcmp0(argv[1], "--twice") == 0) rounds = 2;
    else if (argc != 1) return 64;
    g_autoptr(FpContext) context = fp_context_new();
    g_autoptr(GError) error = NULL;
    fp_context_enumerate(context);
    GPtrArray *devices = fp_context_get_devices(context);
    for (guint i = 0; i < devices->len; i++) {
        FpDevice *device = g_ptr_array_index(devices, i);
        if (g_strcmp0(fp_device_get_driver(device), "goodixtls53xd"))
            continue;
        printf("Detected: %s\n", fp_device_get_name(device));
        fflush(stdout);
        for (unsigned round = 0; round < rounds; round++) {
        printf("Open/close cycle %u of %u\n", round + 1, rounds);
        if (!fp_device_open_sync(device, NULL, &error)) {
            fprintf(stderr, "Sensor open failed: %s\n", error->message);
            return 2;
        }
        puts("Sensor opened successfully: firmware, existing pairing hash, authenticated TLS handshake and device ACK passed.");
        if (!fp_device_close_sync(device, NULL, &error)) {
            fprintf(stderr, "Sensor close failed: %s\n", error->message);
            return 3;
        }
        puts("Sensor closed. No fingerprint was captured or enrolled.");
        }
        return 0;
    }
    fputs("No Goodix 538d device found.\n", stderr);
    return 1;
}
