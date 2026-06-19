#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
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
#include "module.h"
#include "esp.h"

static void builtin_upper(script_ctx_t *ctx, const char *dest, const char *args) {
    char s[SCRIPT_VAR_VAL_LEN];
    expand_vars(ctx, args, s, sizeof(s));
    for (int i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
    var_set(ctx, dest, s);
}

static void builtin_lower(script_ctx_t *ctx, const char *dest, const char *args) {
    char s[SCRIPT_VAR_VAL_LEN];
    expand_vars(ctx, args, s, sizeof(s));
    for (int i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
    var_set(ctx, dest, s);
}

static void builtin_len(script_ctx_t *ctx, const char *dest, const char *args) {
    char s[SCRIPT_VAR_VAL_LEN];
    expand_vars(ctx, args, s, sizeof(s));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)strlen(s));
    var_set(ctx, dest, buf);
}

static void builtin_substr(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char sval[SCRIPT_VAR_VAL_LEN] = {0};
    char st_s[16] = {0}, ln_s[16] = {0};
    int n = sscanf(tmp, "%[^,],%15[^,],%15s", sval, st_s, ln_s);
    trim_inplace(sval);
    trim_inplace(st_s);
    trim_inplace(ln_s);
    int slen = (int)strlen(sval);
    int st = n >= 2 ? atoi(st_s) : 0;
    int ln = n >= 3 ? atoi(ln_s) : slen;
    if (st < 0) st = slen + st;
    if (st < 0) st = 0;
    if (st > slen) st = slen;
    if (ln < 0 || st + ln > slen) ln = slen - st;
    char out[SCRIPT_VAR_VAL_LEN] = {0};
    strncpy(out, sval + st, (size_t)ln);
    var_set(ctx, dest, out);
}

static void builtin_contains(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char hay[SCRIPT_VAR_VAL_LEN] = {0}, ndl[SCRIPT_VAR_VAL_LEN] = {0};
    sscanf(tmp, "%[^,],%[^\n]", hay, ndl);
    trim_inplace(hay);
    trim_inplace(ndl);
    var_set(ctx, dest, strstr(hay, ndl) ? "1" : "0");
}

static void builtin_trim_fn(script_ctx_t *ctx, const char *dest, const char *args) {
    char s[SCRIPT_VAR_VAL_LEN];
    expand_vars(ctx, args, s, sizeof(s));
    trim_inplace(s);
    var_set(ctx, dest, s);
}

static void builtin_replace(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char src[SCRIPT_VAR_VAL_LEN] = {0};
    char old[SCRIPT_VAR_VAL_LEN] = {0};
    char repl[SCRIPT_VAR_VAL_LEN] = {0};
    sscanf(tmp, "%[^,],%[^,],%[^\n]", src, old, repl);
    trim_inplace(src);
    trim_inplace(old);
    trim_inplace(repl);
    if (!old[0]) { var_set(ctx, dest, src); return; }
    char out[SCRIPT_VAR_VAL_LEN] = {0};
    int oi = 0, slen = (int)strlen(src), olen = (int)strlen(old);
    int rlen = (int)strlen(repl);
    for (int i = 0; i < slen;) {
        if (strncmp(src + i, old, (size_t)olen) == 0) {
            if (oi + rlen < SCRIPT_VAR_VAL_LEN - 1) {
                memcpy(out + oi, repl, (size_t)rlen);
                oi += rlen;
            }
            i += olen;
        } else {
            if (oi < SCRIPT_VAR_VAL_LEN - 1) out[oi++] = src[i];
            i++;
        }
    }
    out[oi] = '\0';
    var_set(ctx, dest, out);
}

