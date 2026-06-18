#ifndef SCRIPT_H
#define SCRIPT_H

#define SCRIPT_MAX_VARS     32
#define SCRIPT_VAR_NAME_LEN 16
#define SCRIPT_VAR_VAL_LEN  64
#define SCRIPT_MAX_LINES    256
#define SCRIPT_LINE_LEN     256
#define SCRIPT_MAX_CALL_DEPTH 8

typedef struct {
    char name[SCRIPT_VAR_NAME_LEN];
    char value[SCRIPT_VAR_VAL_LEN];
} script_var_t;

typedef struct {
    script_var_t vars[SCRIPT_MAX_VARS];
    int var_count;

    int exit_code;

} script_ctx_t;

void script_ctx_init(script_ctx_t* ctx);
int  script_run_file(const char* vfs_path);
int  script_run_string(script_ctx_t* ctx, const char* source);

#endif