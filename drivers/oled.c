/**
 * oled.c  –  SSD1306 128×64 I2C OLED driver for DeckOS
 *
 * Wire:  SDA = GP4   SCL = GP5   (I2C0, matches existing i2c commands)
 * Addr:  0x3C  (jumper / SA0 LOW)
 *
 * All drawing goes into a 1-KB software framebuffer; call oled_flush()
 * to push it to the display in a single DMA-like I2C burst.
 */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "oled.h"


#define SSD1306_SET_CONTRAST        0x81
#define SSD1306_DISPLAY_ALL_ON_RAM  0xA4
#define SSD1306_DISPLAY_ALL_ON      0xA5
#define SSD1306_DISPLAY_NORMAL      0xA6
#define SSD1306_DISPLAY_INVERT      0xA7
#define SSD1306_DISPLAY_OFF         0xAE
#define SSD1306_DISPLAY_ON          0xAF
#define SSD1306_SET_MEM_MODE        0x20
#define SSD1306_SET_COL_ADDR        0x21
#define SSD1306_SET_PAGE_ADDR       0x22
#define SSD1306_SET_DISP_START_LINE 0x40
#define SSD1306_SET_SEG_REMAP       0xA0
#define SSD1306_SET_MUX_RATIO       0xA8
#define SSD1306_SET_COM_OUT_DIR     0xC0
#define SSD1306_SET_DISP_OFFSET     0xD3
#define SSD1306_SET_COM_PIN_CFG     0xDA
#define SSD1306_SET_DISP_CLK_DIV    0xD5
#define SSD1306_SET_PRECHARGE       0xD9
#define SSD1306_SET_VCOM_DESEL      0xDB
#define SSD1306_CHARGE_PUMP         0x8D


#define SSD1306_SCROLL_RIGHT        0x26
#define SSD1306_SCROLL_LEFT         0x27
#define SSD1306_SCROLL_STOP         0x2E
#define SSD1306_SCROLL_START        0x2F


static uint8_t s_fb[OLED_WIDTH * OLED_PAGES];   
static bool    s_ready = false;