void builtin_math(script_ctx_t *ctx, const char *dest, const char *fname, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    double v[5] = {0};
    int n = parse_numargs(tmp, v, 5);
    double result = 0.0;
    bool is_int = true;

    if (!strcmp(fname, "sqrt")) { result = sqrt(v[0]); is_int = false; }
    else if (!strcmp(fname, "pow")) { result = pow(v[0], v[1]); is_int = false; }
    else if (!strcmp(fname, "abs")) { result = fabs(v[0]); }
    else if (!strcmp(fname, "min")) { result = (n >= 2 && v[1] < v[0]) ? v[1] : v[0]; }
    else if (!strcmp(fname, "max")) { result = (n >= 2 && v[1] > v[0]) ? v[1] : v[0]; }
    else if (!strcmp(fname, "clamp")) { result = v[0] < v[1] ? v[1] : (v[0] > v[2] ? v[2] : v[0]); }
    else if (!strcmp(fname, "map")) {
        double range = v[2] - v[1];
        result = range == 0.0 ? v[3] : v[3] + (v[0] - v[1]) / range * (v[4] - v[3]);
        is_int = false;
    } else if (!strcmp(fname, "rand")) {
        int lo = (int)v[0], hi = n >= 2 ? (int)v[1] : (int)v[0];
        if (hi < lo) { int t = lo; lo = hi; hi = t; }
        result = (hi == lo) ? lo : (lo + rand() % (hi - lo + 1));
    } else if (!strcmp(fname, "avg")) {
        double sum = 0.0;
        for (int i = 0; i < n; i++) sum += v[i];
        result = n ? sum / n : 0.0;
        is_int = false;
    } else {
        printf("script: unknown math function '%s'\n", fname);
        var_set(ctx, dest, "0");
        return;
    }

    char buf[32];
    if (is_int) snprintf(buf, sizeof(buf), "%ld", (long)result);
    else snprintf(buf, sizeof(buf), "%.4g", result);
    var_set(ctx, dest, buf);
}

static void builtin_adc(script_ctx_t *ctx, const char *dest, const char *args) {
    int ch = eval_int(ctx, args);
    if (ch < 0 || ch > 2) { var_set(ctx, dest, "0"); return; }
    adc_select_input((uint)ch);
    uint16_t raw = adc_read();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", raw);
    var_set(ctx, dest, buf);
}

static void builtin_gpio_read(script_ctx_t *ctx, const char *dest, const char *args) {
    int pin = eval_int(ctx, args);
    if (pin < 0 || pin > 28) { var_set(ctx, dest, "0"); return; }
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_IN);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gpio_get((uint)pin));
    var_set(ctx, dest, buf);
}

static void builtin_pwm(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    double v[2] = {0};
    parse_numargs(tmp, v, 2);
    int pin = (int)v[0];
    int duty = (int)v[1];
    if (pin < 0 || pin > 28 || duty < 0 || duty > 100) {
        printf("script: pwm: invalid pin/duty\n");
        var_set(ctx, dest, "0");
        return;
    }
    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num((uint)pin);
    pwm_set_wrap(slice, 999);
    pwm_set_gpio_level((uint)pin, (uint16_t)(duty * 10));
    pwm_set_enabled(slice, true);
    var_set(ctx, dest, "1");
}

static void builtin_input(script_ctx_t *ctx, const char *dest, const char *prompt) {
    char pr[SCRIPT_LINE_LEN];
    expand_vars(ctx, prompt, pr, sizeof(pr));
    trim_inplace(pr);
    printf("%s", pr);
    char buf[SCRIPT_VAR_VAL_LEN] = {0};
    int pos = 0;
    while (pos < SCRIPT_VAR_VAL_LEN - 1) {
        int c = getchar_timeout_us(30000000UL);
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == '\r' || c == '\n') { putchar('\n'); break; }
        if (c == 127 || c == '\b') {
            if (pos > 0) { pos--; printf("\b \b"); }
            continue;
        }
        buf[pos++] = (char)c;
        putchar(c);
    }
    buf[pos] = '\0';
    var_set(ctx, dest, buf);
}

