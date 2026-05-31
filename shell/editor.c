#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include "pico/stdlib.h"

#include "editor.h"

#include "vfs.h"

#include "file_persist.h"

#define ED_W_MAX 220
#define ED_H_MAX 60
#define GUTTER 5
#define MSG_PERSIST 8

static int ED_W = 80;
static int ED_H = 24;
#define ED_ROWS (ED_H - 3)
#define TW (ED_W - GUTTER)

typedef struct {
  char b[ED_LINELEN];
}
Line;

static Line * s_L = NULL;
static int s_n;
static int s_row, s_col;
static int s_top;
static int s_left;
static bool s_dirty;
static char s_fname[64];
static char s_msg[84];
static int s_msg_ttl;

static Line * s_undo_L = NULL;
static int s_undo_n, s_undo_row, s_undo_col;
static bool s_undo_valid;

static char s_kill[ED_LINELEN];
static bool s_kill_valid;

static Line * s_prev_L = NULL;
static int s_prev_top, s_prev_left, s_prev_n;
static int s_prev_row, s_prev_col;
static bool s_prev_dirty;
static int s_prev_msg_ttl;
static bool s_full_redraw;
static char s_prev_msg[84];

static int s_ungot;

static int ll(int r) {
  return (int) strlen(s_L[r].b);
}

static void clamp_col(void) {
  int l = ll(s_row);
  if (s_col > l) s_col = l;
}

static void setmsg(const char * m) {
  strncpy(s_msg, m, sizeof(s_msg) - 1);
  s_msg[sizeof(s_msg) - 1] = '\0';
  s_msg_ttl = MSG_PERSIST;
}

static void snapshot_undo(void) {
  memcpy(s_undo_L, s_L, sizeof(Line) * (size_t) s_n);
  s_undo_n = s_n;
  s_undo_row = s_row;
  s_undo_col = s_col;
  s_undo_valid = true;
}

static void t_goto(int r, int c) {
  printf("\033[%d;%dH", r, c);
}
static void t_rev(void) {
  fputs("\033[7m", stdout);
}
static void t_dim(void) {
  fputs("\033[2m", stdout);
}
static void t_bold(void) {
  fputs("\033[1m", stdout);
}
static void t_rst(void) {
  fputs("\033[0m", stdout);
}
static void t_hide(void) {
  fputs("\033[?25l", stdout);
}
static void t_show(void) {
  fputs("\033[?25h", stdout);
}
static void t_cls(void) {
  fputs("\033[2J\033[H", stdout);
}
static void t_bp_off(void) {
  fputs("\033[?2004l", stdout);
}
static void t_bp_on(void) {
  fputs("\033[?2004h", stdout);
}
static void t_wrap_off(void) {
  fputs("\033[?7l", stdout);
}
static void t_wrap_on(void) {
  fputs("\033[?7h", stdout);
}

static void ed_query_size(void) {

  while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {}

  fputs("\033[999;999H\033[6n", stdout);
  fflush(stdout);

  int c = getchar_timeout_us(120000);
  if (c != 27) return;
  c = getchar_timeout_us(50000);
  if (c != '[') return;

  int rows = 0, cols = 0;
  c = getchar_timeout_us(50000);
  while (c >= '0' && c <= '9') {
    rows = rows * 10 + (c - '0');
    c = getchar_timeout_us(50000);
  }
  if (c != ';') return;
  c = getchar_timeout_us(50000);
  while (c >= '0' && c <= '9') {
    cols = cols * 10 + (c - '0');
    c = getchar_timeout_us(50000);
  }
  if (c != 'R') return;

  if (rows >= 8 && rows <= ED_H_MAX) ED_H = rows;
  if (cols >= 20 && cols <= ED_W_MAX) ED_W = cols;
}

static void fixed(const char * s, int len, int w) {
  int i = 0, written = 0;
  while (i < len && written < w) {
    unsigned char c = (unsigned char) s[i++];
    putchar((c >= 0x20 && c < 0x7F) ? (char) c : ' ');
    written++;
  }
  while (written < w) {
    putchar(' ');
    written++;
  }
}
static void fixedz(const char * s, int w) {
  fixed(s, (int) strlen(s), w);
}

#define K_UP 0x100
#define K_DOWN 0x101
#define K_LEFT 0x102
#define K_RIGHT 0x103
#define K_HOME 0x104
#define K_END 0x105
#define K_PGUP 0x106
#define K_PGDN 0x107
#define K_DEL 0x108
#define K_BS 127
#define K_TAB 9
#define K_RET 13
#define K_CS 19
#define K_CQ 17
#define K_CX 24
#define K_CK 11
#define K_CU 21
#define K_CY 25
#define K_CF 6
#define K_CG 7
#define K_CZ 26

