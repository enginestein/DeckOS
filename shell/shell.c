#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "shell.h"
#include "commands.h"

#define INPUT_SIZE 128

static char input_buffer[INPUT_SIZE];
static int  input_pos = 0;

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

void shell_init() {
    commands_init();
    printf("[shell] initialized\n");
    printf("\nType 'help' for available commands.\n\n");
    printf("> ");
}

void shell_run() {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

    if (c == '\r' || c == '\n') {
        printf("\n");
        input_buffer[input_pos] = '\0';
        trim(input_buffer);
        if (strlen(input_buffer) > 0)
            commands_execute(input_buffer);
        input_pos = 0;
        memset(input_buffer, 0, INPUT_SIZE);
        printf("> ");

    } else if (c == 127 || c == '\b') {
        if (input_pos > 0) {
            input_pos--;
            input_buffer[input_pos] = '\0';
            printf("\b \b");
        }

    } else if (c == 3) {
        printf("^C\n> ");
        input_pos = 0;
        memset(input_buffer, 0, INPUT_SIZE);

    } else if (c == 4) {
        printf("\n[uptime] ");
        char u[] = "uptime";
        commands_execute(u);
        printf("> ");

    } else {
        if (input_pos < INPUT_SIZE - 1) {
            input_buffer[input_pos++] = (char)c;
            putchar(c);
        }
    }
}