static const uint8_t FONT5x8[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, 
    {0x00,0x00,0x5F,0x00,0x00}, 
    {0x00,0x07,0x00,0x07,0x00}, 
    {0x14,0x7F,0x14,0x7F,0x14}, 
    {0x24,0x2A,0x7F,0x2A,0x12}, 
    {0x23,0x13,0x08,0x64,0x62}, 
    {0x36,0x49,0x55,0x22,0x50}, 
    {0x00,0x05,0x03,0x00,0x00}, 
    {0x00,0x1C,0x22,0x41,0x00}, 
    {0x00,0x41,0x22,0x1C,0x00}, 
    {0x14,0x08,0x3E,0x08,0x14}, 
    {0x08,0x08,0x3E,0x08,0x08}, 
    {0x00,0x50,0x30,0x00,0x00}, 
    {0x08,0x08,0x08,0x08,0x08}, 
    {0x00,0x60,0x60,0x00,0x00}, 
    {0x20,0x10,0x08,0x04,0x02}, 
    {0x3E,0x51,0x49,0x45,0x3E}, 
    {0x00,0x42,0x7F,0x40,0x00}, 
    {0x42,0x61,0x51,0x49,0x46}, 
    {0x21,0x41,0x45,0x4B,0x31}, 
    {0x18,0x14,0x12,0x7F,0x10}, 
    {0x27,0x45,0x45,0x45,0x39}, 
    {0x3C,0x4A,0x49,0x49,0x30}, 
    {0x01,0x71,0x09,0x05,0x03}, 
    {0x36,0x49,0x49,0x49,0x36}, 
    {0x06,0x49,0x49,0x29,0x1E}, 
    {0x00,0x36,0x36,0x00,0x00}, 
    {0x00,0x56,0x36,0x00,0x00}, 
    {0x08,0x14,0x22,0x41,0x00}, 
    {0x14,0x14,0x14,0x14,0x14}, 
    {0x00,0x41,0x22,0x14,0x08}, 
    {0x02,0x01,0x51,0x09,0x06}, 
    {0x32,0x49,0x79,0x41,0x3E}, 
    {0x7E,0x11,0x11,0x11,0x7E}, 
    {0x7F,0x49,0x49,0x49,0x36}, 
    {0x3E,0x41,0x41,0x41,0x22}, 
    {0x7F,0x41,0x41,0x22,0x1C}, 
    {0x7F,0x49,0x49,0x49,0x41}, 
    {0x7F,0x09,0x09,0x09,0x01}, 
    {0x3E,0x41,0x49,0x49,0x7A}, 
    {0x7F,0x08,0x08,0x08,0x7F}, 
    {0x00,0x41,0x7F,0x41,0x00}, 
    {0x20,0x40,0x41,0x3F,0x01}, 
    {0x7F,0x08,0x14,0x22,0x41}, 
    {0x7F,0x40,0x40,0x40,0x40}, 
    {0x7F,0x02,0x0C,0x02,0x7F}, 
    {0x7F,0x04,0x08,0x10,0x7F}, 
    {0x3E,0x41,0x41,0x41,0x3E}, 
    {0x7F,0x09,0x09,0x09,0x06}, 
    {0x3E,0x41,0x51,0x21,0x5E}, 
    {0x7F,0x09,0x19,0x29,0x46}, 
    {0x46,0x49,0x49,0x49,0x31}, 
    {0x01,0x01,0x7F,0x01,0x01}, 
    {0x3F,0x40,0x40,0x40,0x3F}, 
    {0x1F,0x20,0x40,0x20,0x1F}, 
    {0x3F,0x40,0x38,0x40,0x3F}, 
    {0x63,0x14,0x08,0x14,0x63}, 
    {0x07,0x08,0x70,0x08,0x07}, 
    {0x61,0x51,0x49,0x45,0x43}, 
    {0x00,0x7F,0x41,0x41,0x00}, 
    {0x02,0x04,0x08,0x10,0x20}, 
    {0x00,0x41,0x41,0x7F,0x00}, 
    {0x04,0x02,0x01,0x02,0x04}, 
    {0x40,0x40,0x40,0x40,0x40}, 
    {0x00,0x01,0x02,0x04,0x00}, 
    {0x20,0x54,0x54,0x54,0x78}, 
    {0x7F,0x48,0x44,0x44,0x38}, 
    {0x38,0x44,0x44,0x44,0x20}, 
    {0x38,0x44,0x44,0x48,0x7F}, 
    {0x38,0x54,0x54,0x54,0x18}, 
    {0x08,0x7E,0x09,0x01,0x02}, 
    {0x0C,0x52,0x52,0x52,0x3E}, 
    {0x7F,0x08,0x04,0x04,0x78}, 
    {0x00,0x44,0x7D,0x40,0x00}, 
    {0x20,0x40,0x44,0x3D,0x00}, 
    {0x7F,0x10,0x28,0x44,0x00}, 
    {0x00,0x41,0x7F,0x40,0x00}, 
    {0x7C,0x04,0x18,0x04,0x78}, 
    {0x7C,0x08,0x04,0x04,0x78}, 
    {0x38,0x44,0x44,0x44,0x38}, 
    {0x7C,0x14,0x14,0x14,0x08}, 
    {0x08,0x14,0x14,0x18,0x7C}, 
    {0x7C,0x08,0x04,0x04,0x08}, 
    {0x48,0x54,0x54,0x54,0x20}, 
    {0x04,0x3F,0x44,0x40,0x20}, 
    {0x3C,0x40,0x40,0x20,0x7C}, 
    {0x1C,0x20,0x40,0x20,0x1C}, 
    {0x3C,0x40,0x30,0x40,0x3C}, 
    {0x44,0x28,0x10,0x28,0x44}, 
    {0x0C,0x50,0x50,0x50,0x3C}, 
    {0x44,0x64,0x54,0x4C,0x44}, 
    {0x00,0x08,0x36,0x41,0x00}, 
    {0x00,0x00,0x7F,0x00,0x00}, 
    {0x00,0x41,0x36,0x08,0x00}, 
    {0x10,0x08,0x08,0x10,0x08}, 
};



static void oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = { 0x00, cmd };   
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, buf, 2, false);
}

static void oled_cmd2(uint8_t cmd, uint8_t arg) {
    uint8_t buf[3] = { 0x00, cmd, arg };
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, buf, 3, false);
}

static inline void fb_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8, bit = y % 8;
    if (on) s_fb[page * OLED_WIDTH + x] |=  (1u << bit);
    else    s_fb[page * OLED_WIDTH + x] &= ~(1u << bit);
}



