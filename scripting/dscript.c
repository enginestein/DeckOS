#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include <ctype.h>

#include <math.h>

#include "pico/stdlib.h"

#include "dscript.h"

#include "vfs.h"

#include "commands.h"

#include "syslog.h"

#include "hardware/adc.h"

#include "hardware/gpio.h"

#include "hardware/pwm.h"

#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "module.h"
#include "esp.h"


#define RC_OK 0
#define RC_BREAK - 10
#define RC_CONTINUE - 11
#define RC_RETURN - 12
#define RC_EXIT - 20
#define RC_ERROR - 1

static int run_lines(script_ctx_t * ctx,
  char lines[][SCRIPT_LINE_LEN], int total,
  int start, int end);

static void trim_inplace(char * s) {
  int len = (int) strlen(s);
  while (len > 0 && isspace((unsigned char) s[len - 1])) s[--len] = '\0';
  int st = 0;
  while (s[st] && isspace((unsigned char) s[st])) st++;
  if (st) memmove(s, s + st, (size_t)(len - st + 1));
}

void script_ctx_init(script_ctx_t * ctx) {
  memset(ctx, 0, sizeof( * ctx));
}

static const char * var_get(script_ctx_t * ctx,
  const char * name) {
  for (int i = 0; i < ctx -> var_count; i++)
    if (strcmp(ctx -> vars[i].name, name) == 0)
      return ctx -> vars[i].value;
  return "";
}

static void var_set(script_ctx_t * ctx,
  const char * name,
    const char * val) {
  for (int i = 0; i < ctx -> var_count; i++) {
    if (strcmp(ctx -> vars[i].name, name) == 0) {
      strncpy(ctx -> vars[i].value, val, SCRIPT_VAR_VAL_LEN - 1);
      ctx -> vars[i].value[SCRIPT_VAR_VAL_LEN - 1] = '\0';
      return;
    }
  }
  if (ctx -> var_count >= SCRIPT_MAX_VARS) {
    printf("script: too many variables\n");
    return;
  }
  strncpy(ctx -> vars[ctx -> var_count].name, name, SCRIPT_VAR_NAME_LEN - 1);
  strncpy(ctx -> vars[ctx -> var_count].value, val, SCRIPT_VAR_VAL_LEN - 1);
  ctx -> var_count++;
}

static void expand_vars(script_ctx_t * ctx,
  const char * in, char * out, int outlen) {
  int i = 0, o = 0;
  while (in [i] && o < outlen - 1) {
    if (in [i] == '$') {
      i++;
      char vname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      int vn = 0;
      while (in [i] && (isalnum((unsigned char) in [i]) || in [i] == '_') &&
        vn < SCRIPT_VAR_NAME_LEN - 1)
        vname[vn++] = in [i++];
      const char * val = var_get(ctx, vname);
      while ( * val && o < outlen - 1) out[o++] = * val++;
    } else {
      out[o++] = in [i++];
    }
  }
  out[o] = '\0';
}

static int eval_int(script_ctx_t * ctx,
  const char * expr) {
  char buf[64];
  expand_vars(ctx, expr, buf, sizeof(buf));
  trim_inplace(buf);
  return atoi(buf);
}

