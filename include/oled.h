#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define OLED_I2C_PORT   i2c0
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    5
#define OLED_I2C_ADDR   0x3C        
#define OLED_I2C_HZ     400000      

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)   

#define FONT_W          6           
#define FONT_H          8           
#define OLED_COLS       (OLED_WIDTH  / FONT_W)   
#define OLED_ROWS       (OLED_HEIGHT / FONT_H)   

bool    oled_init(void);
void    oled_deinit(void);
bool    oled_is_ready(void);

void    oled_clear(void);
void    oled_fill(uint8_t pattern);
void    oled_flush(void);           
void    oled_on(void);
void    oled_off(void);
void    oled_contrast(uint8_t level);   
void    oled_invert(bool inv);
void    oled_flip(bool horizontal, bool vertical);
void    oled_pixel(int x, int y, bool on);
void    oled_hline(int x0, int x1, int y, bool on);
void    oled_vline(int x, int y0, int y1, bool on);
void    oled_line(int x0, int y0, int x1, int y1, bool on);
void    oled_rect(int x, int y, int w, int h, bool on);
void    oled_rect_fill(int x, int y, int w, int h, bool on);
void    oled_circle(int cx, int cy, int r, bool on);
void    oled_circle_fill(int cx, int cy, int r, bool on);
void    oled_triangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on);
void    oled_bitmap(int x, int y, int w, int h, const uint8_t *bmp);
void    oled_char(int col, int row, char c, bool invert);
void    oled_text(int col, int row, const char *str, bool invert);
void    oled_textxy(int x, int y, const char *str, bool invert);
void    oled_printf(int col, int row, bool invert, const char *fmt, ...);
void    oled_scroll_right(uint8_t start_page, uint8_t end_page);
void    oled_scroll_left(uint8_t start_page, uint8_t end_page);
void    oled_scroll_stop(void);
void    oled_progress_bar(int x, int y, int w, int h, int pct);  
void    oled_waveform(int x, int y, int w, int h,
                      const int16_t *samples, int n_samples);
void    oled_bar_chart(int x, int y, int w, int h,
                       const int *values, int n, int max_val,
                       const char **labels);
void    oled_title_bar(const char *title);
void    oled_status_bar(const char *left, const char *right);
void    oled_splash(const char *line1, const char *line2);
void    oled_notification(const char *msg, uint32_t duration_ms);
void    oled_scroll_text_h(int row, const char *str, uint32_t delay_ms);
void    oled_animate_boot(void);
void    oled_spinner(int x, int y, int frame);   