static void builtin_format(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN * 2];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char *comma = strchr(tmp, ',');
    char fmt[SCRIPT_LINE_LEN] = {0};
    char rest[SCRIPT_LINE_LEN] = {0};
    if (comma) {
        strncpy(fmt, tmp, (size_t)(comma - tmp));
        strncpy(rest, comma + 1, sizeof(rest) - 1);
    } else {
        strncpy(fmt, tmp, sizeof(fmt) - 1);
    }
    trim_inplace(fmt);
    trim_inplace(rest);
    double argv[8] = {0};
    char sargv[8][SCRIPT_VAR_VAL_LEN];
    int argc = 0;
    char r2[SCRIPT_LINE_LEN];
    strncpy(r2, rest, sizeof(r2) - 1);
    char *tok = strtok(r2, ",");
    while (tok && argc < 8) {
        trim_inplace(tok);
        strncpy(sargv[argc], tok, SCRIPT_VAR_VAL_LEN - 1);
        argv[argc] = atof(tok);
        argc++;
        tok = strtok(NULL, ",");
    }
    char out[SCRIPT_VAR_VAL_LEN] = {0};
    int oi = 0, ai = 0;
    const char *f = fmt;
    while (*f && oi < SCRIPT_VAR_VAL_LEN - 1) {
        if (*f != '%') { out[oi++] = *f++; continue; }
        f++;
        char spec[16] = {'%'};
        int si = 1;
        while (*f && si < 14 && !isalpha((unsigned char)*f)) spec[si++] = *f++;
        if (*f) spec[si++] = *f++;
        spec[si] = '\0';
        char piece[SCRIPT_VAR_VAL_LEN] = {0};
        char last = spec[si - 1];
        if (last == 'd' || last == 'i' || last == 'u' || last == 'x' || last == 'o')
            snprintf(piece, sizeof(piece), spec, (long)(ai < argc ? argv[ai] : 0));
        else if (last == 'f' || last == 'e' || last == 'g')
            snprintf(piece, sizeof(piece), spec, (ai < argc ? argv[ai] : 0.0));
        else if (last == 's')
            snprintf(piece, sizeof(piece), spec, (ai < argc ? sargv[ai] : ""));
        else
            snprintf(piece, sizeof(piece), "%s", spec);
        ai++;
        int pl = (int)strlen(piece);
        if (oi + pl < SCRIPT_VAR_VAL_LEN - 1) { memcpy(out + oi, piece, (size_t)pl); oi += pl; }
    }
    out[oi] = '\0';
    var_set(ctx, dest, out);
}

static void builtin_usleep(script_ctx_t *ctx, const char *dest, const char *args) {
    int us = eval_int(ctx, args);
    if (us > 0 && us <= 1000000) sleep_us((uint32_t)us);
    var_set(ctx, dest, "1");
}

static void builtin_temp_c(script_ctx_t *ctx, const char *dest, const char *args) {
    (void)args;
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    uint16_t raw = adc_read();
    float volt = raw * 3.3f / 4096.0f;
    float temp = 27.0f - (volt - 0.706f) / 0.001721f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", temp);
    var_set(ctx, dest, buf);
}

static void builtin_file_read(script_ctx_t *ctx, const char *dest, const char *args) {
    char path[VFS_PATH_LEN];
    expand_vars(ctx, args, path, sizeof(path));
    trim_inplace(path);
    uint8_t *buf = (uint8_t *)malloc(VFS_MAX_FILE_SIZE);
    if (!buf) { var_set(ctx, dest, ""); return; }
    uint32_t flen = 0;
    if (vfs_read(path, buf, VFS_MAX_FILE_SIZE - 1, &flen) < 0) { free(buf); var_set(ctx, dest, ""); return; }
    buf[flen] = '\0';
    char out[SCRIPT_VAR_VAL_LEN];
    snprintf(out, sizeof(out), "%s", (const char *)buf);
    free(buf);
    var_set(ctx, dest, out);
}

static void builtin_file_write(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char path[VFS_PATH_LEN] = {0};
    char content[SCRIPT_VAR_VAL_LEN] = {0};
    sscanf(tmp, "%[^,],%[^\n]", path, content);
    trim_inplace(path);
    trim_inplace(content);
    int ret = vfs_write(path, (const uint8_t *)content, (uint32_t)strlen(content), false);
    var_set(ctx, dest, ret > 0 ? "1" : "0");
}

static void builtin_i2c_read(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    for (char *p = tmp; *p; p++) if (*p == ',') *p = ' ';
    int addr = 0, reg = 0;
    sscanf(tmp, "%i %i", &addr, &reg);
    if (addr < 1 || addr > 127) { var_set(ctx, dest, "0"); return; }
    uint8_t val = 0;
    uint8_t reg_b = (uint8_t)reg;
    i2c_write_blocking(i2c_default, (uint8_t)addr, &reg_b, 1, false);
    i2c_read_blocking(i2c_default, (uint8_t)addr, &val, 1, false);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val);
    var_set(ctx, dest, buf);
}