static int parse_csi(void) {
  int c3 = getchar_timeout_us(40000);
  if (c3 == PICO_ERROR_TIMEOUT) return 27;

  switch (c3) {
  case 'A':
    return K_UP;
  case 'B':
    return K_DOWN;
  case 'C':
    return K_RIGHT;
  case 'D':
    return K_LEFT;
  case 'H':
    return K_HOME;
  case 'F':
    return K_END;
  }

  if (c3 >= '0' && c3 <= '9') {
    char nb[8];
    int nl = 0;
    nb[nl++] = (char) c3;
    int cn;
    while (nl < 7) {
      cn = getchar_timeout_us(20000);
      if (cn == PICO_ERROR_TIMEOUT) break;
      if ((cn >= '0' && cn <= '9') || cn == ';') {
        nb[nl++] = (char) cn;
        continue;
      }
      nb[nl] = '\0';
      int num = atoi(nb);
      if (cn == '~') {
        switch (num) {
        case 1:
        case 7:
          return K_HOME;
        case 3:
          return K_DEL;
        case 4:
        case 8:
          return K_END;
        case 5:
          return K_PGUP;
        case 6:
          return K_PGDN;

        case 200:
        case 201:
          return -1;
        }
      }
      switch (cn) {
      case 'A':
        return K_UP;
      case 'B':
        return K_DOWN;
      case 'C':
        return K_RIGHT;
      case 'D':
        return K_LEFT;
      case 'H':
        return K_HOME;
      case 'F':
        return K_END;
      }
      return 27;
    }
  }
  return 27;
}

static int getkey(void) {
  int c;
  if (s_ungot >= 0) {
    c = s_ungot;
    s_ungot = -1;
  } else {
    while ((c = getchar_timeout_us(50000)) == PICO_ERROR_TIMEOUT) {}
  }

  if (c == '\r' || c == '\n') {
    int next = getchar_timeout_us(5000);
    if (next != PICO_ERROR_TIMEOUT && next != '\n' && next != '\r')
      s_ungot = next;
    return K_RET;
  }

  if (c != 27) return c;

  int c2 = getchar_timeout_us(40000);
  if (c2 == PICO_ERROR_TIMEOUT) return 27;

  if (c2 == 'O') {
    int c3 = getchar_timeout_us(40000);
    if (c3 == PICO_ERROR_TIMEOUT) return 27;
    switch (c3) {
    case 'A':
      return K_UP;
    case 'B':
      return K_DOWN;
    case 'C':
      return K_RIGHT;
    case 'D':
      return K_LEFT;
    case 'H':
      return K_HOME;
    case 'F':
      return K_END;
    }
    return 27;
  }

  if (c2 == '[') return parse_csi();
  return 27;
}

static int getkey_nb(void) {
  int c;
  if (s_ungot >= 0) {
    c = s_ungot;
    s_ungot = -1;
  } else {
    c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return PICO_ERROR_TIMEOUT;
  }

  if (c == '\r' || c == '\n') {
    int next = getchar_timeout_us(5000);
    if (next != PICO_ERROR_TIMEOUT && next != '\n' && next != '\r')
      s_ungot = next;
    return K_RET;
  }

  if (c == 27) {
    int c2 = getchar_timeout_us(5000);
    if (c2 == PICO_ERROR_TIMEOUT) return 27;
    if (c2 == '[') {
      int r = parse_csi();
      return (r == -1) ? PICO_ERROR_TIMEOUT : r;
    }
    return 27;
  }

  return c;
}

static void scroll_adjust(void) {
  if (s_row < s_top) s_top = s_row;
  if (s_row >= s_top + ED_ROWS) s_top = s_row - ED_ROWS + 1;
  if (s_col < s_left) s_left = s_col;
  if (s_col >= s_left + TW) s_left = s_col - TW + 1;
}