bool oled_init(void) {
    if (gpio_get_function(OLED_SDA_PIN) != GPIO_FUNC_I2C) {
        i2c_init(OLED_I2C_PORT, OLED_I2C_HZ);
        gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
        gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
        gpio_pull_up(OLED_SDA_PIN);
        gpio_pull_up(OLED_SCL_PIN);
        sleep_ms(10);
    }
    i2c_init(OLED_I2C_PORT, OLED_I2C_HZ);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);
    sleep_ms(10);

    
    uint8_t rxbuf;
    if (i2c_read_timeout_us(OLED_I2C_PORT, OLED_I2C_ADDR,
                            &rxbuf, 1, false, 5000) < 0) {
        return false;
    }

    static const uint8_t init_seq[] = {
        SSD1306_DISPLAY_OFF,
        SSD1306_SET_DISP_CLK_DIV, 0x80,
        SSD1306_SET_MUX_RATIO,    0x3F,   
        SSD1306_SET_DISP_OFFSET,  0x00,
        SSD1306_SET_DISP_START_LINE | 0x00,
        SSD1306_CHARGE_PUMP,      0x14,   
        SSD1306_SET_MEM_MODE,     0x00,   
        SSD1306_SET_SEG_REMAP | 0x01,     
        SSD1306_SET_COM_OUT_DIR | 0x08,   
        SSD1306_SET_COM_PIN_CFG,  0x12,
        SSD1306_SET_CONTRAST,     0xCF,
        SSD1306_SET_PRECHARGE,    0xF1,
        SSD1306_SET_VCOM_DESEL,   0x40,
        SSD1306_DISPLAY_ALL_ON_RAM,
        SSD1306_DISPLAY_NORMAL,
        SSD1306_DISPLAY_ON,
    };
    for (size_t i = 0; i < sizeof(init_seq); i++)
        oled_cmd(init_seq[i]);

    memset(s_fb, 0, sizeof(s_fb));
    oled_flush();
    s_ready = true;
    return true;
}

void oled_deinit(void) {
    if (!s_ready) return;
    oled_cmd(SSD1306_DISPLAY_OFF);
    s_ready = false;
}

bool oled_is_ready(void) { return s_ready; }



void oled_flush(void) {
    oled_cmd(SSD1306_SET_COL_ADDR);  oled_cmd(0); oled_cmd(OLED_WIDTH - 1);
    oled_cmd(SSD1306_SET_PAGE_ADDR); oled_cmd(0); oled_cmd(OLED_PAGES - 1);

    
    
    uint8_t buf[OLED_WIDTH + 1];
    buf[0] = 0x40;
    for (int p = 0; p < OLED_PAGES; p++) {
        memcpy(buf + 1, s_fb + p * OLED_WIDTH, OLED_WIDTH);
        i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR,
                           buf, sizeof(buf), false);
    }
}

void oled_clear(void) { memset(s_fb, 0x00, sizeof(s_fb)); }
void oled_fill(uint8_t p) { memset(s_fb, p,    sizeof(s_fb)); }



void oled_on(void)             { oled_cmd(SSD1306_DISPLAY_ON); }
void oled_off(void)            { oled_cmd(SSD1306_DISPLAY_OFF); }
void oled_contrast(uint8_t l)  { oled_cmd2(SSD1306_SET_CONTRAST, l); }
void oled_invert(bool inv)     { oled_cmd(inv ? SSD1306_DISPLAY_INVERT : SSD1306_DISPLAY_NORMAL); }
void oled_flip(bool h, bool v) {
    oled_cmd(SSD1306_SET_SEG_REMAP   | (h ? 0x00 : 0x01));
    oled_cmd(SSD1306_SET_COM_OUT_DIR | (v ? 0x00 : 0x08));
}



void oled_pixel(int x, int y, bool on) { fb_pixel(x, y, on); }

void oled_hline(int x0, int x1, int y, bool on) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) fb_pixel(x, y, on);
}

void oled_vline(int x, int y0, int y1, bool on) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) fb_pixel(x, y, on);
}

void oled_line(int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        fb_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { if (x0 == x1) break; err += dy; x0 += sx; }
        if (e2 <= dx) { if (y0 == y1) break; err += dx; y0 += sy; }
    }
}

void oled_rect(int x, int y, int w, int h, bool on) {
    oled_hline(x, x+w-1, y,     on);
    oled_hline(x, x+w-1, y+h-1, on);
    oled_vline(x,     y, y+h-1, on);
    oled_vline(x+w-1, y, y+h-1, on);
}

void oled_rect_fill(int x, int y, int w, int h, bool on) {
    for (int row = y; row < y + h; row++)
        oled_hline(x, x + w - 1, row, on);
}