static void builtin_i2c_write(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    for (char *p = tmp; *p; p++) if (*p == ',') *p = ' ';
    int addr = 0, reg = 0, val = 0;
    sscanf(tmp, "%i %i %i", &addr, &reg, &val);
    if (addr < 1 || addr > 127 || val < 0 || val > 255) { var_set(ctx, dest, "0"); return; }
    uint8_t reg_b = (uint8_t)reg;
    uint8_t val_b = (uint8_t)val;
    uint8_t buf[2] = {reg_b, val_b};
    i2c_write_blocking(i2c_default, (uint8_t)addr, buf, 2, false);
    var_set(ctx, dest, "1");
}

static void builtin_i2c_scan(script_ctx_t *ctx, const char *dest, const char *args) {
    (void)args;
    char found[64] = {0};
    int n = 0;
    for (int addr = 1; addr < 127; addr++) {
        uint8_t tx = 0;
        int ret = i2c_write_blocking(i2c_default, (uint8_t)addr, &tx, 1, true);
        if (ret >= 0) {
            if (n > 0) strncat(found, ",", sizeof(found) - 1 - strlen(found));
            char a[8];
            snprintf(a, sizeof(a), "0x%02X", addr);
            strncat(found, a, sizeof(found) - 1 - strlen(found));
            n++;
        }
    }
    var_set(ctx, dest, n > 0 ? found : "(none)");
}

static void builtin_wifi_status(script_ctx_t *ctx, const char *dest, const char *args) {
    (void)args;
    if (module_is_loaded("wifi")) var_set(ctx, dest, "initialized");
    else var_set(ctx, dest, "not_init");
}

static void builtin_http_get(script_ctx_t *ctx, const char *dest, const char *args) {
    (void)args;
    printf("script: http_get requires the wifi module (module load wifi)\n");
    var_set(ctx, dest, "");
}

static void builtin_http_post(script_ctx_t *ctx, const char *dest, const char *args) {
    (void)args;
    printf("script: http_post requires the wifi module (module load wifi)\n");
    var_set(ctx, dest, "");
}