static void redraw(void) {
  scroll_adjust();

  bool scroll_changed = (s_top != s_prev_top ||
    s_left != s_prev_left ||
    s_n != s_prev_n);
  if (scroll_changed) s_full_redraw = true;

  t_hide();

  bool title_dirty = s_full_redraw ||
    s_row != s_prev_row ||
    s_col != s_prev_col ||
    s_dirty != s_prev_dirty;
  if (title_dirty) {
    t_goto(1, 1);
    t_rev();
    t_bold();
    char tb[ED_W_MAX + 2];
    int pos = 0;
    const char * pfx = " DeckOS Editor | ";
    int pl = (int) strlen(pfx);
    memcpy(tb + pos, pfx, (size_t) pl);
    pos += pl;
    int fnl = (int) strlen(s_fname);
    if (fnl > 24) fnl = 24;
    memcpy(tb + pos, s_fname, (size_t) fnl);
    pos += fnl;
    while (pos < pl + 24) tb[pos++] = ' ';
    const char * dm = s_dirty ? " [+]" : "    ";
    memcpy(tb + pos, dm, 4);
    pos += 4;
    char lc[20];
    int lcl = snprintf(lc, sizeof(lc), "  Ln %-4d Col %-3d", s_row + 1, s_col + 1);
    if (pos + lcl <= ED_W) {
      memcpy(tb + pos, lc, (size_t) lcl);
      pos += lcl;
    }
    tb[pos] = '\0';
    fixed(tb, pos, ED_W);
    t_rst();
  }

  t_goto(2, 1);

  for (int vr = 0; vr < ED_ROWS; vr++) {
    int li = s_top + vr;

    const char * src = (li < s_n) ? s_L[li].b : NULL;
    const char * prev = (li < s_prev_n && !s_full_redraw) ? s_prev_L[li].b : NULL;

    bool row_dirty = s_full_redraw;
    if (!row_dirty) {
      if (li < s_n && li < s_prev_n) {
        row_dirty = (strcmp(s_L[li].b, s_prev_L[li].b) != 0);
      } else {
        row_dirty = ((li < s_n) != (li < s_prev_n));
      }
      (void) prev;
    }

    if (!row_dirty) continue;

    t_goto(vr + 2, 1);
    if (src) {
      char g[8];
      snprintf(g, sizeof(g), "%3d  ", li + 1);
      t_dim();
      fputs(g, stdout);
      t_rst();
      int len = (int) strlen(src);
      int start = s_left < len ? s_left : len;
      fixed(src + start, len - start, TW);
    } else {
      t_dim();
      fputs("  ~  ", stdout);
      t_rst();
      for (int i = GUTTER; i < ED_W; i++) putchar(' ');
    }

    if (li < ED_MAXLINES) {
      if (li < s_n) strncpy(s_prev_L[li].b, s_L[li].b, ED_LINELEN - 1);
      else s_prev_L[li].b[0] = '\0';
    }
  }

  if (s_full_redraw) {
    t_goto(ED_ROWS + 2, 1);
    t_rev();
    fixedz(" ^S Save  ^Q Quit  ^K Cut  ^Y Paste  ^U Blank  ^F Find  ^Z Undo  ^G Help",
      ED_W);
    t_rst();
    t_goto(ED_ROWS + 3, 1);
  }

  bool msg_dirty = s_full_redraw ||
    s_msg_ttl != s_prev_msg_ttl ||
    strcmp(s_msg, s_prev_msg) != 0;
  if (msg_dirty) {
    t_goto(ED_ROWS + 3, 1);
    if (s_msg_ttl > 0) {
      char mb[ED_W_MAX + 2];
      int mbl = snprintf(mb, sizeof(mb), " %s", s_msg);
      t_bold();
      fixed(mb, mbl, ED_W);
      t_rst();
    } else {
      for (int i = 0; i < ED_W; i++) putchar(' ');
    }
    strncpy(s_prev_msg, s_msg, sizeof(s_prev_msg) - 1);
    s_prev_msg_ttl = s_msg_ttl;
  }
  if (s_msg_ttl > 0) s_msg_ttl--;

  int scr_col = (s_col - s_left) + GUTTER + 1;
  if (scr_col < GUTTER + 1) scr_col = GUTTER + 1;
  if (scr_col > ED_W) scr_col = ED_W;
  t_goto(s_row - s_top + 2, scr_col);
  t_show();
  fflush(stdout);

  s_prev_top = s_top;
  s_prev_left = s_left;
  s_prev_n = s_n;
  s_prev_row = s_row;
  s_prev_col = s_col;
  s_prev_dirty = s_dirty;
  s_full_redraw = false;
}

static void ed_ins(char c) {
  char * b = s_L[s_row].b;
  int len = ll(s_row);
  if (len >= ED_LINELEN - 1) {
    setmsg("line too long");
    return;
  }
  memmove(b + s_col + 1, b + s_col, (size_t)(len - s_col + 1));
  b[s_col++] = c;
  s_dirty = true;
}

