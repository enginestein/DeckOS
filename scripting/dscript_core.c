#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "dscript_internal.h"
#include "vfs.h"
#include "commands.h"
#include "syslog.h"
#include "module.h"
#include "esp.h"
#include "tusb.h"

void script_ctx_init(script_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

int script_run_string(script_ctx_t *ctx, const char *source) {
    char (*lines)[SCRIPT_LINE_LEN] = malloc((size_t)SCRIPT_MAX_LINES * sizeof(*lines));
    if (!lines) {
        printf("script: out of memory\n");
        return RC_ERROR;
    }

    int total = 0;
    const char *p = source;
    while (*p && total < SCRIPT_MAX_LINES) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len >= SCRIPT_LINE_LEN) len = SCRIPT_LINE_LEN - 1;
        strncpy(lines[total], p, (size_t)len);
        lines[total][len] = '\0';
        total++;
        if (!nl) break;
        p = nl + 1;
    }

    int rc = run_lines(ctx, lines, total, 0, total);
    free(lines);

    if (rc == RC_EXIT) return ctx->exit_code;
    return rc < 0 ? rc : 0;
}

int script_run_file(const char *vfs_path) {
    uint8_t *buf = (uint8_t *)malloc(VFS_MAX_FILE_SIZE);
    if (!buf) { printf("script: out of memory\n"); return -1; }

    uint32_t flen = 0;
    if (vfs_read(vfs_path, buf, VFS_MAX_FILE_SIZE - 1, &flen) < 0) {
        printf("script: file not found: %s\n", vfs_path);
        free(buf);
        return -1;
    }
    buf[flen] = '\0';
    printf("[script] running '%s'  (%lu bytes)\n", vfs_path, flen);

    script_ctx_t *ctx = (script_ctx_t *)malloc(sizeof(*ctx));
    if (!ctx) { printf("script: out of memory\n"); free(buf); return -1; }
    script_ctx_init(ctx);
    int rc = script_run_string(ctx, (const char *)buf);
    printf("[script] done (rc=%d)\n", rc);

    free(ctx);
    free(buf);
    return rc;
}

