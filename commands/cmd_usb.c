/*
   usb      - manage the FAT12 USB mass-storage disk and bridge to the VFS
   hid      - act as a USB keyboard and type into the connected host
   console  - mirror shell output to the OLED (standalone handheld mode)
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include "tusb.h"
#include "class/hid/hid.h"

#include "commands.h"
#include "fat_disk.h"
#include "usb_hid.h"
#include "oled_console.h"
#include "oled.h"
#include "vfs.h"

extern void msc_disk_mark_changed(void);

static void usb_print_status(void) {
    uint32_t blk  = fat_disk_block_count();
    uint32_t bs   = fat_disk_block_size();
    uint32_t cap  = blk * bs;
    uint32_t used = fat_disk_bytes_used();

    printf("USB mass storage (FAT12 RAM disk)\n");
    printf("  host link : %s\n", tud_mounted() ? "mounted" : "not mounted");
    printf("  capacity  : %lu KB  (%lu x %lu B sectors)\n",
           (unsigned long)(cap / 1024), (unsigned long)blk, (unsigned long)bs);
    printf("  used      : %lu B  (%d file(s))\n",
           (unsigned long)used, fat_disk_count());
    printf("  tip       : drop files on the drive, then `usb import <NAME>`\n");
}

static void usb_list(void) {
    int n = fat_disk_count();
    if (n == 0) { printf("(disk empty)\n"); return; }
    printf("  %-13s  %s\n", "name", "size");
    for (int i = 0; i < n; i++) {
        char nm[13]; uint32_t sz;
        if (fat_disk_entry(i, nm, &sz) == 0)
            printf("  %-13s  %lu B\n", nm, (unsigned long)sz);
    }
}

/* Copy one VFS file onto the USB disk. */
static void usb_export_one(const char *vfspath, const char *diskname) {
    static uint8_t buf[VFS_MAX_FILE_SIZE];
    uint32_t len = 0;
    if (vfs_read(vfspath, buf, sizeof(buf), &len) < 0) return;   /* vfs_read prints error */

    const char *name = diskname ? diskname : vfspath;
    int rc = fat_disk_add_file(name, buf, len);
    if (rc == 0) {
        printf("exported '%s' -> disk (%lu B)\n", vfspath, (unsigned long)len);
        msc_disk_mark_changed();
    } else if (rc == -1)  printf("usb: disk directory full\n");
    else                  printf("usb: disk out of space\n");
    fflush(stdout);
}

/* Copy one USB-disk file into the VFS. */
static void usb_import_one(const char *name, const char *vfsdir) {
    static uint8_t buf[VFS_MAX_FILE_SIZE];
    uint32_t len = 0;
    if (fat_disk_read_file(name, buf, sizeof(buf), &len) != 0) {
        printf("usb: '%s' not found on disk\n", name);
        return;
    }
    char path[VFS_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", vfsdir ? vfsdir : "/home", name);
    if (vfs_write(path, buf, len, false) >= 0) {
        printf("imported '%s' -> %s (%lu B)\n", name, path, (unsigned long)len);
        fflush(stdout);
    }
}

void cmd_usb(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) { usb_print_status(); return; }

    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "ls") == 0) {
        usb_list();
    } else if (strcmp(argv[1], "format") == 0) {
        fat_disk_format();
        msc_disk_mark_changed();
        printf("usb: disk reformatted\n");
    } else if (strcmp(argv[1], "rm") == 0) {
        if (argc < 3) { printf("usage: usb rm <NAME>\n"); return; }
        if (fat_disk_delete_file(argv[2]) == 0) {
            msc_disk_mark_changed();
            printf("deleted '%s' from disk\n", argv[2]);
        } else printf("usb: '%s' not found\n", argv[2]);
    } else if (strcmp(argv[1], "export") == 0) {
        if (argc < 3) { printf("usage: usb export <vfspath> [diskname]\n"); return; }
        usb_export_one(argv[2], argc >= 4 ? argv[3] : NULL);
    } else if (strcmp(argv[1], "import") == 0) {
        if (argc < 3) { printf("usage: usb import <NAME> [vfsdir]\n"); return; }
        usb_import_one(argv[2], argc >= 4 ? argv[3] : NULL);
    } else if (strcmp(argv[1], "sync") == 0) {
        /* Export every file currently in /home (top level) to the disk. */
        int exported = 0;
        for (int i = 1; i < VFS_MAX_NODES; i++) {
            extern vfs_node_t s_nodes[];
            if (!s_nodes[i].used || s_nodes[i].type != VFS_FILE) continue;
            int parent = s_nodes[i].parent;
            int home = vfs_resolve("/home");
            if (parent != home) continue;
            char path[VFS_PATH_LEN];
            snprintf(path, sizeof(path), "/home/%s", s_nodes[i].name);
            usb_export_one(path, s_nodes[i].name);
            exported++;
        }
        printf("usb: synced %d file(s) from /home to disk\n", exported);
    } else {
        printf("usb: status | list | export <vfspath> [name] | import <NAME> [dir] |\n");
        printf("     sync | rm <NAME> | format\n");
    }
}