static void ed_bksp(void) {
  if (s_col > 0) {
    char * b = s_L[s_row].b;
    int len = ll(s_row);
    memmove(b + s_col - 1, b + s_col, (size_t)(len - s_col + 1));
    s_col--;
    s_dirty = true;
  } else if (s_row > 0) {
    char * prev = s_L[s_row - 1].b;
    char * curr = s_L[s_row].b;
    int pl = ll(s_row - 1);
    int cl = ll(s_row);
    if (pl + cl <= ED_LINELEN - 1) {
      memcpy(prev + pl, curr, (size_t)(cl + 1));
      for (int i = s_row; i < s_n - 1; i++) s_L[i] = s_L[i + 1];
      s_n--;
      s_row--;
      s_col = pl;
      s_dirty = true;
    } else {
      setmsg("can't merge: lines too long");
    }
  }
}

static void ed_del(void) {
  char * b = s_L[s_row].b;
  int len = ll(s_row);
  if (s_col < len) {
    memmove(b + s_col, b + s_col + 1, (size_t)(len - s_col));
    s_dirty = true;
  } else if (s_row < s_n - 1) {
    char * nxt = s_L[s_row + 1].b;
    if (len + ll(s_row + 1) <= ED_LINELEN - 1) {
      memcpy(b + len, nxt, strlen(nxt) + 1);
      for (int i = s_row + 1; i < s_n - 1; i++) s_L[i] = s_L[i + 1];
      s_n--;
      s_dirty = true;
    }
  }
}

static void ed_newline(void) {
  if (s_n >= ED_MAXLINES) {
    setmsg("max lines reached");
    return;
  }
  snapshot_undo();
  char tail[ED_LINELEN];
  char * b = s_L[s_row].b;
  strncpy(tail, b + s_col, ED_LINELEN - 1);
  tail[ED_LINELEN - 1] = '\0';
  b[s_col] = '\0';
  for (int i = s_n; i > s_row + 1; i--) s_L[i] = s_L[i - 1];
  s_n++;
  strncpy(s_L[s_row + 1].b, tail, ED_LINELEN - 1);
  s_L[s_row + 1].b[ED_LINELEN - 1] = '\0';
  s_row++;
  s_col = 0;
  s_dirty = true;
}

static void ed_kill_line(void) {
  snapshot_undo();
  strncpy(s_kill, s_L[s_row].b, ED_LINELEN - 1);
  s_kill[ED_LINELEN - 1] = '\0';
  s_kill_valid = true;

  if (s_n <= 1) {
    s_L[0].b[0] = '\0';
    s_col = 0;
  } else {
    for (int i = s_row; i < s_n - 1; i++) s_L[i] = s_L[i + 1];
    s_n--;
    if (s_row >= s_n) s_row = s_n - 1;
    s_col = 0;
  }
  s_dirty = true;
  setmsg("line cut  (^Y to paste)");
}

static void ed_yank(void) {
  if (!s_kill_valid) {
    setmsg("nothing to paste");
    return;
  }
  if (s_n >= ED_MAXLINES) {
    setmsg("max lines reached");
    return;
  }
  snapshot_undo();

  int dest = s_row + 1;
  for (int i = s_n; i > dest; i--) s_L[i] = s_L[i - 1];
  s_n++;
  strncpy(s_L[dest].b, s_kill, ED_LINELEN - 1);
  s_L[dest].b[ED_LINELEN - 1] = '\0';
  s_row = dest;
  s_col = 0;
  s_dirty = true;
  setmsg("line pasted");
}

static void ed_ins_blank(void) {
  if (s_n >= ED_MAXLINES) {
    setmsg("max lines reached");
    return;
  }
  snapshot_undo();
  for (int i = s_n; i > s_row; i--) s_L[i] = s_L[i - 1];
  s_n++;
  s_L[s_row].b[0] = '\0';
  s_col = 0;
  s_dirty = true;
  setmsg("blank line inserted  (^Z to undo)");
}

static void ed_undo(void) {
  if (!s_undo_valid) {
    setmsg("nothing to undo");
    return;
  }
  memcpy(s_L, s_undo_L, sizeof(Line) * (size_t) s_undo_n);
  s_n = s_undo_n;
  s_row = s_undo_row;
  s_col = s_undo_col;
  s_undo_valid = false;
  s_dirty = true;
  s_full_redraw = true;
  setmsg("undo applied");
}