static bool eval_cond(script_ctx_t * ctx,
  const char * expr) {
  char buf[128];
  expand_vars(ctx, expr, buf, sizeof(buf));
  trim_inplace(buf);

 
  const char * ops[] = {
    "==",
    "!=",
    "<=",
    ">=",
    "<",
    ">"
  };
  for (int oi = 0; oi < 6; oi++) {
    char * p = strstr(buf, ops[oi]);
    if (!p) continue;
    * p = '\0';
    char lhs[64], rhs[64];
    strncpy(lhs, buf, sizeof(lhs) - 1);
    lhs[sizeof(lhs)-1] = '\0';
    trim_inplace(lhs);
    strncpy(rhs, p + strlen(ops[oi]), sizeof(rhs) - 1);
    rhs[sizeof(rhs)-1] = '\0';
    trim_inplace(rhs);

    char * le, * re;
    long lv = strtol(lhs, & le, 10);
    long rv = strtol(rhs, & re, 10);
    bool numeric = ( * le == '\0' && * re == '\0');

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
 
  if (buf[0] == '\0') return false;
  char *end;
  long v = strtol(buf, &end, 10);
  if (*end == '\0') return v != 0;       
  return true;                           
}

static void builtin_upper(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char s[SCRIPT_VAR_VAL_LEN];
  expand_vars(ctx, args, s, sizeof(s));
  for (int i = 0; s[i]; i++) s[i] = (char) toupper((unsigned char) s[i]);
  var_set(ctx, dest, s);
}

static void builtin_lower(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char s[SCRIPT_VAR_VAL_LEN];
  expand_vars(ctx, args, s, sizeof(s));
  for (int i = 0; s[i]; i++) s[i] = (char) tolower((unsigned char) s[i]);
  var_set(ctx, dest, s);
}

static void builtin_len(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char s[SCRIPT_VAR_VAL_LEN];
  expand_vars(ctx, args, s, sizeof(s));
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", (int) strlen(s));
  var_set(ctx, dest, buf);
}

static void builtin_substr(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char tmp[SCRIPT_LINE_LEN];
  expand_vars(ctx, args, tmp, sizeof(tmp));

  char sval[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  char st_s[16] = {
    0
  }, ln_s[16] = {
    0
  };
  int n = sscanf(tmp, "%[^,],%15[^,],%15s", sval, st_s, ln_s);
  trim_inplace(sval);
  trim_inplace(st_s);
  trim_inplace(ln_s);

  int slen = (int) strlen(sval);
  int st = n >= 2 ? atoi(st_s) : 0;
  int ln = n >= 3 ? atoi(ln_s) : slen;

  if (st < 0) st = slen + st;
  if (st < 0) st = 0;
  if (st > slen) st = slen;
  if (ln < 0 || st + ln > slen) ln = slen - st;

  char out[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  strncpy(out, sval + st, (size_t) ln);
  var_set(ctx, dest, out);
}

static void builtin_contains(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char tmp[SCRIPT_LINE_LEN];
  expand_vars(ctx, args, tmp, sizeof(tmp));
  char hay[SCRIPT_VAR_VAL_LEN] = {
    0
  }, ndl[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  sscanf(tmp, "%[^,],%[^\n]", hay, ndl);
  trim_inplace(hay);
  trim_inplace(ndl);
  var_set(ctx, dest, strstr(hay, ndl) ? "1" : "0");
}

static void builtin_trim_fn(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char s[SCRIPT_VAR_VAL_LEN];
  expand_vars(ctx, args, s, sizeof(s));
  trim_inplace(s);
  var_set(ctx, dest, s);
}

static void builtin_replace(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char tmp[SCRIPT_LINE_LEN];
  expand_vars(ctx, args, tmp, sizeof(tmp));

  char src[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  char old[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  char repl[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  sscanf(tmp, "%[^,],%[^,],%[^\n]", src, old, repl);
  trim_inplace(src);
  trim_inplace(old);
  trim_inplace(repl);

  if (!old[0]) {
    var_set(ctx, dest, src);
    return;
  }

  char out[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  int oi = 0, slen = (int) strlen(src), olen = (int) strlen(old);
  int rlen = (int) strlen(repl);

  for (int i = 0; i < slen;) {
    if (strncmp(src + i, old, (size_t) olen) == 0) {
      if (oi + rlen < SCRIPT_VAR_VAL_LEN - 1) {
        memcpy(out + oi, repl, (size_t) rlen);
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

static int parse_numargs(const char * expanded, double * v, int maxn) {
  char tmp[SCRIPT_LINE_LEN];
  strncpy(tmp, expanded, SCRIPT_LINE_LEN - 1);
  int n = 0;
  char * tok = strtok(tmp, ",");
  while (tok && n < maxn) {
    trim_inplace(tok);
    v[n++] = atof(tok);
    tok = strtok(NULL, ",");
  }
  return n;
}

static void builtin_math(script_ctx_t * ctx,
  const char * dest,
    const char * fname,
      const char * args) {
  char tmp[SCRIPT_LINE_LEN];
  expand_vars(ctx, args, tmp, sizeof(tmp));
  double v[5] = {
    0
  };
  int n = parse_numargs(tmp, v, 5);

  double result = 0.0;
  bool is_int = true;

  if (!strcmp(fname, "sqrt")) {
    result = sqrt(v[0]);
    is_int = false;
  } else if (!strcmp(fname, "pow")) {
    result = pow(v[0], v[1]);
    is_int = false;
  } else if (!strcmp(fname, "abs")) {
    result = fabs(v[0]);
  } else if (!strcmp(fname, "min")) {
    result = (n >= 2 && v[1] < v[0]) ? v[1] : v[0];
  } else if (!strcmp(fname, "max")) {
    result = (n >= 2 && v[1] > v[0]) ? v[1] : v[0];
  } else if (!strcmp(fname, "clamp")) {
    result = v[0] < v[1] ? v[1] : (v[0] > v[2] ? v[2] : v[0]);
  } else if (!strcmp(fname, "map")) {
    double range = v[2] - v[1];
    result = range == 0.0 ? v[3] :
      v[3] + (v[0] - v[1]) / range * (v[4] - v[3]);
    is_int = false;
  } else if (!strcmp(fname, "rand")) {
    int lo = (int) v[0], hi = n >= 2 ? (int) v[1] : (int) v[0];
    if (hi < lo) {
      int t = lo;
      lo = hi;
      hi = t;
    }
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
  if (is_int)
    snprintf(buf, sizeof(buf), "%ld", (long) result);
  else
    snprintf(buf, sizeof(buf), "%.4g", result);
  var_set(ctx, dest, buf);
}

static void builtin_adc(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  int ch = eval_int(ctx, args);
  if (ch < 0 || ch > 2) {
    var_set(ctx, dest, "0");
    return;
  }
  adc_select_input((uint) ch);
  uint16_t raw = adc_read();
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", raw);
  var_set(ctx, dest, buf);
}

static void builtin_gpio_read(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  int pin = eval_int(ctx, args);
  if (pin < 0 || pin > 28) {
    var_set(ctx, dest, "0");
    return;
  }
  gpio_init((uint) pin);
  gpio_set_dir((uint) pin, GPIO_IN);
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", gpio_get((uint) pin));
  var_set(ctx, dest, buf);
}

static void builtin_pwm(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char tmp[SCRIPT_LINE_LEN];
  expand_vars(ctx, args, tmp, sizeof(tmp));
  double v[2] = {
    0
  };
  parse_numargs(tmp, v, 2);
  int pin = (int) v[0];
  int duty = (int) v[1];
  if (pin < 0 || pin > 28 || duty < 0 || duty > 100) {
    printf("script: pwm: invalid pin/duty\n");
    var_set(ctx, dest, "0");
    return;
  }
  gpio_set_function((uint) pin, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num((uint) pin);
  pwm_set_wrap(slice, 999);
  pwm_set_gpio_level((uint) pin, (uint16_t)(duty * 10));
  pwm_set_enabled(slice, true);
  var_set(ctx, dest, "1");
}

static void builtin_input(script_ctx_t * ctx,
  const char * dest,
    const char * prompt) {
  char pr[SCRIPT_LINE_LEN];
  expand_vars(ctx, prompt, pr, sizeof(pr));
  trim_inplace(pr);
  printf("%s", pr);

  char buf[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  int pos = 0;
  while (pos < SCRIPT_VAR_VAL_LEN - 1) {
    int c = getchar_timeout_us(30000000UL);
    if (c == PICO_ERROR_TIMEOUT) break;
    if (c == '\r' || c == '\n') {
      putchar('\n');
      break;
    }
    if (c == 127 || c == '\b') {
      if (pos > 0) {
        pos--;
        printf("\b \b");
      }
      continue;
    }
    buf[pos++] = (char) c;
    putchar(c);
  }
  buf[pos] = '\0';
  var_set(ctx, dest, buf);
}

static void builtin_format(script_ctx_t * ctx,
  const char * dest,
    const char * args) {
  char tmp[SCRIPT_LINE_LEN * 2];
  expand_vars(ctx, args, tmp, sizeof(tmp));

  char * comma = strchr(tmp, ',');
  char fmt[SCRIPT_LINE_LEN] = {
    0
  };
  char rest[SCRIPT_LINE_LEN] = {
    0
  };

  if (comma) {
    strncpy(fmt, tmp, (size_t)(comma - tmp));
    strncpy(rest, comma + 1, sizeof(rest) - 1);
  } else {
    strncpy(fmt, tmp, sizeof(fmt) - 1);
  }
  trim_inplace(fmt);
  trim_inplace(rest);

  double argv[8] = {
    0
  };
  char sargv[8][SCRIPT_VAR_VAL_LEN];
  int argc = 0;
  char r2[SCRIPT_LINE_LEN];
  strncpy(r2, rest, sizeof(r2) - 1);
  char * tok = strtok(r2, ",");
  while (tok && argc < 8) {
    trim_inplace(tok);
    strncpy(sargv[argc], tok, SCRIPT_VAR_VAL_LEN - 1);
    argv[argc] = atof(tok);
    argc++;
    tok = strtok(NULL, ",");
  }

  char out[SCRIPT_VAR_VAL_LEN] = {
    0
  };
  int oi = 0, ai = 0;
  const char * f = fmt;
  while ( * f && oi < SCRIPT_VAR_VAL_LEN - 1) {
    if ( * f != '%') {
      out[oi++] = * f++;
      continue;
    }
    f++;

    char spec[16] = {
      '%'
    };
    int si = 1;
    while ( * f && si < 14 && !isalpha((unsigned char) * f))
      spec[si++] = * f++;
    if ( * f) spec[si++] = * f++;
    spec[si] = '\0';

    char piece[SCRIPT_VAR_VAL_LEN] = {
      0
    };
    char last = spec[si - 1];
    if (last == 'd' || last == 'i' || last == 'u' || last == 'x' || last == 'o') {
      snprintf(piece, sizeof(piece), spec, (long)(ai < argc ? argv[ai] : 0));
    } else if (last == 'f' || last == 'e' || last == 'g') {
      snprintf(piece, sizeof(piece), spec, (ai < argc ? argv[ai] : 0.0));
    } else if (last == 's') {
      snprintf(piece, sizeof(piece), spec, (ai < argc ? sargv[ai] : ""));
    } else {
      snprintf(piece, sizeof(piece), "%s", spec);
    }
    ai++;
    int pl = (int) strlen(piece);
    if (oi + pl < SCRIPT_VAR_VAL_LEN - 1) {
      memcpy(out + oi, piece, (size_t) pl);
      oi += pl;
    }
  }
  out[oi] = '\0';
  var_set(ctx, dest, out);
}

static void arr_key(char * buf, int buflen,
  const char * name, int idx) {
  snprintf(buf, (size_t) buflen, "_arr_%s_%d", name, idx);
}
static void arr_lenkey(char * buf, int buflen,
  const char * name) {
  snprintf(buf, (size_t) buflen, "_arr_%s_len", name);
}

static int arr_get_len(script_ctx_t * ctx,
  const char * name) {
  char key[SCRIPT_VAR_NAME_LEN];
  arr_lenkey(key, sizeof(key), name);
  return atoi(var_get(ctx, key));
}
static void arr_set_len(script_ctx_t * ctx,
  const char * name, int len) {
  char key[SCRIPT_VAR_NAME_LEN];
  arr_lenkey(key, sizeof(key), name);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", len);
  var_set(ctx, key, buf);
}

static int find_def(char lines[][SCRIPT_LINE_LEN], int total,
  const char * fname, int * body_start, int * body_end) {
  for (int i = 0; i < total; i++) {
    char buf[SCRIPT_LINE_LEN];
    strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
    trim_inplace(buf);
    if (strncmp(buf, "def ", 4) != 0) continue;
    char defname[SCRIPT_VAR_NAME_LEN] = {
      0
    };
    sscanf(buf + 4, "%31s", defname);
    if (strcmp(defname, fname) != 0) continue;

    int depth = 1, j = i + 1;
    while (j < total && depth > 0) {
      char b2[SCRIPT_LINE_LEN];
      strncpy(b2, lines[j], SCRIPT_LINE_LEN - 1);
      trim_inplace(b2);
      char kw[32] = {
        0
      };
      sscanf(b2, "%31s", kw);
      if (!strcmp(kw, "def")) depth++;
      if (!strcmp(kw, "enddef")) depth--;
      j++;
    }
    * body_start = i + 1;
    * body_end = j - 1;
    return i;
  }
  return -1;
}

static int split(const char * s, char parts[][SCRIPT_LINE_LEN], int max) {
  char tmp[SCRIPT_LINE_LEN];
  strncpy(tmp, s, SCRIPT_LINE_LEN - 1);
  int n = 0;
  char * tok = strtok(tmp, " \t");
  while (tok && n < max) {
    strncpy(parts[n++], tok, SCRIPT_LINE_LEN - 1);
    tok = strtok(NULL, " \t");
  }
  return n;
}

static int find_end(char lines[][SCRIPT_LINE_LEN], int total,
  int from,
  const char * kw_open,
    const char * kw_close) {
  int depth = 1;
  for (int i = from; i < total; i++) {
    char buf[SCRIPT_LINE_LEN];
    strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
    trim_inplace(buf);
    char first[32] = {
      0
    };
    sscanf(buf, "%31s", first);
    if (!strcmp(first, kw_open)) depth++;
    if (!strcmp(first, kw_close)) {
      if (!--depth) return i;
    }
  }
  return -1;
}

static int find_elif_else_end(
    char lines[][SCRIPT_LINE_LEN],
    int total,
    int from,
    const char *which)
{
    int depth = 1;

    for (int i = from; i < total; i++) {
        char buf[SCRIPT_LINE_LEN];
        strncpy(buf, lines[i], SCRIPT_LINE_LEN - 1);
        buf[SCRIPT_LINE_LEN - 1] = '\0';

        trim_inplace(buf);

        char first[32] = {0};
        sscanf(buf, "%31s", first);

        if (!strcmp(first, "if"))
            depth++;

        if (!strcmp(first, "endif")) {
            depth--;

            if (depth == 0) {
                if (!strcmp(which, "endif"))
                    return i;

                return -1;
            }
        }

        if (depth == 1 && !strcmp(first, which))
            return i;
    }

    return -1;
}


// --- New builtins ---

static void builtin_usleep(script_ctx_t *ctx, const char *dest, const char *args) {
    int us = eval_int(ctx, args);
    if (us > 0 && us <= 1000000) sleep_us((uint32_t)us);
    var_set(ctx, dest, "1");
}

static void builtin_temp_c(script_ctx_t *ctx, const char *dest, const char *args) {
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
    // Replace commas with spaces for parsing
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
    // On RP2040, check ESP8266 bridge via module
    if (module_is_loaded("wifi")) {
        var_set(ctx, dest, "initialized");
    } else {
        var_set(ctx, dest, "not_init");
    }
}

static void builtin_http_get(script_ctx_t *ctx, const char *dest, const char *args) {
    char url[SCRIPT_VAR_VAL_LEN];
    expand_vars(ctx, args, url, sizeof(url));
    trim_inplace(url);
    // Use the module's wifi_http_get if available
    // For now, return empty since HTTP from scripts needs the wifi module
    printf("script: http_get requires the wifi module (module load wifi)\n");
    var_set(ctx, dest, "");
}

static void builtin_http_post(script_ctx_t *ctx, const char *dest, const char *args) {
    char tmp[SCRIPT_LINE_LEN];
    expand_vars(ctx, args, tmp, sizeof(tmp));
    char url[SCRIPT_VAR_VAL_LEN] = {0};
    char body[SCRIPT_VAR_VAL_LEN] = {0};
    sscanf(tmp, "%[^,],%[^\n]", url, body);
    trim_inplace(url);
    trim_inplace(body);
    printf("script: http_post requires the wifi module (module load wifi)\n");
    var_set(ctx, dest, "");
}

static bool try_builtin_val(script_ctx_t * ctx,
  const char * vname,
    const char * valexpr,
      char lines[][SCRIPT_LINE_LEN], int total) {

  if (!strncmp(valexpr, "adc(", 4)) {
    char arg[32] = {
      0
    };
    sscanf(valexpr + 4, "%31[^)]", arg);
    builtin_adc(ctx, vname, arg);
    return true;
  }
  if (!strncmp(valexpr, "gpio(", 5)) {
    char arg[32] = {
      0
    };
    sscanf(valexpr + 5, "%31[^)]", arg);
    builtin_gpio_read(ctx, vname, arg);
    return true;
  }
  if (!strncmp(valexpr, "pwm(", 4)) {
    char arg[SCRIPT_LINE_LEN] = {
      0
    };
    sscanf(valexpr + 4, "%[^)]", arg);
    builtin_pwm(ctx, vname, arg);
    return true;
  }
  if (!strncmp(valexpr, "millis", 6)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", to_ms_since_boot(get_absolute_time()));
    var_set(ctx, vname, buf);
    return true;
  }
  if (!strncmp(valexpr, "micros", 6)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) time_us_64());
    var_set(ctx, vname, buf);
    return true;
  }

  if (!strncmp(valexpr, "temp_c", 6) && valexpr[6] == '(') {
    builtin_temp_c(ctx, vname, "");
    return true;
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
    const char * fn;
    void( * handler)(script_ctx_t * ,
      const char * ,
        const char * );
  }
  sfns[] = {
    {
      "upper(",
      builtin_upper
    },
    {
      "lower(",
      builtin_lower
    },
    {
      "len(",
      builtin_len
    },
    {
      "substr(",
      builtin_substr
    },
    {
      "contains(",
      builtin_contains
    },
    {
      "trim(",
      builtin_trim_fn
    },
    {
      "replace(",
      builtin_replace
    },
  };
  for (int si = 0; si < (int)(sizeof(sfns) / sizeof(sfns[0])); si++) {
    if (!strncmp(valexpr, sfns[si].fn, strlen(sfns[si].fn))) {
      char arg[SCRIPT_LINE_LEN] = {
        0
      };
      sscanf(valexpr + strlen(sfns[si].fn), "%[^)]", arg);
      sfns[si].handler(ctx, vname, arg);
      return true;
    }
  }

  const char * mathfns[] = {
    "sqrt(",
    "pow(",
    "abs(",
    "min(",
    "max(",
    "clamp(",
    "map(",
    "rand(",
    "avg("
  };
  for (int mi = 0; mi < (int)(sizeof(mathfns) / sizeof(mathfns[0])); mi++) {
    if (!strncmp(valexpr, mathfns[mi], strlen(mathfns[mi]))) {
      char arg[SCRIPT_LINE_LEN] = {
        0
      };
      char fn[16] = {
        0
      };

      strncpy(fn, mathfns[mi], strlen(mathfns[mi]) - 1);
      sscanf(valexpr + strlen(mathfns[mi]), "%[^)]", arg);
      builtin_math(ctx, vname, fn, arg);
      return true;
    }
  }

  if (!strncmp(valexpr, "input(", 6)) {
    char arg[SCRIPT_LINE_LEN] = {
      0
    };
    sscanf(valexpr + 6, "%[^)]", arg);
    builtin_input(ctx, vname, arg);
    return true;
  }

  if (!strncmp(valexpr, "format(", 7)) {
    char arg[SCRIPT_LINE_LEN] = {
      0
    };
    const char * p = valexpr + 7;
    int plen = (int) strlen(p);
    if (plen > 0 && p[plen - 1] == ')') plen--;
    strncpy(arg, p, (size_t) plen);
    builtin_format(ctx, vname, arg);
    return true;
  }

  if (!strncmp(valexpr, "arr_get(", 8)) {
    char arg[SCRIPT_LINE_LEN] = {
      0
    };
    sscanf(valexpr + 8, "%[^)]", arg);
    char aname[SCRIPT_VAR_NAME_LEN] = {
      0
    };
    char aidx[16] = {
      0
    };
    sscanf(arg, "%[^,],%15s", aname, aidx);
    trim_inplace(aname);
    trim_inplace(aidx);
    char expanded_idx[32] = {
      0
    };
    expand_vars(ctx, aidx, expanded_idx, sizeof(expanded_idx));
    int idx = atoi(expanded_idx);
    char key[SCRIPT_VAR_NAME_LEN];
    arr_key(key, sizeof(key), aname, idx);
    var_set(ctx, vname, var_get(ctx, key));
    return true;
  }

  if (!strncmp(valexpr, "arr_len(", 8)) {
    char aname[SCRIPT_VAR_NAME_LEN] = {
      0
    };
    sscanf(valexpr + 8, "%[^)]", aname);
    trim_inplace(aname);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", arr_get_len(ctx, aname));
    var_set(ctx, vname, buf);
    return true;
  }

  return false;
}

static int do_include(script_ctx_t * ctx,
  const char * path) {
 
  uint8_t *buf = (uint8_t *)malloc(VFS_MAX_FILE_SIZE);
  if (!buf) {
    printf("include: out of memory\n");
    return RC_ERROR;
  }
  uint32_t flen = 0;
  if (vfs_read(path, buf, VFS_MAX_FILE_SIZE - 1, & flen) < 0) {
    printf("include: file not found: %s\n", path);
    free(buf);
    return RC_ERROR;
  }
  buf[flen] = '\0';
  int rc = script_run_string(ctx, (const char * ) buf);
  free(buf);
  return rc;
}


// Returns: 1 = numeric result in *num_out, 2 = string concat in str_out, 0 = not arithmetic
static int eval_arith(script_ctx_t *ctx, const char *expr, double *num_out,
                      char *str_out, int str_len) {
  char tmp[SCRIPT_LINE_LEN];
  strncpy(tmp, expr, SCRIPT_LINE_LEN - 1);
  tmp[SCRIPT_LINE_LEN - 1] = '\0';
  trim_inplace(tmp);

  int op_pos = -1;
  char op_ch = 0;
  for (int i = 1; tmp[i]; i++) {
    if (tmp[i] == '.' && i > 0 && tmp[i-1] == ' ' && tmp[i+1] == ' ') {
      op_pos = i; op_ch = '.'; break;
    }
    if (tmp[i] == '+' || tmp[i] == '-' ||
        tmp[i] == '*' || tmp[i] == '/' || tmp[i] == '%') {
      if (i > 0 && tmp[i-1] == ' ' && tmp[i+1] == ' ') {
        op_pos = i; op_ch = tmp[i]; break;
      }
    }
  }

  if (op_pos < 0) {
    char *end;
    double v = strtod(tmp, &end);
    if (*end == '\0') { *num_out = v; return 1; }
    return 0;
  }

  char lhs_s[SCRIPT_LINE_LEN], rhs_s[SCRIPT_LINE_LEN];
  strncpy(lhs_s, tmp, (size_t)op_pos);
  lhs_s[op_pos] = '\0';
  trim_inplace(lhs_s);
  strncpy(rhs_s, tmp + op_pos + 1, SCRIPT_LINE_LEN - 1);
  trim_inplace(rhs_s);

  // String concatenation
  if (op_ch == '.') {
    char lval[SCRIPT_LINE_LEN] = {0}, rval[SCRIPT_LINE_LEN] = {0};
    expand_vars(ctx, lhs_s, lval, sizeof(lval));
    expand_vars(ctx, rhs_s, rval, sizeof(rval));
    trim_inplace(lval);
    trim_inplace(rval);
    strncpy(str_out, lval, (size_t)(str_len - 1));
    strncat(str_out, rval, (size_t)(str_len - 1 - strlen(str_out)));
    return 2;
  }

  char *le, *re;
  double lv = strtod(lhs_s, &le);
  if (*le != '\0') {
    const char *v = var_get(ctx, lhs_s);
    lv = v[0] ? atof(v) : 0.0;
  }

  double rv = strtod(rhs_s, &re);
  if (*re != '\0') {
    const char *v = var_get(ctx, rhs_s);
    rv = v[0] ? atof(v) : 0.0;
  }

  switch (op_ch) {
    case '+': *num_out = lv + rv; break;
    case '-': *num_out = lv - rv; break;
    case '*': *num_out = lv * rv; break;
    case '/': *num_out = rv ? lv / rv : 0; break;
    case '%': *num_out = rv ? (long)lv % (long)rv : 0; break;
    default:  *num_out = lv; break;
  }
  return 1;
}

static int run_lines(script_ctx_t * ctx,
  char lines[][SCRIPT_LINE_LEN], int total,
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
      char name[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      sscanf(line + 4, "%31s", name);
      int defline = find_def(lines, total, name, & dummy_s, & dummy_e);
      if (defline < 0) continue;
      i = dummy_e + 1;
      continue;
    }
    if (!strcmp(line, "enddef")) continue;

    if (!strncmp(line, "exit", 4)) {
      int code = 0;
      if (line[4] == ' ') code = atoi(line + 5);
      ctx -> exit_code = code;
      return RC_EXIT;
    }

    if (!strncmp(line, "assert ", 7)) {
      char rest[SCRIPT_LINE_LEN];
      strncpy(rest, raw + 7, sizeof(rest) - 1);
      expand_vars(ctx, rest, rest, sizeof(rest));

      char * orfail = strstr(rest, " or fail:");
      char msg[SCRIPT_LINE_LEN] = "assertion failed";
      if (orfail) {
        strncpy(msg, orfail + 9, sizeof(msg) - 1);
        trim_inplace(msg);
        * orfail = '\0';
      }
      trim_inplace(rest);

      if (!eval_cond(ctx, rest)) {
        printf("script:%d: ASSERT FAILED: %s\n", i, msg);
        return RC_ERROR;
      }
      continue;
    }

    if (!strncmp(line, "log ", 4)) {
      char level[16] = {
        0
      }, tag[32] = {
        0
      }, msg[SCRIPT_LINE_LEN] = {
        0
      };
      sscanf(line + 4, "%15s %31s %[^\n]", level, tag, msg);
      if (!strcmp(level, "warn")) syslog_write(LOG_WARN, tag, msg);
      else if (!strcmp(level, "err")) syslog_write(LOG_ERR, tag, msg);
      else if (!strcmp(level, "debug")) syslog_write(LOG_DEBUG, tag, msg);
      else syslog_write(LOG_INFO, tag, msg);
      continue;
    }

    if (!strncmp(line, "include ", 8)) {
      char path[VFS_PATH_LEN] = {
        0
      };
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
      return -30 - d; // encode depth: -32..-38
    }
    if (!strncmp(line, "continue", 8)) {
      int d = 1;
      if (line[8] == ' ') d = atoi(line + 9);
      if (d < 1) d = 1; if (d > 8) d = 8;
      if (d == 1) return RC_CONTINUE;
      return -40 - d; // encode depth: -42..-48
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

      char fname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      sscanf(rest2, "%31s", fname);

      char * argp = rest2 + strlen(fname);
      while ( * argp == ' ') argp++;
      char parts[8][SCRIPT_LINE_LEN];
      int nargs = 0;
      char argtmp[SCRIPT_LINE_LEN];
      strncpy(argtmp, argp, sizeof(argtmp) - 1);
      char * tok = strtok(argtmp, " ");
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
      if (find_def(lines, total, fname, & bs, & be) < 0) {
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

     
      char vname[SCRIPT_VAR_NAME_LEN] = { 0 };
      char eq[4]                      = { 0 };
      char valexpr[SCRIPT_LINE_LEN]   = { 0 };

     
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

     
      char expanded[SCRIPT_LINE_LEN] = { 0 };
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
      char aname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      int sz = 0;
      sscanf(line + 8, "%31s %d", aname, & sz);
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
      char aname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      char aidx[32] = {
        0
      }, aval[SCRIPT_VAR_VAL_LEN] = {
        0
      };
      sscanf(rest, "%31s %31s %[^\n]", aname, aidx, aval);
      char eidx[32] = {
        0
      }, eval_val[SCRIPT_VAR_VAL_LEN] = {
        0
      };
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
      char aname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      char aval[SCRIPT_VAR_VAL_LEN] = {
        0
      };
      sscanf(line + 9, "%31s %[^\n]", aname, aval);
      char eval_val[SCRIPT_VAR_VAL_LEN] = {
        0
      };
      expand_vars(ctx, aval, eval_val, sizeof(eval_val));
      int idx = arr_get_len(ctx, aname);
      char key[SCRIPT_VAR_NAME_LEN];
      arr_key(key, sizeof(key), aname, idx);
      var_set(ctx, key, eval_val);
      arr_set_len(ctx, aname, idx + 1);
      continue;
    }

    if (!strncmp(line, "arr_pop ", 8)) {
      char aname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      char dest[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      sscanf(line + 8, "%31s %31s", aname, dest);
      int len = arr_get_len(ctx, aname);
      if (len <= 0) {
        if (dest[0]) var_set(ctx, dest, "");
        continue;
      }
      char key[SCRIPT_VAR_NAME_LEN];
      arr_key(key, sizeof(key), aname, len - 1);
      if (dest[0]) var_set(ctx, dest, var_get(ctx, key));
      var_set(ctx, key, "");
      arr_set_len(ctx, aname, len - 1);
      continue;
    }

    if (!strncmp(line, "arr_dump ", 9)) {
      char aname[SCRIPT_VAR_NAME_LEN] = {
        0
      };
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
      char fvar[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      char keyword[8] = {
        0
      };
      char source[SCRIPT_VAR_NAME_LEN] = {
        0
      };
      sscanf(raw + 4, "%31s %7s %31s", fvar, keyword, source);

      int fi = find_end(lines, total, i, "for", "endfor");
      if (fi < 0) {
        printf("script:%d: missing endfor\n", i);
        return RC_ERROR;
      }

      if (!strcmp(keyword, "in")) {
        
        char aname[SCRIPT_VAR_NAME_LEN] = { 0 };
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
        char to_kw[8] = { 0 };
        char to_s[32] = { 0 };
        char step_kw[8] = { 0 };
        char step_s[32] = { 0 };
        sscanf(raw + 4, "%*s %*s %*s %7s %31s %7s %31s",
          to_kw, to_s, step_kw, step_s);
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
    if (!strcmp(line, "print")) {
      printf("\n");
      continue;
    }

   
    if (!strncmp(line, "println ", 8)) {
      char out[SCRIPT_LINE_LEN];
      expand_vars(ctx, line + 8, out, sizeof(out));
      printf("%s\n", out);
      continue;
    }
    if (!strcmp(line, "println")) {
      printf("\n");
      continue;
    }

    if (!strncmp(line, "sleep ", 6)) {
      int ms = eval_int(ctx, line + 6);
      if (ms > 0 && ms <= 60000) sleep_ms((uint32_t) ms);
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

        if (!strncmp(bbuf, "elif ", 5)) {
          cond = !taken && eval_cond(ctx, bbuf + 5);
        } else if (!strcmp(bbuf, "else")) {
          cond = !taken;
        }
      }
      continue;
    }

    if (!strncmp(raw, "switch ", 7)) {
      char subject[SCRIPT_VAR_VAL_LEN] = {
        0
      };
      expand_vars(ctx, raw + 7, subject, sizeof(subject));
      trim_inplace(subject);

      int sw_end = find_end(lines, total, i, "switch", "endswitch");
      if (sw_end < 0) {
        printf("script:%d: missing endswitch\n", i);
        return RC_ERROR;
      }

      bool matched = false;
      int ci = i;

      while (ci < sw_end) {
        char cbuf[SCRIPT_LINE_LEN];
        strncpy(cbuf, lines[ci], SCRIPT_LINE_LEN - 1);
        trim_inplace(cbuf);
        ci++;

        if (!strncmp(cbuf, "case ", 5)) {
          char cval[SCRIPT_VAR_VAL_LEN] = {
            0
          };
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
            if (rc == RC_BREAK) {
              matched = true;
              break;
            }
            if (rc < 0) {
              i = sw_end + 1;
              return rc;
            }
            matched = true;
            ci = next;
          }
        } else if (!strcmp(cbuf, "default:") && !matched) {
          int rc = run_lines(ctx, lines, total, ci, sw_end);
          if (rc < 0 && rc != RC_BREAK) {
            i = sw_end + 1;
            return rc;
          }
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
      if (wi < 0) {
        printf("script:%d: missing endwhile\n", i);
        return RC_ERROR;
      }

      
      int iter = 0;
      while (true) {
        char ce[SCRIPT_LINE_LEN];
        expand_vars(ctx, cond_expr, ce, sizeof(ce));
        if (!eval_cond(ctx, ce)) break;
        if (++iter > 100000) {
          printf("script:%d: while iteration limit reached (100000)\n", i);
          break;
        }
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
      if (ri < 0) {
        printf("script:%d: missing endrepeat\n", i);
        return RC_ERROR;
      }
      
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
      if (sscanf(line + 11, "%d %d", & pin, & val) == 2) {
        if (pin >= 0 && pin <= 28) {
          gpio_init((uint) pin);
          gpio_set_dir((uint) pin, GPIO_OUT);
          gpio_put((uint) pin, val ? 1 : 0);
        }
      }
      continue;
    }

    if (!strncmp(line, "wait_pin ", 9)) {
      int pin, expected;
      int tms = 5000;
      sscanf(line + 9, "%d %d %d", & pin, & expected, & tms);
      if (pin >= 0 && pin <= 28) {
        gpio_init((uint) pin);
        gpio_set_dir((uint) pin, GPIO_IN);
        uint32_t start_ms = to_ms_since_boot(get_absolute_time());
        while (gpio_get((uint) pin) != (expected ? 1 : 0)) {
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
      if (sscanf(line + 6, "%d %d", & pin, & us) == 2 && pin >= 0 && pin <= 28) {
        gpio_init((uint) pin);
        gpio_set_dir((uint) pin, GPIO_OUT);
        gpio_put((uint) pin, 1);
        sleep_us((uint32_t) us);
        gpio_put((uint) pin, 0);
      }
      continue;
    }

    if (!strcmp(line, "echo")) {
      printf("\n");
      continue;
    }

    if (!strcmp(line, "pause")) {
      printf("[script] press any key to continue...\n");
      while (getchar_timeout_us(100000) == PICO_ERROR_TIMEOUT) {}
      continue;
    }

    if (!strcmp(line, "vars")) {
      printf("script variables:\n");
      for (int v = 0; v < ctx -> var_count; v++)
        printf("  %-15s = %s\n",
          ctx -> vars[v].name, ctx -> vars[v].value);
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

int script_run_string(script_ctx_t *ctx, const char *source) {
 
    char (*lines)[SCRIPT_LINE_LEN] =
        malloc((size_t)SCRIPT_MAX_LINES * sizeof(*lines));
 
    if (!lines) {
        printf("script: out of memory\n");
        return RC_ERROR;
    }
 
    int total = 0;
    const char *p = source;
 
    while (*p && total < SCRIPT_MAX_LINES) {
        const char *nl  = strchr(p, '\n');
        int          len = nl ? (int)(nl - p) : (int)strlen(p);
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


int script_run_file(const char * vfs_path) {
 
  uint8_t *buf = (uint8_t *)malloc(VFS_MAX_FILE_SIZE);
  if (!buf) {
    printf("script: out of memory\n");
    return -1;
  }

  uint32_t flen = 0;
  if (vfs_read(vfs_path, buf, VFS_MAX_FILE_SIZE - 1, & flen) < 0) {
    printf("script: file not found: %s\n", vfs_path);
    free(buf);
    return -1;
  }
  buf[flen] = '\0';
  printf("[script] running '%s'  (%lu bytes)\n", vfs_path, flen);

  script_ctx_t *ctx = (script_ctx_t *)malloc(sizeof(*ctx));
  if (!ctx) {
    printf("script: out of memory\n");
    free(buf);
    return -1;
  }
  script_ctx_init(ctx);
  int rc = script_run_string(ctx, (const char * ) buf);
  printf("[script] done (rc=%d)\n", rc);

  free(ctx);
  free(buf);
  return rc;
}