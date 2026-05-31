/*

    mirrors everything on the oled screen

*/


#include "oled_console.h"
#include "oled.h"
#include "pico/stdio/driver.h"
#include <string.h>
// spacing works on a typical oled screen
#define CON_ROWS  OLED_ROWS    /* 8  */
#define CON_COLS  OLED_COLS    /* 21 */

static char     s_buf[CON_ROWS][CON_COLS + 1];
static int      s_row = 0;
static int      s_col = 0;
static bool     s_enabled = false;
static bool     s_dirty = false;

/* ANSI escape swallow state: 0=normal, 1=saw ESC, 2=inside CSI. */
static int      s_esc = 0;

static void con_clear(void) {
    for (int r = 0; r < CON_ROWS; r++) s_buf[r][0] = '\0';
    s_row = s_col = 0;
}

static void con_scroll(void) {
    for (int r = 1; r < CON_ROWS; r++)
        memcpy(s_buf[r - 1], s_buf[r], CON_COLS + 1);
    s_buf[CON_ROWS - 1][0] = '\0';
    s_row = CON_ROWS - 1;
    s_col = 0;
}

static void con_newline(void) {
    s_col = 0;
    if (s_row < CON_ROWS - 1) s_row++;
    else con_scroll();
}

static void con_putc(char c) {
    /* swallow ANSI escape sequences */
    if (s_esc == 1) { s_esc = (c == '[') ? 2 : 0; return; }
    if (s_esc == 2) { if ((c >= '@' && c <= '~')) s_esc = 0; return; }

    switch (c) {
        case 27:   s_esc = 1; return;          /* ESC */
        case '\r': s_col = 0; return;
        case '\n': con_newline(); s_dirty = true; return;
        case '\b': if (s_col > 0) { s_col--; s_buf[s_row][s_col] = '\0'; } return;
        case '\t': c = ' '; break;             /* treat tab as space */
        default: break;
    }
    if (c < 32 || c > 126) return;             /* skip other control chars */

    if (s_col >= CON_COLS) con_newline();
    s_buf[s_row][s_col++] = c;
    s_buf[s_row][s_col]   = '\0';
    s_dirty = true;
}

static void con_render(void) {
    if (!s_dirty || !oled_is_ready()) return;
    oled_clear();
    for (int r = 0; r < CON_ROWS; r++)
        oled_text(0, r, s_buf[r], false);
    oled_flush();
    s_dirty = false;
}

static void oled_out_chars(const char *buf, int len) {
    if (!s_enabled) return;
    bool saw_nl = false;
    for (int i = 0; i < len; i++) {
        con_putc(buf[i]);
        if (buf[i] == '\n') saw_nl = true;
    }
    if (saw_nl) con_render();    /* redraw per line to limit I2C traffic */
}

static void oled_out_flush(void) {
    if (s_enabled) con_render();
}

static stdio_driver_t s_oled_driver = {
    .out_chars = oled_out_chars,
    .out_flush = oled_out_flush,
    .in_chars  = NULL,
};

void oled_console_enable(bool on) {
    if (on == s_enabled) return;
    s_enabled = on;
    if (on) {
        con_clear();
        s_dirty = true;
        stdio_set_driver_enabled(&s_oled_driver, true);
        if (oled_is_ready()) { oled_clear(); oled_flush(); }
    } else {
        stdio_set_driver_enabled(&s_oled_driver, false);
    }
}

bool oled_console_enabled(void) { return s_enabled; }
