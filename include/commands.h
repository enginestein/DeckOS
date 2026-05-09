#ifndef COMMANDS_H
#define COMMANDS_H

#define MAX_ARGS  8
#define INPUT_SIZE 128

typedef struct {
    const char* name;
    void (*handler)(int argc, char* argv[]);
    const char* description;
} command_t;

void commands_init();
void commands_execute(char* input);
void commands_list();

#endif