int run_lines(script_ctx_t *ctx, char lines[][SCRIPT_LINE_LEN], int total,
              int start, int end) {
    int i = start;
    while (i < end) {
        char raw[SCRIPT_LINE_LEN];
        strncpy(raw, lines[i], SCRIPT_LINE_LEN - 1);
        trim_inplace(raw);
        i++;

        if (!raw[0] || raw[0] == '#') continue;

        char line[SCRIPT_LINE_LEN];
        strncpy(line, raw, sizeof(line)-1);
        line[sizeof(line)-1] = '\0';
        trim_inplace(line);

        if (!strncmp(line, "def ", 4)) {
            int dummy_s, dummy_e;
            char name[SCRIPT_VAR_NAME_LEN] = {0};
            sscanf(line + 4, "%31s", name);
            int defline = find_def(lines, total, name, &dummy_s, &dummy_e);
            if (defline < 0) continue;
            i = dummy_e + 1;
            continue;
        }
        if (!strcmp(line, "enddef")) continue;

        if (!strncmp(line, "exit", 4)) {
            int code = 0;
            if (line[4] == ' ') code = atoi(line + 5);
            ctx->exit_code = code;
            return RC_EXIT;
        }

        if (!strncmp(line, "assert ", 7)) {
            char rest[SCRIPT_LINE_LEN];
            strncpy(rest, raw + 7, sizeof(rest) - 1);
            expand_vars(ctx, rest, rest, sizeof(rest));
            char *orfail = strstr(rest, " or fail:");
            char msg[SCRIPT_LINE_LEN] = "assertion failed";
            if (orfail) {
                strncpy(msg, orfail + 9, sizeof(msg) - 1);
                trim_inplace(msg);
                *orfail = '\0';
            }
            trim_inplace(rest);
            if (!eval_cond(ctx, rest)) {
                printf("script:%d: ASSERT FAILED: %s\n", i, msg);
                return RC_ERROR;
            }
            continue;
        }

        if (!strncmp(line, "log ", 4)) {
            char level[16] = {0}, tag[32] = {0}, msg[SCRIPT_LINE_LEN] = {0};
            sscanf(line + 4, "%15s %31s %[^\n]", level, tag, msg);
            if (!strcmp(level, "warn")) syslog_write(LOG_WARN, tag, msg);
            else if (!strcmp(level, "err")) syslog_write(LOG_ERR, tag, msg);
            else if (!strcmp(level, "debug")) syslog_write(LOG_DEBUG, tag, msg);
            else syslog_write(LOG_INFO, tag, msg);
            continue;
        }

        if (!strncmp(line, "include ", 8)) {
            char path[VFS_PATH_LEN] = {0};
            sscanf(line + 8, "%[^\n]", path);
            trim_inplace(path);
            int rc = do_include(ctx, path);
            if (rc == RC_EXIT) return rc;
            if (rc < 0 && rc != RC_ERROR) return rc;
            continue;
        }

        if (!strncmp(line, "break", 5)) {
            int d = 1;
            if (line[5] == ' ') d = atoi(line + 6);
            if (d < 1) d = 1; if (d > 8) d = 8;
            if (d == 1) return RC_BREAK;
            return -30 - d;
        }
        if (!strncmp(line, "continue", 8)) {
            int d = 1;
            if (line[8] == ' ') d = atoi(line + 9);
            if (d < 1) d = 1; if (d > 8) d = 8;
            if (d == 1) return RC_CONTINUE;
            return -40 - d;
        }

        if (!strncmp(line, "return", 6)) {
            if (line[6] == ' ') {
                char retbuf[SCRIPT_VAR_VAL_LEN];
                expand_vars(ctx, line + 7, retbuf, sizeof(retbuf));
                trim_inplace(retbuf);
                var_set(ctx, "return", retbuf);
            }
            return RC_RETURN;
        }

        if (!strncmp(line, "call ", 5)) {
            char rest2[SCRIPT_LINE_LEN];
            strncpy(rest2, line + 5, sizeof(rest2) - 1);
            trim_inplace(rest2);

            char fname[SCRIPT_VAR_NAME_LEN] = {0};
            sscanf(rest2, "%31s", fname);

            char *argp = rest2 + strlen(fname);
            while (*argp == ' ') argp++;
            char parts[8][SCRIPT_LINE_LEN];
            int nargs = 0;
            char argtmp[SCRIPT_LINE_LEN];
            strncpy(argtmp, argp, sizeof(argtmp) - 1);
            char *tok = strtok(argtmp, " ");
            while (tok && nargs < 8) {
                strncpy(parts[nargs++], tok, SCRIPT_LINE_LEN - 1);
                tok = strtok(NULL, " ");
            }

            for (int a = 0; a < nargs; a++) {
                char akey[16];
                snprintf(akey, sizeof(akey), "arg%d", a);
                char expanded_arg[SCRIPT_VAR_VAL_LEN];
                expand_vars(ctx, parts[a], expanded_arg, sizeof(expanded_arg));
                var_set(ctx, akey, expanded_arg);
            }
            var_set(ctx, "return", "");

            int bs, be;
            if (find_def(lines, total, fname, &bs, &be) < 0) {
                printf("script:%d: undefined function '%s'\n", i, fname);
                continue;
            }
            int rc = run_lines(ctx, lines, total, bs, be);
            if (rc == RC_RETURN) rc = RC_OK;
            if (rc == RC_EXIT) return rc;
            continue;
        }

        if (!strncmp(line, "let ", 4)) {
            char rest[SCRIPT_LINE_LEN];
            strncpy(rest, line + 4, sizeof(rest) - 1);
            trim_inplace(rest);

            char vname[SCRIPT_VAR_NAME_LEN] = {0};
            char eq[4] = {0};
            char valexpr[SCRIPT_LINE_LEN] = {0};

            char *eq_pos = strchr(rest, '=');
            if (!eq_pos) {
                printf("script:%d: bad let syntax (no '='): %s\n", i, rest);
                continue;
            }

            int namelen = (int)(eq_pos - rest);
            if (namelen <= 0 || namelen >= SCRIPT_VAR_NAME_LEN) {
                printf("script:%d: bad let syntax (name): %s\n", i, rest);
                continue;
            }
            strncpy(vname, rest, (size_t)namelen);
            vname[namelen] = '\0';
            trim_inplace(vname);

            strncpy(valexpr, eq_pos + 1, sizeof(valexpr) - 1);
            trim_inplace(valexpr);

            if (!vname[0]) {
                printf("script:%d: bad let syntax (empty name): %s\n", i, rest);
                continue;
            }

            if (try_builtin_val(ctx, vname, valexpr, lines, total))
                continue;

            char expanded[SCRIPT_LINE_LEN] = {0};
            expand_vars(ctx, valexpr, expanded, sizeof(expanded));
            trim_inplace(expanded);

            double arith_result;
            char arith_str[SCRIPT_LINE_LEN] = {0};
            int arith_type = eval_arith(ctx, expanded, &arith_result, arith_str, sizeof(arith_str));
            if (arith_type == 1) {
                char numbuf[32];
                if (arith_result == (long)arith_result)
                    snprintf(numbuf, sizeof(numbuf), "%ld", (long)arith_result);
                else
                    snprintf(numbuf, sizeof(numbuf), "%g", arith_result);
                var_set(ctx, vname, numbuf);
            } else if (arith_type == 2) {
                var_set(ctx, vname, arith_str);
            } else {
                var_set(ctx, vname, expanded);
            }
            continue;
        }

        if (!strncmp(line, "arr_new ", 8)) {
            char aname[SCRIPT_VAR_NAME_LEN] = {0};
            int sz = 0;
            sscanf(line + 8, "%31s %d", aname, &sz);
            arr_set_len(ctx, aname, 0);
            for (int ai = 0; ai < sz && ai < SCRIPT_MAX_VARS; ai++) {
                char key[SCRIPT_VAR_NAME_LEN];
                arr_key(key, sizeof(key), aname, ai);
                var_set(ctx, key, "0");
            }
            if (sz > 0) arr_set_len(ctx, aname, sz);
            continue;
        }

        if (!strncmp(line, "arr_set ", 8)) {
            char rest[SCRIPT_LINE_LEN];
            strncpy(rest, line + 8, sizeof(rest) - 1);
            char aname[SCRIPT_VAR_NAME_LEN] = {0}, aidx[32] = {0}, aval[SCRIPT_VAR_VAL_LEN] = {0};
            sscanf(rest, "%31s %31s %[^\n]", aname, aidx, aval);
            char eidx[32] = {0}, eval_val[SCRIPT_VAR_VAL_LEN] = {0};
            expand_vars(ctx, aidx, eidx, sizeof(eidx));
            expand_vars(ctx, aval, eval_val, sizeof(eval_val));
            int idx = atoi(eidx);
            char key[SCRIPT_VAR_NAME_LEN];
            arr_key(key, sizeof(key), aname, idx);
            var_set(ctx, key, eval_val);
            int curlen = arr_get_len(ctx, aname);
            if (idx >= curlen) arr_set_len(ctx, aname, idx + 1);
            continue;
        }

        if (!strncmp(line, "arr_push ", 9)) {
            char aname[SCRIPT_VAR_NAME_LEN] = {0}, aval[SCRIPT_VAR_VAL_LEN] = {0};
            sscanf(line + 9, "%31s %[^\n]", aname, aval);
            char eval_val[SCRIPT_VAR_VAL_LEN] = {0};
            expand_vars(ctx, aval, eval_val, sizeof(eval_val));
            int idx = arr_get_len(ctx, aname);
            char key[SCRIPT_VAR_NAME_LEN];
            arr_key(key, sizeof(key), aname, idx);
            var_set(ctx, key, eval_val);
            arr_set_len(ctx, aname, idx + 1);
            continue;
        }

        if (!strncmp(line, "arr_pop ", 8)) {
            char aname[SCRIPT_VAR_NAME_LEN] = {0}, dest[SCRIPT_VAR_NAME_LEN] = {0};
            sscanf(line + 8, "%31s %31s", aname, dest);
            int len = arr_get_len(ctx, aname);
            if (len <= 0) { if (dest[0]) var_set(ctx, dest, ""); continue; }
            char key[SCRIPT_VAR_NAME_LEN];
            arr_key(key, sizeof(key), aname, len - 1);
            if (dest[0]) var_set(ctx, dest, var_get(ctx, key));
            var_set(ctx, key, "");
            arr_set_len(ctx, aname, len - 1);
            continue;
        }

        if (!strncmp(line, "arr_dump ", 9)) {
            char aname[SCRIPT_VAR_NAME_LEN] = {0};
            sscanf(line + 9, "%31s", aname);
            int len = arr_get_len(ctx, aname);
            printf("array '%s'  len=%d:\n", aname, len);
            for (int ai = 0; ai < len; ai++) {
                char key[SCRIPT_VAR_NAME_LEN];
                arr_key(key, sizeof(key), aname, ai);
                printf("  [%d] = %s\n", ai, var_get(ctx, key));
            }
            continue;
        }

        if (!strncmp(raw, "foreach ", 8)) {
            char fvar[SCRIPT_VAR_NAME_LEN]={0}, keyword[8]={0}, source[SCRIPT_VAR_NAME_LEN]={0};
            sscanf(raw + 8, "%31s %7s %31s", fvar, keyword, source);
            int fi = find_end(lines, total, i, "foreach", "endfor");
            if (fi < 0) { printf("script:%d: missing endfor\n", i); return RC_ERROR; }
            if (!strcmp(keyword, "in")) {
                char aname[SCRIPT_VAR_NAME_LEN]={0}; expand_vars(ctx,source,aname,sizeof(aname)); trim_inplace(aname);
                int alen = arr_get_len(ctx, aname);
                for (int ai = 0; ai < alen; ai++) {
                    char key[SCRIPT_VAR_NAME_LEN]; arr_key(key,sizeof(key),aname,ai);
                    var_set(ctx, fvar, var_get(ctx, key));
                    int rc = run_lines(ctx, lines, total, i, fi);
                    if (rc == RC_BREAK) break;
                    if (rc == RC_CONTINUE) continue;
                    if (rc >= -38 && rc <= -32) { int d = -rc - 30; return (d > 2) ? rc + 1 : RC_BREAK; }
                    if (rc >= -48 && rc <= -42) { int d = -rc - 40; return (d > 2) ? rc + 1 : RC_CONTINUE; }
                    if (rc < 0) return rc;
                }
            }
            i = fi + 1;
            continue;
        }

        if (!strncmp(raw, "for ", 4)) {
            char fvar[SCRIPT_VAR_NAME_LEN] = {0}, keyword[8] = {0}, source[SCRIPT_VAR_NAME_LEN] = {0};
            sscanf(raw + 4, "%31s %7s %31s", fvar, keyword, source);

            int fi = find_end(lines, total, i, "for", "endfor");
            if (fi < 0) { printf("script:%d: missing endfor\n", i); return RC_ERROR; }

            if (!strcmp(keyword, "in")) {
                char aname[SCRIPT_VAR_NAME_LEN] = {0};
                expand_vars(ctx, source, aname, sizeof(aname));
                trim_inplace(aname);
                int alen = arr_get_len(ctx, aname);
                for (int ai = 0; ai < alen; ai++) {
                    char key[SCRIPT_VAR_NAME_LEN];
                    arr_key(key, sizeof(key), aname, ai);
                    var_set(ctx, fvar, var_get(ctx, key));
                    int rc = run_lines(ctx, lines, total, i, fi);
                    if (rc == RC_BREAK) break;
                    if (rc == RC_CONTINUE) continue;
                    if (rc >= -38 && rc <= -32) { int d = -rc - 30; return (d > 2) ? rc + 1 : RC_BREAK; }
                    if (rc >= -48 && rc <= -42) { int d = -rc - 40; return (d > 2) ? rc + 1 : RC_CONTINUE; }
                    if (rc < 0) return rc;
                }
            } else if (!strcmp(keyword, "from")) {
                char to_kw[8] = {0}, to_s[32] = {0}, step_kw[8] = {0}, step_s[32] = {0};
                sscanf(raw + 4, "%*s %*s %*s %7s %31s %7s %31s", to_kw, to_s, step_kw, step_s);
                int from_v = eval_int(ctx, source);
                int to_v   = eval_int(ctx, to_s);
                int step_v = (!strcmp(step_kw, "step")) ? eval_int(ctx, step_s) :
                    (from_v <= to_v ? 1 : -1);
                if (step_v == 0) step_v = 1;

                for (int cv = from_v; step_v > 0 ? cv <= to_v : cv >= to_v; cv += step_v) {
                    char nbuf[32];
                    snprintf(nbuf, sizeof(nbuf), "%d", cv);
                    var_set(ctx, fvar, nbuf);
                    int rc = run_lines(ctx, lines, total, i, fi);
                    if (rc == RC_BREAK) break;
                    if (rc == RC_CONTINUE) continue;
                    if (rc >= -38 && rc <= -32) { int d = -rc - 30; return (d > 2) ? rc + 1 : RC_BREAK; }
                    if (rc >= -48 && rc <= -42) { int d = -rc - 40; return (d > 2) ? rc + 1 : RC_CONTINUE; }
                    if (rc < 0) return rc;
                }
            }
            i = fi + 1;
            continue;
        }

        if (!strncmp(line, "print ", 6)) {
            char out[SCRIPT_LINE_LEN];
            expand_vars(ctx, line + 6, out, sizeof(out));
            printf("%s\n", out);
            continue;
        }
        if (!strcmp(line, "print")) { printf("\n"); continue; }

        if (!strncmp(line, "println ", 8)) {
            char out[SCRIPT_LINE_LEN];
            expand_vars(ctx, line + 8, out, sizeof(out));
            printf("%s\n", out);
            continue;
        }
        if (!strcmp(line, "println")) { printf("\n"); continue; }

        if (!strncmp(line, "sleep ", 6)) {
            int ms = eval_int(ctx, line + 6);
            if (ms > 0 && ms <= 60000) {
                uint32_t remaining = (uint32_t)ms;
                while (remaining > 0) {
                    uint32_t chunk = remaining < 10 ? remaining : 10;
                    sleep_ms(chunk);
                    tud_task();
                    remaining -= chunk;
                }
            }
            continue;
        }

        if (!strncmp(line, "if ", 3)) {
            bool taken = false;
            int cursor = i;
            bool cond = eval_cond(ctx, line + 3);

            while (true) {
                int ei_end  = find_elif_else_end(lines, total, cursor, "endif");
                int ei_elif = find_elif_else_end(lines, total, cursor, "elif");
                int ei_else = find_elif_else_end(lines, total, cursor, "else");

                int body_end = ei_end;
                if (ei_elif >= 0 && ei_elif < body_end) body_end = ei_elif;
                if (ei_else >= 0 && ei_else < body_end) body_end = ei_else;

                if (cond && !taken) {
                    int rc = run_lines(ctx, lines, total, cursor, body_end);
                    if (rc == RC_BREAK || rc == RC_CONTINUE ||
                        rc == RC_RETURN || rc == RC_EXIT || rc == RC_ERROR)
                        return rc;
                    taken = true;
                }

                if (body_end < 0 || body_end == ei_end) {
                    i = (ei_end >= 0) ? ei_end + 1 : end;
                    break;
                }

                char bbuf[SCRIPT_LINE_LEN];
                strncpy(bbuf, lines[body_end], SCRIPT_LINE_LEN - 1);
                trim_inplace(bbuf);
                cursor = body_end + 1;

                if (!strncmp(bbuf, "elif ", 5)) cond = !taken && eval_cond(ctx, bbuf + 5);
                else if (!strcmp(bbuf, "else")) cond = !taken;
            }
            continue;
        }

        if (!strncmp(raw, "switch ", 7)) {
            char subject[SCRIPT_VAR_VAL_LEN] = {0};
            expand_vars(ctx, raw + 7, subject, sizeof(subject));
            trim_inplace(subject);

            int sw_end = find_end(lines, total, i, "switch", "endswitch");
            if (sw_end < 0) { printf("script:%d: missing endswitch\n", i); return RC_ERROR; }

            bool matched = false;
            int ci = i;
            while (ci < sw_end) {
                char cbuf[SCRIPT_LINE_LEN];
                strncpy(cbuf, lines[ci], SCRIPT_LINE_LEN - 1);
                trim_inplace(cbuf);
                ci++;

                if (!strncmp(cbuf, "case ", 5)) {
                    char cval[SCRIPT_VAR_VAL_LEN] = {0};
                    expand_vars(ctx, cbuf + 5, cval, sizeof(cval));
                    trim_inplace(cval);
                    if (!matched && !strcmp(cval, subject)) {
                        int next = ci;
                        while (next < sw_end) {
                            char nb[SCRIPT_LINE_LEN];
                            strncpy(nb, lines[next], SCRIPT_LINE_LEN - 1);
                            trim_inplace(nb);
                            if (!strncmp(nb, "case ", 5) || !strcmp(nb, "default:")) break;
                            next++;
                        }
                        int rc = run_lines(ctx, lines, total, ci, next);
                        if (rc == RC_BREAK) { matched = true; break; }
                        if (rc < 0) { i = sw_end + 1; return rc; }
                        matched = true;
                        ci = next;
                    }
                } else if (!strcmp(cbuf, "default:") && !matched) {
                    int rc = run_lines(ctx, lines, total, ci, sw_end);
                    if (rc < 0 && rc != RC_BREAK) { i = sw_end + 1; return rc; }
                    matched = true;
                    break;
                }
            }
            i = sw_end + 1;
            continue;
        }

        if (!strncmp(line, "while ", 6)) {
            char cond_expr[SCRIPT_LINE_LEN];
            strncpy(cond_expr, raw + 6, sizeof(cond_expr) - 1);
            trim_inplace(cond_expr);

            int wi = find_end(lines, total, i, "while", "endwhile");
            if (wi < 0) { printf("script:%d: missing endwhile\n", i); return RC_ERROR; }

            int iter = 0;
            while (true) {
                char ce[SCRIPT_LINE_LEN];
                expand_vars(ctx, cond_expr, ce, sizeof(ce));
                if (!eval_cond(ctx, ce)) break;
                if (++iter > 100000) { printf("script:%d: while iteration limit reached\n", i); break; }
                int rc = run_lines(ctx, lines, total, i, wi);
                if (rc == RC_BREAK) break;
                if (rc == RC_CONTINUE) continue;
                if (rc >= -38 && rc <= -32) { int d = -rc - 30; return (d > 2) ? rc + 1 : RC_BREAK; }
                if (rc >= -48 && rc <= -42) { int d = -rc - 40; return (d > 2) ? rc + 1 : RC_CONTINUE; }
                if (rc < 0) return rc;
            }
            i = wi + 1;
            continue;
        }

        if (!strncmp(line, "repeat ", 7)) {
            int n = eval_int(ctx, line + 7);
            int ri = find_end(lines, total, i, "repeat", "endrepeat");
            if (ri < 0) { printf("script:%d: missing endrepeat\n", i); return RC_ERROR; }

            for (int r = 0; r < n && r < 10000; r++) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", r);
                var_set(ctx, "_i", buf);
                int rc = run_lines(ctx, lines, total, i, ri);
                if (rc == RC_BREAK) break;
                if (rc == RC_CONTINUE) continue;
                if (rc >= -38 && rc <= -32) { int d = -rc - 30; return (d > 2) ? rc + 1 : RC_BREAK; }
                if (rc >= -48 && rc <= -42) { int d = -rc - 40; return (d > 2) ? rc + 1 : RC_CONTINUE; }
                if (rc < 0) return rc;
            }
            i = ri + 1;
            continue;
        }

        if (!strncmp(line, "gpio_write ", 11)) {
            int pin, val;
            if (sscanf(line + 11, "%d %d", &pin, &val) == 2) {
                if (pin >= 0 && pin <= 28) {
                    gpio_init((uint)pin);
                    gpio_set_dir((uint)pin, GPIO_OUT);
                    gpio_put((uint)pin, val ? 1 : 0);
                }
            }
            continue;
        }

        if (!strncmp(line, "wait_pin ", 9)) {
            int pin, expected, tms = 5000;
            sscanf(line + 9, "%d %d %d", &pin, &expected, &tms);
            if (pin >= 0 && pin <= 28) {
                gpio_init((uint)pin);
                gpio_set_dir((uint)pin, GPIO_IN);
                uint32_t start_ms = to_ms_since_boot(get_absolute_time());
                while (gpio_get((uint)pin) != (expected ? 1 : 0)) {
                    if ((int)(to_ms_since_boot(get_absolute_time()) - start_ms) >= tms) {
                        var_set(ctx, "_timeout", "1");
                        break;
                    }
                    sleep_us(100);
                }
            }
            continue;
        }

        if (!strncmp(line, "pulse ", 6)) {
            int pin, us;
            if (sscanf(line + 6, "%d %d", &pin, &us) == 2 && pin >= 0 && pin <= 28) {
                gpio_init((uint)pin);
                gpio_set_dir((uint)pin, GPIO_OUT);
                gpio_put((uint)pin, 1);
                sleep_us((uint32_t)us);
                gpio_put((uint)pin, 0);
            }
            continue;
        }

        if (!strcmp(line, "echo")) { printf("\n"); continue; }

        if (!strcmp(line, "pause")) {
            printf("[script] press any key to continue...\n");
            while (getchar_timeout_us(100000) == PICO_ERROR_TIMEOUT) {}
            continue;
        }

        if (!strcmp(line, "vars")) {
            printf("script variables:\n");
            for (int v = 0; v < ctx->var_count; v++)
                printf("  %-15s = %s\n", ctx->vars[v].name, ctx->vars[v].value);
            continue;
        }

        if (!strcmp(line, "else") || !strncmp(line, "elif ", 5) ||
            !strcmp(line, "endif") || !strcmp(line, "endwhile") ||
            !strcmp(line, "endrepeat") || !strcmp(line, "endfor") ||
            !strcmp(line, "endswitch") || !strcmp(line, "default:"))
            continue;

        char exec_buf[SCRIPT_LINE_LEN];
        strncpy(exec_buf, line, sizeof(exec_buf) - 1);
        commands_execute(exec_buf);
    }
    return RC_OK;
}