void oled_circle(int cx, int cy, int r, bool on) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        fb_pixel(cx+x, cy+y, on); fb_pixel(cx-x, cy+y, on);
        fb_pixel(cx+x, cy-y, on); fb_pixel(cx-x, cy-y, on);
        fb_pixel(cx+y, cy+x, on); fb_pixel(cx-y, cy+x, on);
        fb_pixel(cx+y, cy-x, on); fb_pixel(cx-y, cy-x, on);
        if (d < 0) d += 4*x+6; else { d += 4*(x-y)+10; y--; }
        x++;
    }
}

void oled_circle_fill(int cx, int cy, int r, bool on) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r) fb_pixel(cx+x, cy+y, on);
}

void oled_triangle(int x0,int y0,int x1,int y1,int x2,int y2,bool on) {
    oled_line(x0,y0,x1,y1,on);
    oled_line(x1,y1,x2,y2,on);
    oled_line(x2,y2,x0,y0,on);
}

void oled_bitmap(int x, int y, int w, int h, const uint8_t *bmp) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            int byte = (row * w + col) / 8;
            int bit  = 7 - (col % 8);
            fb_pixel(x + col, y + row, (bmp[byte] >> bit) & 1);
        }
}



void oled_char(int col, int row, char c, bool inv) {
    int x = col * FONT_W;
    int y = row * FONT_H;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = FONT5x8[c - 0x20];
    for (int cx = 0; cx < 5; cx++) {
        uint8_t col_bits = inv ? ~glyph[cx] : glyph[cx];
        for (int cy = 0; cy < 8; cy++)
            fb_pixel(x + cx, y + cy, (col_bits >> cy) & 1);
    }
    
    for (int cy = 0; cy < 8; cy++)
        fb_pixel(x + 5, y + cy, inv);
}

void oled_text(int col, int row, const char *str, bool inv) {
    while (*str && col < OLED_COLS)
        oled_char(col++, row, *str++, inv);
}

void oled_textxy(int x, int y, const char *str, bool inv) {
    while (*str) {
        if (*str < 0x20 || *str > 0x7E) { str++; x += FONT_W; continue; }
        const uint8_t *glyph = FONT5x8[*str - 0x20];
        for (int cx = 0; cx < 5; cx++) {
            uint8_t bits = inv ? ~glyph[cx] : glyph[cx];
            for (int cy = 0; cy < 8; cy++)
                fb_pixel(x + cx, y + cy, (bits >> cy) & 1);
        }
        for (int cy = 0; cy < 8; cy++) fb_pixel(x + 5, y + cy, inv);
        x += FONT_W; str++;
    }
}

void oled_printf(int col, int row, bool inv, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    oled_text(col, row, buf, inv);
}



void oled_scroll_right(uint8_t sp, uint8_t ep) {
    oled_cmd(SSD1306_SCROLL_RIGHT); oled_cmd(0x00);
    oled_cmd(sp); oled_cmd(0x00); oled_cmd(ep);
    oled_cmd(0x00); oled_cmd(0xFF);
    oled_cmd(SSD1306_SCROLL_START);
}
void oled_scroll_left(uint8_t sp, uint8_t ep) {
    oled_cmd(SSD1306_SCROLL_LEFT); oled_cmd(0x00);
    oled_cmd(sp); oled_cmd(0x00); oled_cmd(ep);
    oled_cmd(0x00); oled_cmd(0xFF);
    oled_cmd(SSD1306_SCROLL_START);
}
void oled_scroll_stop(void) { oled_cmd(SSD1306_SCROLL_STOP); }



void oled_progress_bar(int x, int y, int w, int h, int pct) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    oled_rect(x, y, w, h, true);
    int fill = (w - 2) * pct / 100;
    if (fill > 0) oled_rect_fill(x + 1, y + 1, fill, h - 2, true);
}

void oled_waveform(int x, int y, int w, int h,
                   const int16_t *samples, int n) {
    oled_rect(x, y, w, h, true);
    for (int i = 0; i < w - 2 && i < n - 1; i++) {
        int s0 = samples[i * n / (w - 2)];
        int s1 = samples[(i + 1) * n / (w - 2)];
        
        int y0 = y + 1 + (h - 2) / 2 - (int)((long)s0 * (h-2) / 2 / 32768);
        int y1 = y + 1 + (h - 2) / 2 - (int)((long)s1 * (h-2) / 2 / 32768);
        if (y0 < y+1) y0 = y+1; if (y0 > y+h-2) y0 = y+h-2;
        if (y1 < y+1) y1 = y+1; if (y1 > y+h-2) y1 = y+h-2;
        oled_line(x + 1 + i, y0, x + 2 + i, y1, true);
    }
}