static uint8_t key_name_to_code(const char *s) {
    if (strcasecmp(s, "ENTER") == 0 || strcasecmp(s, "RETURN") == 0) return HID_KEY_ENTER;
    if (strcasecmp(s, "TAB")   == 0) return HID_KEY_TAB;
    if (strcasecmp(s, "ESC")   == 0 || strcasecmp(s, "ESCAPE") == 0) return HID_KEY_ESCAPE;
    if (strcasecmp(s, "SPACE") == 0) return HID_KEY_SPACE;
    if (strcasecmp(s, "BACKSPACE") == 0 || strcasecmp(s, "BKSP") == 0) return HID_KEY_BACKSPACE;
    if (strcasecmp(s, "DEL")   == 0 || strcasecmp(s, "DELETE") == 0) return HID_KEY_DELETE;
    if (strcasecmp(s, "UP")    == 0) return HID_KEY_ARROW_UP;
    if (strcasecmp(s, "DOWN")  == 0) return HID_KEY_ARROW_DOWN;
    if (strcasecmp(s, "LEFT")  == 0) return HID_KEY_ARROW_LEFT;
    if (strcasecmp(s, "RIGHT") == 0) return HID_KEY_ARROW_RIGHT;
    if (strcasecmp(s, "HOME")  == 0) return HID_KEY_HOME;
    if (strcasecmp(s, "END")   == 0) return HID_KEY_END;
    if (s[0] == 'F' && isdigit((unsigned char)s[1])) {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 12) return (uint8_t)(HID_KEY_F1 + (n - 1));
    }
    if (strlen(s) == 1) {
        char c = (char)tolower((unsigned char)s[0]);
        if (c >= 'a' && c <= 'z') return (uint8_t)(HID_KEY_A + (c - 'a'));
        if (c >= '1' && c <= '9') return (uint8_t)(HID_KEY_1 + (c - '1'));
        if (c == '0') return HID_KEY_0;
    }
    return 0;
}

static uint8_t mod_name_to_bit(const char *s) {
    if (strcasecmp(s, "CTRL") == 0 || strcasecmp(s, "CONTROL") == 0) return KEYBOARD_MODIFIER_LEFTCTRL;
    if (strcasecmp(s, "SHIFT") == 0) return KEYBOARD_MODIFIER_LEFTSHIFT;
    if (strcasecmp(s, "ALT")  == 0)  return KEYBOARD_MODIFIER_LEFTALT;
    if (strcasecmp(s, "GUI")  == 0 || strcasecmp(s, "WIN") == 0 ||
        strcasecmp(s, "CMD")  == 0 || strcasecmp(s, "SUPER") == 0) return KEYBOARD_MODIFIER_LEFTGUI;
    return 0;
}

