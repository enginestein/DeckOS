#pragma once

#include <stdbool.h>
#include "dscript.h"

#define RC_OK 0
#define RC_BREAK -10
#define RC_CONTINUE -11
#define RC_RETURN -12
#define RC_EXIT -20
#define RC_ERROR -1

/* ---- util ---- */
void trim_inplace(char *s);
int  split(const char *s, char parts[][SCRIPT_LINE_LEN], int max);

/* ---- block matching ---- */
int find_end(char lines[][SCRIPT_LINE_LEN], int total, int from,
             const char *kw_open, const char *kw_close);
int find_elif_else_end(char lines[][SCRIPT_LINE_LEN], int total,
                       int from, const char *which);
int find_def(char lines[][SCRIPT_LINE_LEN], int total, const char *fname,
             int *body_start, int *body_end);

/* ---- variables ---- */
const char *var_get(script_ctx_t *ctx, const char *name);
void        var_set(script_ctx_t *ctx, const char *name, const char *val);
void        expand_vars(script_ctx_t *ctx, const char *in, char *out, int outlen);
void        arr_key(char *buf, int buflen, const char *name, int idx);
void        arr_lenkey(char *buf, int buflen, const char *name);
int         arr_get_len(script_ctx_t *ctx, const char *name);
void        arr_set_len(script_ctx_t *ctx, const char *name, int len);

/* ---- expressions ---- */
int  eval_int(script_ctx_t *ctx, const char *expr);
bool eval_cond(script_ctx_t *ctx, const char *expr);
int  eval_arith(script_ctx_t *ctx, const char *expr, double *num_out,
                char *str_out, int str_len);
int  parse_numargs(const char *expanded, double *v, int maxn);

/* ---- builtins ---- */
void builtin_math(script_ctx_t *ctx, const char *dest, const char *fname,
                  const char *args);
bool try_builtin_val(script_ctx_t *ctx, const char *vname, const char *valexpr,
                     char lines[][SCRIPT_LINE_LEN], int total);

/* ---- include ---- */
int do_include(script_ctx_t *ctx, const char *path);

/* ---- core ---- */
int run_lines(script_ctx_t *ctx, char lines[][SCRIPT_LINE_LEN],
              int total, int start, int end);
