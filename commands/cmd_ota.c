#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "commands.h"
#include "ota.h"
#include "vfs.h"
#include "dscript.h"

static bool s_pending = false;
static uint32_t s_expected = 0;
static uint32_t s_received = 0;

void cmd_ota(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  ota update <vfs-path>\n");
        printf("  ota status\n");
        printf("  ota cancel\n");
        return;
    }

    if (!strcmp(argv[1], "update")) {
        if (argc < 3) { printf("ota: missing file path\n"); return; }

        // Read firmware from VFS file
        uint8_t *buf = (uint8_t *)malloc(OTA_STAGING_SIZE - 16);
        if (!buf) { printf("ota: out of memory\n"); return; }

        uint32_t flen = 0;
        if (vfs_read(argv[2], buf, OTA_STAGING_SIZE - 16, &flen) < 0) {
            printf("ota: can't read '%s'\n", argv[2]);
            free(buf);
            return;
        }
        if (flen < 256) {
            printf("ota: file too small (%u bytes)\n", flen);
            free(buf);
            return;
        }

        printf("ota: read %u bytes from '%s'\n", flen, argv[2]);

        // Align firmware size to 256-byte page boundary for flash programming
        uint32_t aligned = (flen + 255) & ~255;
        if (aligned > OTA_STAGING_SIZE - 16) {
            printf("ota: firmware too large (%u > %u)\n", aligned, OTA_STAGING_SIZE - 16);
            free(buf);
            return;
        }

        // Erase the staging area
        flash_range_erase(OTA_STAGING_BASE, OTA_STAGING_SIZE);

        // Write firmware data to staging (skip first 16 bytes header)
        uint32_t data_offs = OTA_STAGING_BASE + 16;
        for (uint32_t off = 0; off < aligned; off += 256) {
            uint32_t chunk = (aligned - off < 256) ? (aligned - off) : 256;
            if (off + chunk > flen)
                memset(buf + off, 0xFF, off + chunk - flen);
            flash_range_program(data_offs + off, buf + off, chunk);
        }

        // Write size and magic at the end of staging
        uint32_t footer_offs = OTA_STAGING_BASE + OTA_SIZE_OFFSET;
        uint32_t footer_page = footer_offs & ~255;
        uint32_t footer_page_off = footer_offs & 255;

        uint8_t fpage[256] __attribute__((aligned(4)));
        memset(fpage, 0xFF, 256);
        *(uint32_t *)(fpage + footer_page_off) = flen;
        *(uint32_t *)(fpage + footer_page_off + 4) = OTA_MAGIC_VAL;
        flash_range_program(footer_page, fpage, 256);

        free(buf);
        printf("ota: staged %u bytes, rebooting...\n", flen);
        fflush(stdout);
        sleep_ms(100);
        watchdog_reboot(0, 0, 0);
        while (1);
    }

    if (!strcmp(argv[1], "status")) {
        uint32_t psz;
        if (ota_pending(&psz))
            printf("ota: pending update (%u bytes) — will apply on next boot\n", psz);
        else if (s_pending)
            printf("ota: receiving  (%u/%u)\n", s_received, s_expected);
        else
            printf("ota: idle\n");
        return;
    }

    if (!strcmp(argv[1], "cancel")) {
        s_pending = false;
        s_expected = s_received = 0;
        printf("ota: cancelled\n");
        return;
    }

    printf("ota: unknown subcommand '%s'\n", argv[1]);
}