static void load(void) {
  uint8_t raw[VFS_MAX_FILE_SIZE];
  uint32_t rlen = 0;

  if (vfs_touch(s_fname) < 0) {
    setmsg("ERROR: cannot create file");
    return;
  }

  if (vfs_read(s_fname, raw, sizeof(raw) - 1, & rlen) < 0 || rlen == 0) {

    s_n = 1;
    s_L[0].b[0] = '\0';
    s_row = s_col = s_top = s_left = 0;
    s_dirty = false;
    setmsg("new file");
    return;
  }

  raw[rlen] = '\0';
  s_n = 0;
  const char * p = (const char * ) raw;
  while ( * p && s_n < ED_MAXLINES) {
    const char * nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int) strlen(p);
    if (len > 0 && p[len - 1] == '\r') len--;
    if (len > ED_LINELEN - 1) len = ED_LINELEN - 1;
    if (len < 0) len = 0;
    strncpy(s_L[s_n].b, p, (size_t) len);
    s_L[s_n].b[len] = '\0';
    s_n++;
    if (!nl) break;
    p = nl + 1;
  }
  if (s_n == 0) {
    s_n = 1;
    s_L[0].b[0] = '\0';
  }
  s_row = s_col = s_top = s_left = 0;
  s_dirty = false;
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "loaded %d lines (%lu B)", s_n, (unsigned long) rlen);
  setmsg(tmp);
}

static bool save(void) {
  uint8_t raw[VFS_MAX_FILE_SIZE];
  int pos = 0;
  for (int i = 0; i < s_n && pos < (int) sizeof(raw) - 2; i++) {
    int len = ll(i);
    if (pos + len + 1 >= (int) sizeof(raw)) {
      setmsg("WARNING: truncated");
      break;
    }
    memcpy(raw + pos, s_L[i].b, (size_t) len);
    pos += len;
    raw[pos++] = '\n';
  }
  if (vfs_write(s_fname, raw, (uint32_t) pos, false) < 0) {
    setmsg("ERROR: save failed");
    return false;
  }
  vfs_save();
  s_dirty = false;
  char tmp[48];
  snprintf(tmp, sizeof(tmp), "saved %d bytes", pos);
  setmsg(tmp);
  return true;
}

static char s_last_pat[40];

static void ed_find(void) {
  t_goto(ED_ROWS + 3, 1);
  t_bold();
  if (s_last_pat[0])
    fixedz(" Search (Enter=repeat): ", ED_W);
  else
    fixedz(" Search: ", ED_W);
  t_rst();
  t_goto(ED_ROWS + 3, s_last_pat[0] ? 25 : 10);
  t_show();
  fflush(stdout);

  char pat[40];
  pat[0] = '\0';
  int plen = 0, c;

  while (1) {
    c = getkey();
    if (c == 27 || c == K_RET) break;
    if ((c == K_BS || c == '\b') && plen > 0) {
      pat[--plen] = '\0';
      fputs("\b \b", stdout);
      fflush(stdout);
    } else if (c >= 32 && c < 127 && plen < (int) sizeof(pat) - 1) {
      pat[plen++] = (char) c;
      pat[plen] = '\0';
      putchar(c);
      fflush(stdout);
    }
  }

  const char * search = (plen > 0) ? pat : s_last_pat;
  if (!search[0]) {
    s_full_redraw = true;
    return;
  }
  if (plen > 0) strncpy(s_last_pat, pat, sizeof(s_last_pat) - 1);

  s_full_redraw = true;
  if (c == 27) return;

  for (int pass = 0; pass < 2; pass++) {
    int r0 = (pass == 0) ? s_row : 0;
    int r1 = (pass == 0) ? s_n : s_row + 1;
    for (int r = r0; r < r1; r++) {
      int st = (pass == 0 && r == s_row) ? s_col + 1 : 0;
      char * f = strstr(s_L[r].b + st, search);
      if (f) {
        s_row = r;
        s_col = (int)(f - s_L[r].b);
        setmsg(pass == 0 ? "found" : "found (wrapped)");
        return;
      }
    }
  }
  setmsg("not found");
}

static bool ed_quit(void) {
  if (!s_dirty) return true;
  t_goto(ED_ROWS + 3, 1);
  t_bold();
  fixedz(" Unsaved changes -- save? (y)es / (n)o / (c)ancel : ", ED_W);
  t_rst();
  t_show();
  fflush(stdout);
  int c = getkey();
  if (c == 'y' || c == 'Y') {
    save();
    return true;
  }
  if (c == 'n' || c == 'N') return true;
  s_full_redraw = true;
  setmsg("quit cancelled");
  return false;
}