void oled_bar_chart(int x, int y, int w, int h,
                    const int *vals, int n, int max_val,
                    const char **labels) {
    oled_rect(x, y, w, h, true);
    if (n <= 0 || max_val <= 0) return;
    int bar_w = (w - 2) / n;
    for (int i = 0; i < n; i++) {
        int bh = (int)((long)vals[i] * (h - 3) / max_val);
        if (bh < 0) bh = 0; if (bh > h-3) bh = h-3;
        int bx = x + 1 + i * bar_w;
        int by = y + h - 1 - bh;
        if (bh > 0) oled_rect_fill(bx, by, bar_w - 1, bh, true);
        if (labels && labels[i])
            oled_textxy(bx, y + h - FONT_H - 1, labels[i], false);
    }
}

void oled_title_bar(const char *title) {
    oled_rect_fill(0, 0, OLED_WIDTH, FONT_H, true);
    
    int len = 0;
    for (const char *p = title; *p; p++) len++;
    int cx = (OLED_COLS - len) / 2;
    if (cx < 0) cx = 0;
    oled_text(cx, 0, title, true);   
}

void oled_status_bar(const char *left, const char *right) {
    int row = OLED_ROWS - 1;
    oled_rect_fill(0, row * FONT_H, OLED_WIDTH, FONT_H, true);
    oled_text(0, row, left, true);
    
    int len = 0;
    for (const char *p = right; *p; p++) len++;
    int col = OLED_COLS - len;
    if (col < 0) col = 0;
    oled_text(col, row, right, true);
}

void oled_splash(const char *line1, const char *line2) {
    oled_clear();
    
    oled_rect(0, 0, OLED_WIDTH, OLED_HEIGHT, true);
    oled_rect(2, 2, OLED_WIDTH-4, OLED_HEIGHT-4, true);
    int l1 = 0; for (const char *p=line1; *p; p++) l1++;
    int l2 = 0; for (const char *p=line2; *p; p++) l2++;
    oled_text((OLED_COLS-l1)/2, 2, line1, false);
    oled_text((OLED_COLS-l2)/2, 4, line2, false);
    oled_flush();
}

void oled_notification(const char *msg, uint32_t dur_ms) {
    
    uint8_t backup[OLED_WIDTH * 3];
    memcpy(backup, s_fb, sizeof(backup));

    oled_rect_fill(0, 0, OLED_WIDTH, 26, false);
    oled_rect(1, 1, OLED_WIDTH - 2, 24, true);
    oled_text(1, 0, "[ NOTIFY ]", false);
    oled_text(1, 1, msg, false);
    oled_flush();
    sleep_ms(dur_ms);

    memcpy(s_fb, backup, sizeof(backup));
    oled_flush();
}

void oled_scroll_text_h(int row, const char *str, uint32_t delay_ms) {
    int len = 0;
    for (const char *p = str; *p; p++) len++;
    
    char buf[OLED_COLS * 3 + 1];
    snprintf(buf, sizeof(buf), "%-*s%s", OLED_COLS, "", str);
    int total = (int)strlen(buf);
    for (int pos = 0; pos < total - OLED_COLS; pos++) {
        
        oled_rect_fill(0, row * FONT_H, OLED_WIDTH, FONT_H, false);
        char slice[OLED_COLS + 1];
        memcpy(slice, buf + pos, OLED_COLS);
        slice[OLED_COLS] = '\0';
        oled_text(0, row, slice, false);
        oled_flush();
        sleep_ms(delay_ms);
    }
}

void oled_spinner(int x, int y, int frame) {
    static const char glyphs[] = { '-','\\','|','/','-','\\','|','/' };
    char s[2] = { glyphs[frame & 7], '\0' };
    oled_textxy(x, y, s, false);
}

void oled_animate_boot(void) {
    oled_clear();

    
    oled_text(3, 1, "  DeckOS v3.0", false);
    oled_hline(0, 127, 10, true);
    oled_hline(0, 127, 11, true);
    oled_flush(); sleep_ms(120);

    
    for (int p = 0; p <= 100; p += 4) {
        oled_progress_bar(10, 28, 108, 10, p);
        char pbuf[12];
        snprintf(pbuf, sizeof(pbuf), "Boot %3d%%", p);
        oled_text(4, 5, pbuf, false);
        oled_flush();
        sleep_ms(18);
    }

    oled_text(5, 6, "  Ready!  ", false);
    oled_flush();
    sleep_ms(600);
    oled_clear();
    oled_flush();
}