#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "dscript_internal.h"
#include "vfs.h"

int find_def(char lines[][SCRIPT_LINE_LEN], int total, const char *fname,
             int *body_start, int *body_end) {
    for (int i = 0; i < total; i++) {
        char buf[SCRIPT_LINE_LEN];
        strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
        trim_inplace(buf);
        if (strncmp(buf, "def ", 4) != 0) continue;
        char defname[SCRIPT_VAR_NAME_LEN] = {0};
        sscanf(buf + 4, "%31s", defname);
        if (strcmp(defname, fname) != 0) continue;

        int depth = 1, j = i + 1;
        while (j < total && depth > 0) {
            char b2[SCRIPT_LINE_LEN];
            strncpy(b2, lines[j], SCRIPT_LINE_LEN - 1);
            trim_inplace(b2);
            char kw[32] = {0};
            sscanf(b2, "%31s", kw);
            if (!strcmp(kw, "def")) depth++;
            if (!strcmp(kw, "enddef")) depth--;
            j++;
        }
        *body_start = i + 1;
        *body_end = j - 1;
        return i;
    }
    return -1;
}

int find_end(char lines[][SCRIPT_LINE_LEN], int total, int from,
             const char *kw_open, const char *kw_close) {
    int depth = 1;
    for (int i = from; i < total; i++) {
        char buf[SCRIPT_LINE_LEN];
        strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
        trim_inplace(buf);
        char first[32] = {0};
        sscanf(buf, "%31s", first);
        if (!strcmp(first, kw_open)) depth++;
        if (!strcmp(first, kw_close)) {
            if (!--depth) return i;
        }
    }
    return -1;
}

int find_elif_else_end(char lines[][SCRIPT_LINE_LEN], int total,
                       int from, const char *which) {
    int depth = 1;
    for (int i = from; i < total; i++) {
        char buf[SCRIPT_LINE_LEN];
        strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
        buf[SCRIPT_LINE_LEN - 1] = '\0';
        trim_inplace(buf);
        char first[32] = {0};
        sscanf(buf, "%31s", first);

        if (!strcmp(first, "if")) depth++;
        if (!strcmp(first, "endif")) {
            depth--;
            if (depth == 0) {
                if (!strcmp(which, "endif")) return i;
                return -1;
            }
        }
        if (depth == 1 && !strcmp(first, which))
            return i;
    }
    return -1;
}

int do_include(script_ctx_t *ctx, const char *path) {
    uint8_t *buf = (uint8_t *)malloc(VFS_MAX_FILE_SIZE);
    if (!buf) {
        printf("include: out of memory\n");
        return RC_ERROR;
    }
    uint32_t flen = 0;
    if (vfs_read(path, buf, VFS_MAX_FILE_SIZE - 1, &flen) < 0) {
        printf("include: file not found: %s\n", path);
        free(buf);
        return RC_ERROR;
    }
    buf[flen] = '\0';
    int rc = script_run_string(ctx, (const char *)buf);
    free(buf);
    return rc;
}