/* Parse one combo token like "CTRL+ALT+DEL" or "WIN+r" or "F5". */
static bool hid_tap_combo(const char *token) {
    char tmp[64];
    strncpy(tmp, token, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    uint8_t modifier = 0;
    uint8_t keycode  = 0;
    char *save = NULL;
    for (char *part = strtok_r(tmp, "+", &save); part; part = strtok_r(NULL, "+", &save)) {
        uint8_t m = mod_name_to_bit(part);
        if (m) { modifier |= m; continue; }
        keycode = key_name_to_code(part);
    }
    if (keycode == 0) { printf("hid: unknown key '%s'\n", token); return false; }
    return usb_hid_tap(modifier, keycode);
}

static int mouse_button_from_name(const char *name) {
    if (strcasecmp(name, "left") == 0) return MOUSE_BUTTON_LEFT;
    if (strcasecmp(name, "right") == 0) return MOUSE_BUTTON_RIGHT;
    if (strcasecmp(name, "middle") == 0) return MOUSE_BUTTON_MIDDLE;
    if (strcasecmp(name, "both") == 0) return MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT;
    return atoi(name);
}

void cmd_hid(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        printf("USB HID\n");
        printf("  host link : %s\n", tud_mounted() ? "mounted" : "not mounted");
        printf("  ready     : %s\n", usb_hid_ready() ? "yes" : "no");
        printf("  KB: type <text> | line <text> | key <COMBO...> | enter\n");
        printf("  MS: move <x> <y> | click <left|right|middle|both> | scroll <n>\n");
        return;
    }

    if (!tud_mounted()) { printf("hid: USB not connected to a host\n"); return; }

    if (strcmp(argv[1], "type") == 0 || strcmp(argv[1], "line") == 0) {
        if (argc < 3) { printf("usage: hid %s <text...>\n", argv[1]); return; }
        char text[INPUT_SIZE];
        int n = 0;
        for (int i = 2; i < argc && n < (int)sizeof(text) - 1; i++) {
            if (i > 2) text[n++] = ' ';
            n += snprintf(text + n, sizeof(text) - (size_t)n, "%s", argv[i]);
        }
        text[n] = '\0';
        int sent = usb_hid_type_str(text);
        if (strcmp(argv[1], "line") == 0) usb_hid_tap(0, HID_KEY_ENTER);
        printf("hid: typed %d char(s)%s\n", sent,
               strcmp(argv[1], "line") == 0 ? " + Enter" : "");
    } else if (strcmp(argv[1], "enter") == 0) {
        usb_hid_tap(0, HID_KEY_ENTER);
        printf("hid: Enter\n");
    } else if (strcmp(argv[1], "key") == 0) {
        if (argc < 3) { printf("usage: hid key <COMBO> [COMBO...]\n"); return; }
        int ok = 0;
        for (int i = 2; i < argc; i++) if (hid_tap_combo(argv[i])) ok++;
        printf("hid: sent %d key(s)\n", ok);
    } else if (strcmp(argv[1], "move") == 0) {
        if (argc < 4) { printf("usage: hid move <x> <y>\n"); return; }
        int8_t x = (int8_t)atoi(argv[2]);
        int8_t y = (int8_t)atoi(argv[3]);
        if (usb_hid_mouse_move(x, y)) printf("hid: mouse moved (%d,%d)\n", x, y);
        else printf("hid: mouse move failed\n");
    } else if (strcmp(argv[1], "click") == 0) {
        int btn = argc >= 3 ? mouse_button_from_name(argv[2]) : MOUSE_BUTTON_LEFT;
        if (usb_hid_mouse_click((uint8_t)btn)) printf("hid: mouse click (0x%x)\n", btn);
        else printf("hid: mouse click failed\n");
    } else if (strcmp(argv[1], "scroll") == 0) {
        int8_t n = (int8_t)(argc >= 3 ? atoi(argv[2]) : 1);
        if (usb_hid_mouse_scroll(n)) printf("hid: mouse scroll %d\n", n);
        else printf("hid: mouse scroll failed\n");
    } else {
        printf("hid: status | type | line | key | enter | move | click | scroll\n");
    }
}

void cmd_console(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        printf("OLED console mirror: %s\n", oled_console_enabled() ? "ON" : "OFF");
        printf("  usage: console oled on|off\n");
        return;
    }
    if (strcmp(argv[1], "oled") == 0) {
        bool on = (argc >= 3 && strcmp(argv[2], "on") == 0);
        bool off = (argc >= 3 && strcmp(argv[2], "off") == 0);
        if (!on && !off) { printf("usage: console oled on|off\n"); return; }
        if (on && !oled_is_ready()) {
            if (!oled_init()) { printf("console: OLED not found on GP4/GP5\n"); return; }
        }
        oled_console_enable(on);
        printf("console: OLED mirror %s\n", on ? "enabled" : "disabled");
    } else {
        printf("console: oled on|off | status\n");
    }
}