static bool handle_key(int k) {
  switch (k) {
  case K_UP:
    if (s_row > 0) {
      s_row--;
      clamp_col();
    }
    break;
  case K_DOWN:
    if (s_row < s_n - 1) {
      s_row++;
      clamp_col();
    }
    break;
  case K_LEFT:
    if (s_col > 0) s_col--;
    else if (s_row > 0) {
      s_row--;
      s_col = ll(s_row);
    }
    break;
  case K_RIGHT:
    if (s_col < ll(s_row)) s_col++;
    else if (s_row < s_n - 1) {
      s_row++;
      s_col = 0;
    }
    break;
  case K_HOME:
    s_col = 0;
    break;
  case K_END:
    s_col = ll(s_row);
    break;
  case K_PGUP:
    s_row -= ED_ROWS;
    if (s_row < 0) s_row = 0;
    clamp_col();
    break;
  case K_PGDN:
    s_row += ED_ROWS;
    if (s_row >= s_n) s_row = s_n - 1;
    clamp_col();
    break;

  case K_RET:
  case '\n':
    ed_newline();
    break;
  case K_BS:
  case '\b':
    ed_bksp();
    break;
  case K_DEL:
    ed_del();
    break;
  case K_TAB:
    for (int i = 0; i < 4; i++) ed_ins(' ');
    break;

  case K_CK:
    ed_kill_line();
    break;
  case K_CY:
    ed_yank();
    break;
  case K_CU:
    ed_ins_blank();
    break;
  case K_CZ:
    ed_undo();
    break;
  case K_CS:
    save();
    break;
  case K_CF:
    ed_find();
    break;

  case K_CQ:
  case K_CX:
    return ed_quit();

  case K_CG:
    setmsg("^S Save  ^Q Quit  ^K Cut  ^Y Paste  ^U Blank  ^F Find  ^Z Undo");
    break;

  default:
    if (k >= 32 && k < 127) ed_ins((char) k);
    break;
  }
  return false;
}

bool editor_is_loaded(void) {
  return s_L != NULL;
}

bool editor_module_load(void) {
  if (s_L) return true;
  s_L = (Line * ) malloc(sizeof(Line) * ED_MAXLINES);
  s_undo_L = (Line * ) malloc(sizeof(Line) * ED_MAXLINES);
  s_prev_L = (Line * ) malloc(sizeof(Line) * ED_MAXLINES);
  if (!s_L || !s_undo_L || !s_prev_L) {
    editor_module_unload();
    return false;
  }
  return true;
}

void editor_module_unload(void) {
  free(s_L);
  s_L = NULL;
  free(s_undo_L);
  s_undo_L = NULL;
  free(s_prev_L);
  s_prev_L = NULL;
}

void editor_run(const char * vfs_path) {

  if (!s_L) {
    printf("editor: module not loaded -- run 'module load editor' first\n");
    return;
  }

  s_ungot = -1;
  memset(s_L, 0, sizeof(Line) * ED_MAXLINES);

  memset(s_prev_L, 0xFF, sizeof(Line) * ED_MAXLINES);
  s_n = 1;
  s_row = s_col = s_top = s_left = 0;
  s_dirty = false;
  s_msg[0] = '\0';
  s_prev_msg[0] = '\0';
  s_msg_ttl = 0;
  s_prev_msg_ttl = -1;
  s_undo_valid = false;
  s_kill_valid = false;
  s_kill[0] = '\0';
  s_last_pat[0] = '\0';
  s_full_redraw = true;
  s_prev_top = s_prev_left = s_prev_n = -1;
  s_prev_row = s_prev_col = -1;
  s_prev_dirty = false;

  strncpy(s_fname, vfs_path, sizeof(s_fname) - 1);
  s_fname[sizeof(s_fname) - 1] = '\0';

  ED_W = 80;
  ED_H = 24;
  ed_query_size();

  load();

  t_cls();
  t_bp_off();
  t_wrap_off();
  fflush(stdout);
  sleep_ms(20);

  for (;;) {
    redraw();

    int k = getkey();
    do {
      if (handle_key(k)) goto done;
      k = getkey_nb();
    } while (k != PICO_ERROR_TIMEOUT);
  }

  done:
    t_wrap_on();
  t_bp_on();
  t_cls();
  t_show();
  fflush(stdout);
  printf("[editor] closed '%s'\n", s_fname);
}