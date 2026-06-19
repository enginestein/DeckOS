#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "dscript_internal.h"

int eval_int(script_ctx_t *ctx, const char *expr) {
    char buf[64];
    expand_vars(ctx, expr, buf, sizeof(buf));
    trim_inplace(buf);
    return atoi(buf);
}

static bool eval_simple_cond(const char *buf) {
    const char *ops[] = {"==", "!=", "<=", ">=", "<", ">"};
    char tmp[128];
    strncpy(tmp, buf, sizeof(tmp) - 1);

    for (int oi = 0; oi < 6; oi++) {
        char *p = strstr(tmp, ops[oi]);
        if (!p) continue;
        *p = '\0';
        char lhs[64], rhs[64];
        strncpy(lhs, tmp, sizeof(lhs) - 1);
        lhs[sizeof(lhs)-1] = '\0';
        trim_inplace(lhs);
        strncpy(rhs, p + strlen(ops[oi]), sizeof(rhs) - 1);
        rhs[sizeof(rhs)-1] = '\0';
        trim_inplace(rhs);

        char *le, *re;
        long lv = strtol(lhs, &le, 10);
        long rv = strtol(rhs, &re, 10);
        bool numeric = (*le == '\0' && *re == '\0');

        if (numeric) {
            if (!strcmp(ops[oi], "==")) return lv == rv;
            if (!strcmp(ops[oi], "!=")) return lv != rv;
            if (!strcmp(ops[oi], "<")) return lv < rv;
            if (!strcmp(ops[oi], ">")) return lv > rv;
            if (!strcmp(ops[oi], "<=")) return lv <= rv;
            if (!strcmp(ops[oi], ">=")) return lv >= rv;
        } else {
            int cmp = strcmp(lhs, rhs);
            if (!strcmp(ops[oi], "==")) return cmp == 0;
            if (!strcmp(ops[oi], "!=")) return cmp != 0;
            return false;
        }
    }

    if (tmp[0] == '\0') return false;
    char *end;
    long v = strtol(tmp, &end, 10);
    if (*end == '\0') return v != 0;
    return true;
}

bool eval_cond(script_ctx_t *ctx, const char *expr) {
    char buf[256];
    expand_vars(ctx, expr, buf, sizeof(buf));
    trim_inplace(buf);

    char *and = strstr(buf, " && ");
    char *or  = strstr(buf, " || ");

    if (and) {
        *and = '\0';
        char lhs[128], rhs[128];
        strncpy(lhs, buf, sizeof(lhs) - 1);
        trim_inplace(lhs);
        strncpy(rhs, and + 4, sizeof(rhs) - 1);
        trim_inplace(rhs);
        return eval_simple_cond(lhs) && eval_simple_cond(rhs);
    }
    if (or) {
        *or = '\0';
        char lhs[128], rhs[128];
        strncpy(lhs, buf, sizeof(lhs) - 1);
        trim_inplace(lhs);
        strncpy(rhs, or + 4, sizeof(rhs) - 1);
        trim_inplace(rhs);
        return eval_simple_cond(lhs) || eval_simple_cond(rhs);
    }

    return eval_simple_cond(buf);
}

int parse_numargs(const char *expanded, double *v, int maxn) {
    char tmp[SCRIPT_LINE_LEN];
    strncpy(tmp, expanded, SCRIPT_LINE_LEN - 1);
    int n = 0;
    char *tok = strtok(tmp, ",");
    while (tok && n < maxn) {
        trim_inplace(tok);
        v[n++] = atof(tok);
        tok = strtok(NULL, ",");
    }
    return n;
}

static int find_op(const char *s, int start) {
    for (int i = start; s[i]; i++) {
        if (s[i] == '.' && i > 0 && s[i-1] == ' ' && s[i+1] == ' ')
            return i;
        if ((s[i] == '+' || s[i] == '-' || s[i] == '*' || s[i] == '/' || s[i] == '%') &&
            i > 0 && s[i-1] == ' ' && s[i+1] == ' ')
            return i;
    }
    return -1;
}

static double resolve_val(script_ctx_t *ctx, const char *s) {
    char *end;
    double v = strtod(s, &end);
    if (*end == '\0') return v;
    const char *var = var_get(ctx, s);
    return var[0] ? atof(var) : 0.0;
}

int eval_arith(script_ctx_t *ctx, const char *expr, double *num_out,
               char *str_out, int str_len) {
    char tmp[SCRIPT_LINE_LEN];
    strncpy(tmp, expr, SCRIPT_LINE_LEN - 1);
    tmp[SCRIPT_LINE_LEN - 1] = '\0';
    trim_inplace(tmp);

    /* Check for string concat first (single `.` with spaces around it) */
    int dot = find_op(tmp, 0);
    if (dot >= 0 && tmp[dot] == '.') {
        /* Find matching `.` — must be exactly one `.` operator */
        int second = find_op(tmp, dot + 1);
        if (second < 0) {
            char lhs_s[SCRIPT_LINE_LEN], rhs_s[SCRIPT_LINE_LEN];
            strncpy(lhs_s, tmp, (size_t)dot);
            lhs_s[dot] = '\0';
            trim_inplace(lhs_s);
            strncpy(rhs_s, tmp + dot + 1, SCRIPT_LINE_LEN - 1);
            trim_inplace(rhs_s);
            char lval[SCRIPT_LINE_LEN] = {0}, rval[SCRIPT_LINE_LEN] = {0};
            expand_vars(ctx, lhs_s, lval, sizeof(lval));
            expand_vars(ctx, rhs_s, rval, sizeof(rval));
            trim_inplace(lval);
            trim_inplace(rval);
            strncpy(str_out, lval, (size_t)(str_len - 1));
            strncat(str_out, rval, (size_t)(str_len - 1 - strlen(str_out)));
            return 2;
        }
    }

    /* Chained arithmetic: evaluate left-to-right */
    int op = find_op(tmp, 0);
    if (op < 0) {
        char *end;
        double v = strtod(tmp, &end);
        if (*end == '\0') { *num_out = v; return 1; }
        return 0;
    }

    char left[SCRIPT_LINE_LEN];
    strncpy(left, tmp, (size_t)op);
    left[op] = '\0';
    trim_inplace(left);
    double acc = resolve_val(ctx, left);

    while (op >= 0) {
        char op_ch = tmp[op];
        char right[SCRIPT_LINE_LEN];
        int next = find_op(tmp, op + 1);
        if (next >= 0) {
            strncpy(right, tmp + op + 1, (size_t)(next - op - 1));
            right[next - op - 1] = '\0';
        } else {
            strncpy(right, tmp + op + 1, SCRIPT_LINE_LEN - 1);
        }
        trim_inplace(right);
        double rv = resolve_val(ctx, right);

        switch (op_ch) {
            case '+': acc += rv; break;
            case '-': acc -= rv; break;
            case '*': acc *= rv; break;
            case '/': acc = rv ? acc / rv : 0; break;
            case '%': acc = rv ? (long)acc % (long)rv : 0; break;
        }
        op = next;
    }

    *num_out = acc;
    return 1;
}
