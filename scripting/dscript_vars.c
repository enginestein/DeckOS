#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "dscript_internal.h"

void trim_inplace(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    int st = 0;
    while (s[st] && isspace((unsigned char)s[st])) st++;
    if (st) memmove(s, s + st, (size_t)(len - st + 1));
}

int split(const char *s, char parts[][SCRIPT_LINE_LEN], int max) {
    char tmp[SCRIPT_LINE_LEN];
    strncpy(tmp, s, SCRIPT_LINE_LEN - 1);
    int n = 0;
    char *tok = strtok(tmp, " \t");
    while (tok && n < max) {
        strncpy(parts[n++], tok, SCRIPT_LINE_LEN - 1);
        tok = strtok(NULL, " \t");
    }
    return n;
}

const char *var_get(script_ctx_t *ctx, const char *name) {
    for (int i = 0; i < ctx->var_count; i++)
        if (strcmp(ctx->vars[i].name, name) == 0)
            return ctx->vars[i].value;
    return "";
}

void var_set(script_ctx_t *ctx, const char *name, const char *val) {
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            strncpy(ctx->vars[i].value, val, SCRIPT_VAR_VAL_LEN - 1);
            ctx->vars[i].value[SCRIPT_VAR_VAL_LEN - 1] = '\0';
            return;
        }
    }
    if (ctx->var_count >= SCRIPT_MAX_VARS) {
        printf("script: too many variables\n");
        return;
    }
    strncpy(ctx->vars[ctx->var_count].name, name, SCRIPT_VAR_NAME_LEN - 1);
    strncpy(ctx->vars[ctx->var_count].value, val, SCRIPT_VAR_VAL_LEN - 1);
    ctx->var_count++;
}

void expand_vars(script_ctx_t *ctx, const char *in, char *out, int outlen) {
    int i = 0, o = 0;
    while (in[i] && o < outlen - 1) {
        if (in[i] == '$') {
            i++;
            char vname[SCRIPT_VAR_NAME_LEN] = {0};
            int vn = 0;
            while (in[i] && (isalnum((unsigned char)in[i]) || in[i] == '_') &&
                   vn < SCRIPT_VAR_NAME_LEN - 1)
                vname[vn++] = in[i++];
            const char *val = var_get(ctx, vname);
            while (*val && o < outlen - 1) out[o++] = *val++;
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = '\0';
}

void arr_key(char *buf, int buflen, const char *name, int idx) {
    snprintf(buf, (size_t)buflen, "_arr_%s_%d", name, idx);
}

void arr_lenkey(char *buf, int buflen, const char *name) {
    snprintf(buf, (size_t)buflen, "_arr_%s_len", name);
}

int arr_get_len(script_ctx_t *ctx, const char *name) {
    char key[SCRIPT_VAR_NAME_LEN];
    arr_lenkey(key, sizeof(key), name);
    return atoi(var_get(ctx, key));
}

void arr_set_len(script_ctx_t *ctx, const char *name, int len) {
    char key[SCRIPT_VAR_NAME_LEN];
    arr_lenkey(key, sizeof(key), name);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", len);
    var_set(ctx, key, buf);
}