bool try_builtin_val(script_ctx_t *ctx, const char *vname, const char *valexpr,
                     char lines[][SCRIPT_LINE_LEN], int total) {
    (void)lines;
    (void)total;

    if (!strncmp(valexpr, "adc(", 4)) {
        char arg[32] = {0}; sscanf(valexpr + 4, "%31[^)]", arg);
        builtin_adc(ctx, vname, arg); return true;
    }
    if (!strncmp(valexpr, "gpio(", 5)) {
        char arg[32] = {0}; sscanf(valexpr + 5, "%31[^)]", arg);
        builtin_gpio_read(ctx, vname, arg); return true;
    }
    if (!strncmp(valexpr, "pwm(", 4)) {
        char arg[SCRIPT_LINE_LEN] = {0}; sscanf(valexpr + 4, "%[^)]", arg);
        builtin_pwm(ctx, vname, arg); return true;
    }
    if (!strncmp(valexpr, "millis", 6)) {
        char buf[16]; snprintf(buf, sizeof(buf), "%lu", to_ms_since_boot(get_absolute_time()));
        var_set(ctx, vname, buf); return true;
    }
    if (!strncmp(valexpr, "micros", 6)) {
        char buf[24]; snprintf(buf, sizeof(buf), "%llu", (unsigned long long)time_us_64());
        var_set(ctx, vname, buf); return true;
    }
    if (!strncmp(valexpr, "temp_c", 6) && valexpr[6] == '(') {
        builtin_temp_c(ctx, vname, ""); return true;
    }
    if (!strncmp(valexpr, "usleep(", 7)) { char arg[32]={0}; sscanf(valexpr+7,"%31[^)]",arg); builtin_usleep(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "file_read(", 10)) { char arg[VFS_PATH_LEN]={0}; sscanf(valexpr+10,"%[^)]",arg); builtin_file_read(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "file_write(", 11)) { char arg[SCRIPT_LINE_LEN]={0}; sscanf(valexpr+11,"%[^)]",arg); builtin_file_write(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "i2c_read(", 9)) { char arg[SCRIPT_LINE_LEN]={0}; sscanf(valexpr+9,"%[^)]",arg); builtin_i2c_read(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "i2c_write(", 10)) { char arg[SCRIPT_LINE_LEN]={0}; sscanf(valexpr+10,"%[^)]",arg); builtin_i2c_write(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "i2c_scan", 8)) { builtin_i2c_scan(ctx, vname, ""); return true; }
    if (!strncmp(valexpr, "wifi_status", 11)) { builtin_wifi_status(ctx, vname, ""); return true; }
    if (!strncmp(valexpr, "http_get(", 9)) { char arg[SCRIPT_VAR_VAL_LEN]={0}; sscanf(valexpr+9,"%[^)]",arg); builtin_http_get(ctx, vname, arg); return true; }
    if (!strncmp(valexpr, "http_post(", 10)) { char arg[SCRIPT_LINE_LEN]={0}; sscanf(valexpr+10,"%[^)]",arg); builtin_http_post(ctx, vname, arg); return true; }

    struct {
        const char *fn;
        void (*handler)(script_ctx_t *, const char *, const char *);
    } sfns[] = {
        {"upper(", builtin_upper},
        {"lower(", builtin_lower},
        {"len(", builtin_len},
        {"substr(", builtin_substr},
        {"contains(", builtin_contains},
        {"trim(", builtin_trim_fn},
        {"replace(", builtin_replace},
    };
    for (int si = 0; si < (int)(sizeof(sfns) / sizeof(sfns[0])); si++) {
        if (!strncmp(valexpr, sfns[si].fn, strlen(sfns[si].fn))) {
            char arg[SCRIPT_LINE_LEN] = {0};
            sscanf(valexpr + strlen(sfns[si].fn), "%[^)]", arg);
            sfns[si].handler(ctx, vname, arg);
            return true;
        }
    }

    const char *mathfns[] = {
        "sqrt(", "pow(", "abs(", "min(", "max(", "clamp(", "map(", "rand(", "avg("
    };
    for (int mi = 0; mi < (int)(sizeof(mathfns) / sizeof(mathfns[0])); mi++) {
        if (!strncmp(valexpr, mathfns[mi], strlen(mathfns[mi]))) {
            char arg[SCRIPT_LINE_LEN] = {0};
            char fn[16] = {0};
            strncpy(fn, mathfns[mi], strlen(mathfns[mi]) - 1);
            sscanf(valexpr + strlen(mathfns[mi]), "%[^)]", arg);
            builtin_math(ctx, vname, fn, arg);
            return true;
        }
    }

    if (!strncmp(valexpr, "input(", 6)) {
        char arg[SCRIPT_LINE_LEN] = {0}; sscanf(valexpr + 6, "%[^)]", arg);
        builtin_input(ctx, vname, arg); return true;
    }

    if (!strncmp(valexpr, "format(", 7)) {
        char arg[SCRIPT_LINE_LEN] = {0};
        const char *p = valexpr + 7;
        int plen = (int)strlen(p);
        if (plen > 0 && p[plen - 1] == ')') plen--;
        strncpy(arg, p, (size_t)plen);
        builtin_format(ctx, vname, arg); return true;
    }

    if (!strncmp(valexpr, "arr_get(", 8)) {
        char arg[SCRIPT_LINE_LEN] = {0};
        sscanf(valexpr + 8, "%[^)]", arg);
        char aname[SCRIPT_VAR_NAME_LEN] = {0}, aidx[16] = {0};
        sscanf(arg, "%[^,],%15s", aname, aidx);
        trim_inplace(aname); trim_inplace(aidx);
        char expanded_idx[32] = {0};
        expand_vars(ctx, aidx, expanded_idx, sizeof(expanded_idx));
        int idx = atoi(expanded_idx);
        char key[SCRIPT_VAR_NAME_LEN];
        arr_key(key, sizeof(key), aname, idx);
        var_set(ctx, vname, var_get(ctx, key));
        return true;
    }

    if (!strncmp(valexpr, "arr_len(", 8)) {
        char aname[SCRIPT_VAR_NAME_LEN] = {0};
        sscanf(valexpr + 8, "%[^)]", aname);
        trim_inplace(aname);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", arr_get_len(ctx, aname));
        var_set(ctx, vname, buf);
        return true;
    }

    return false;
}
