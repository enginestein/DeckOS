#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "module.h"
#include "oled.h"

static void cmd_oled_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  oled init                          - init display (GP4=SDA GP5=SCL)\n");
    printf("  oled on|off                        - power display\n");
    printf("  oled clear                         - blank framebuffer\n");
    printf("  oled fill <hex>                    - fill fb with pattern\n");
    printf("  oled flush                         - push framebuffer to screen\n");
    printf("  oled contrast <0-255>              - set brightness\n");
    printf("  oled invert <0|1>                  - invert display\n");
    printf("  oled flip <h:0|1> <v:0|1>          - mirror display\n");
    printf("  oled text <col> <row> <str>        - draw text at grid cell\n");
    printf("  oled textxy <x> <y> <str>          - draw text at pixel coords\n");
    printf("  oled printf <col> <row> <fmt...>   - formatted text\n");
    printf("  oled pixel <x> <y> <0|1>           - set/clear pixel\n");
    printf("  oled line <x0> <y0> <x1> <y1>      - draw line\n");
    printf("  oled hline <x0> <x1> <y>           - horizontal line\n");
    printf("  oled vline <x> <y0> <y1>           - vertical line\n");
    printf("  oled rect <x> <y> <w> <h>          - rectangle outline\n");
    printf("  oled rectfill <x> <y> <w> <h>      - filled rectangle\n");
    printf("  oled circle <cx> <cy> <r>          - circle outline\n");
    printf("  oled circlefill <cx> <cy> <r>      - filled circle\n");
    printf("  oled progress <x> <y> <w> <h> <%%> - progress bar\n");
    printf("  oled title <text>                  - title bar\n");
    printf("  oled status <left> <right>         - status bar\n");
    printf("  oled splash <line1> <line2>        - splash screen + flush\n");
    printf("  oled notify <msg> <ms>             - timed notification\n");
    printf("  oled scroll right|left <sp> <ep>   - hardware scroll\n");
    printf("  oled scroll stop                   - stop hardware scroll\n");
    printf("  oled spinner <x> <y> <frame>       - spinner glyph\n");
    printf("  oled boot                          - animated boot sequence\n");
    return;
  }

  const char *sub = argv[1];

  if (strcmp(sub, "init") == 0) {
    bool ok = oled_init();
    printf("oled: %s\n", ok ? "ready" : "not found");
  } else if (strcmp(sub, "on") == 0) {
    oled_on(); printf("oled: on\n");
  } else if (strcmp(sub, "off") == 0) {
    oled_off(); printf("oled: off\n");
  } else if (strcmp(sub, "clear") == 0) {
    oled_clear(); oled_flush(); printf("oled: cleared\n");
  } else if (strcmp(sub, "fill") == 0 && argc >= 3) {
    uint8_t pat = (uint8_t)strtol(argv[2], NULL, 16);
    oled_fill(pat); oled_flush(); printf("oled: filled 0x%02X\n", pat);
  } else if (strcmp(sub, "flush") == 0) {
    oled_flush(); printf("oled: flushed\n");
  } else if (strcmp(sub, "contrast") == 0 && argc >= 3) {
    oled_contrast((uint8_t)atoi(argv[2]));
    printf("oled: contrast %d\n", atoi(argv[2]));
  } else if (strcmp(sub, "invert") == 0 && argc >= 3) {
    oled_invert(atoi(argv[2]) != 0);
    printf("oled: invert %s\n", atoi(argv[2]) ? "on" : "off");
  } else if (strcmp(sub, "flip") == 0 && argc >= 4) {
    oled_flip(atoi(argv[2]) != 0, atoi(argv[3]) != 0);
    printf("oled: flip h=%d v=%d\n", atoi(argv[2]), atoi(argv[3]));
  } else if (strcmp(sub, "text") == 0 && argc >= 5) {
    char buf[128] = "";
    for (int i = 4; i < argc; i++) {
      if (i > 4) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    oled_text(atoi(argv[2]), atoi(argv[3]), buf, false);
    oled_flush();
  } else if (strcmp(sub, "textxy") == 0 && argc >= 5) {
    char buf[128] = "";
    for (int i = 4; i < argc; i++) {
      if (i > 4) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    oled_textxy(atoi(argv[2]), atoi(argv[3]), buf, false);
    oled_flush();
  } else if (strcmp(sub, "printf") == 0 && argc >= 5) {
    char buf[128] = "";
    for (int i = 4; i < argc; i++) {
      if (i > 4) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    oled_text(atoi(argv[2]), atoi(argv[3]), buf, false);
    oled_flush();
  } else if (strcmp(sub, "pixel") == 0 && argc >= 5) {
    oled_pixel(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]) != 0);
    oled_flush();
  } else if (strcmp(sub, "hline") == 0 && argc >= 5) {
    oled_hline(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), true);
    oled_flush();
  } else if (strcmp(sub, "vline") == 0 && argc >= 5) {
    oled_vline(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), true);
    oled_flush();
  } else if (strcmp(sub, "line") == 0 && argc >= 6) {
    oled_line(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), true);
    oled_flush();
  } else if (strcmp(sub, "rect") == 0 && argc >= 6) {
    oled_rect(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), true);
    oled_flush();
  } else if (strcmp(sub, "rectfill") == 0 && argc >= 6) {
    oled_rect_fill(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), true);
    oled_flush();
  } else if (strcmp(sub, "circle") == 0 && argc >= 5) {
    oled_circle(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), true);
    oled_flush();
  } else if (strcmp(sub, "circlefill") == 0 && argc >= 5) {
    oled_circle_fill(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), true);
    oled_flush();
  } else if (strcmp(sub, "progress") == 0 && argc >= 7) {
    oled_progress_bar(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
    oled_flush();
  } else if (strcmp(sub, "title") == 0 && argc >= 3) {
    char buf[128] = "";
    for (int i = 2; i < argc; i++) {
      if (i > 2) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    oled_title_bar(buf);
    oled_flush();
  } else if (strcmp(sub, "status") == 0 && argc >= 4) {
    oled_status_bar(argv[2], argv[3]);
    oled_flush();
  } else if (strcmp(sub, "splash") == 0 && argc >= 4) {
    oled_splash(argv[2], argv[3]);
  } else if (strcmp(sub, "notify") == 0 && argc >= 4) {
    uint32_t ms = (uint32_t)atoi(argv[3]);
    if (ms < 100 || ms > 30000) ms = 2000;
    oled_notification(argv[2], ms);
  } else if (strcmp(sub, "scroll") == 0) {
    if (argc < 3) { printf("usage: oled scroll right|left <sp> <ep> | stop\n"); return; }
    if (strcmp(argv[2], "stop") == 0) { oled_scroll_stop(); printf("oled: scroll stopped\n"); return; }
    if (argc < 5) { printf("usage: oled scroll right|left <start_page> <end_page>\n"); return; }
    uint8_t sp = (uint8_t)atoi(argv[3]), ep = (uint8_t)atoi(argv[4]);
    if (strcmp(argv[2], "right") == 0) oled_scroll_right(sp, ep);
    else if (strcmp(argv[2], "left") == 0) oled_scroll_left(sp, ep);
    else { printf("oled scroll: direction must be right|left\n"); return; }
    printf("oled: scrolling %s pages %d-%d\n", argv[2], sp, ep);
  } else if (strcmp(sub, "spinner") == 0 && argc >= 5) {
    oled_spinner(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    oled_flush();
  } else if (strcmp(sub, "boot") == 0) {
    oled_animate_boot();
  } else {
    printf("oled: unknown subcommand '%s'\n", sub);
  }
}

static module_cmd_t s_cmds[] = {
    {"oled", "OLED display (init/on/off/clear/fill/flush/contrast/invert/flip/text/textxy/printf/pixel/line/hline/vline/rect/rectfill/circle/circlefill/progress/title/status/splash/notify/scroll/spinner/boot)", cmd_oled_handler},
};

static bool mod_oled_load(void) {
    oled_init();
    printf("oled: module loaded\n");
    return true;
}

static void mod_oled_unload(void) {
    oled_off();
    oled_deinit();
    printf("oled: module unloaded\n");
}

plugin_api_t MOD_OLED = {
    .init = mod_oled_load,
    .deinit = mod_oled_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
