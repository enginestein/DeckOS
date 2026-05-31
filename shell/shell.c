#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "shell.h"
#include "commands.h"

#define HISTORY_SIZE    8
#define HISTORY_LINE    128

static char history[HISTORY_SIZE][HISTORY_LINE];
static int  hist_count  = 0;
static int  hist_cursor = 0;

static void history_push(const char* line) {
    if (!line || !*line) return;
    if (hist_count > 0) {
        int prev = (hist_count - 1) % HISTORY_SIZE;
        if (strcmp(history[prev], line) == 0) return;
    }
    strncpy(history[hist_count % HISTORY_SIZE], line, HISTORY_LINE - 1);
    history[hist_count % HISTORY_SIZE][HISTORY_LINE - 1] = '\0';
    hist_count++;
}

static const char* history_get(int back) {
    if (back <= 0 || back > hist_count) return NULL;
    int idx = (hist_count - back) % HISTORY_SIZE;
    return history[idx];
}

void shell_history_dump(void) {
    int shown = hist_count < HISTORY_SIZE ? hist_count : HISTORY_SIZE;
    if (shown == 0) { printf("(history empty)\n"); return; }
    for (int i = shown; i >= 1; i--) {
        const char* line = history_get(i);
        if (line) printf("  %3d  %s\n", hist_count - i + 1, line);
    }
}

void shell_history_clear(void) {
    hist_count  = 0;
    hist_cursor = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) history[i][0] = '\0';
}

static char input_buf[INPUT_SIZE];
static int  input_pos = 0;



static void shell_prompt(void) {
    printf("> ");
    fflush(stdout);  
}

static void line_clear_display(void) {
    printf("\r\033[2K> ");
    fflush(stdout);
    input_pos    = 0;
    input_buf[0] = '\0';
}

static void line_replace(const char* s) {
    line_clear_display();
    if (!s) return;
    strncpy(input_buf, s, INPUT_SIZE - 1);
    input_buf[INPUT_SIZE - 1] = '\0';
    input_pos = (int)strlen(input_buf);
    printf("%s", input_buf);
    fflush(stdout);
}



static int esc_state = 0;

static bool parse_escape(int c, char* vk) {
    switch (esc_state) {
        case 0:
            if (c == 27) { esc_state = 1; return true; }
            return false;
        case 1:
            if (c == '[') { esc_state = 2; return true; }
            esc_state = 0;
            return false;
        case 2:
            esc_state = 0;
            if (c == 'A') { *vk = 'U'; return true; }  
            if (c == 'B') { *vk = 'D'; return true; }  
            return true;  
        default:
            esc_state = 0;
            return false;
    }
}



static void trim(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, (size_t)(len - start + 1));
}



void shell_init(void) {
    commands_init();
    printf("[shell] initialized  (history: %d slots)\n", HISTORY_SIZE);
    printf("\nType 'help' for available commands.\n\n");
    fflush(stdout);
    shell_prompt();
}

void shell_run(void) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

   
    char vk = 0;
    if (parse_escape(c, &vk)) {
        if (vk == 'U') {                         
            hist_cursor++;
            const char* h = history_get(hist_cursor);
            if (h) {
                line_replace(h);
            } else {
                hist_cursor--;
            }
        } else if (vk == 'D') {                  
            hist_cursor--;
            if (hist_cursor <= 0) {
                hist_cursor = 0;
                line_replace(NULL);
            } else {
                line_replace(history_get(hist_cursor));
            }
        }
        return;
    }

   
    if (c == '\r' || c == '\n') {
        printf("\n");
        fflush(stdout);
        input_buf[input_pos] = '\0';
        trim(input_buf);
        if (strlen(input_buf) > 0) {
            history_push(input_buf);
            hist_cursor = 0;
            commands_execute(input_buf);
           
            fflush(stdout);
        }
        input_pos = 0;
        memset(input_buf, 0, INPUT_SIZE);
        shell_prompt();

   
    } else if (c == 127 || c == '\b') {
        if (input_pos > 0) {
            input_pos--;
            input_buf[input_pos] = '\0';
            printf("\b \b");
            fflush(stdout);
        }

   
    } else if (c == 3) {
        printf("^C\n");
        fflush(stdout);
        input_pos   = 0;
        memset(input_buf, 0, INPUT_SIZE);
        hist_cursor = 0;
        shell_prompt();

   
    } else if (c == 4) {
        printf("\n[uptime] ");
        fflush(stdout);
        char u[] = "uptime";
        commands_execute(u);
        fflush(stdout);
        shell_prompt();

   
    } else if (c == 12) {
        printf("\033[2J\033[H");
        fflush(stdout);
        shell_prompt();
        printf("%s", input_buf);
        fflush(stdout);

   
    } else {
        if (input_pos < INPUT_SIZE - 1) {
            input_buf[input_pos++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}