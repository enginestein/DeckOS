#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "vfs.h"


static vfs_node_t s_nodes[VFS_MAX_NODES];
static int        s_cwd = 0;


static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}
static int alloc_node(void) {
    for (int i = 1; i < VFS_MAX_NODES; i++)
        if (!s_nodes[i].used) return i;
    return -1;
}

static void node_path(int idx, char *buf, int buflen) {
    if (idx == 0) { strncpy(buf, "/", (size_t)buflen); return; }

    char parts[16][VFS_NAME_LEN];
    int  depth = 0;
    int  cur   = idx;

    while (cur != 0 && depth < 16) {
        strncpy(parts[depth++], s_nodes[cur].name, VFS_NAME_LEN - 1);
        cur = s_nodes[cur].parent;
    }

    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        strncat(buf, "/",       (size_t)(buflen - 1) - strlen(buf));
        strncat(buf, parts[i],  (size_t)(buflen - 1) - strlen(buf));
    }
    if (buf[0] == '\0') strncpy(buf, "/", (size_t)buflen);
}

static int resolve_from(int start, const char *path) {
    char  tmp[VFS_PATH_LEN];
    strncpy(tmp, path, VFS_PATH_LEN - 1);
    tmp[VFS_PATH_LEN - 1] = '\0';

    int   cur = start;
    char *p   = tmp;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char *end   = p;
        while (*end && *end != '/') end++;
        char  saved = *end;
        *end = '\0';

        if (strcmp(p, ".") == 0) {
        } else if (strcmp(p, "..") == 0) {
            if (cur != 0) cur = s_nodes[cur].parent;
        } else {
            int found = -1;
            for (int i = 0; i < VFS_MAX_NODES; i++) {
                if (!s_nodes[i].used)                     continue;
                if (i == cur)                             continue;
                if (s_nodes[i].parent != (int16_t)cur)   continue;
                if (strcmp(s_nodes[i].name, p) == 0)   { found = i; break; }
            }
            *end = saved;
            if (found < 0) return -1;
            cur = found;
            p   = end;
            continue;
        }

        *end = saved;
        p    = end;
    }
    return cur;
}

int vfs_resolve(const char *path) {
    if (!path || !*path)          return s_cwd;
    if (strcmp(path, "/") == 0)   return 0;
    if (path[0] == '/')           return resolve_from(0, path + 1);
    return resolve_from(s_cwd, path);
}

int vfs_resolve_parent(const char *path, char *out_name, int name_len) {
    if (!path || !*path) return -1;

    char tmp[VFS_PATH_LEN];
    strncpy(tmp, path, VFS_PATH_LEN - 1);
    tmp[VFS_PATH_LEN - 1] = '\0';

    int len = (int)strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

    char *last = strrchr(tmp, '/');

    if (!last) {
        strncpy(out_name, tmp, (size_t)name_len - 1);
        out_name[name_len - 1] = '\0';
        return s_cwd;
    }

    strncpy(out_name, last + 1, (size_t)name_len - 1);
    out_name[name_len - 1] = '\0';

    if (last == tmp) return 0; 

    *last = '\0';
    return vfs_resolve(tmp);
}


void vfs_init(void) {
    memset(s_nodes, 0, sizeof(s_nodes));

    s_nodes[0].used        = true;
    s_nodes[0].type        = VFS_DIR;
    s_nodes[0].parent      = 0; 
    strncpy(s_nodes[0].name, "/", VFS_NAME_LEN);
    s_nodes[0].created_ms  = now_ms();
    s_nodes[0].modified_ms = now_ms();
    s_cwd = 0;

    vfs_mkdir("/tmp");
    vfs_mkdir("/home");

    printf("[vfs] RAM filesystem ready  %d nodes × %d B  (~%lu KB)\n",
           VFS_MAX_NODES, VFS_MAX_FILE_SIZE,
           (uint32_t)(sizeof(s_nodes) / 1024));
}

int vfs_mkdir(const char *path) {
    char name[VFS_NAME_LEN];
    int  parent = vfs_resolve_parent(path, name, sizeof(name));

    if (parent < 0)                       { printf("mkdir: bad path '%s'\n", path); return -1; }
    if (!name[0])                          { printf("mkdir: empty name\n");          return -1; }
    if (strlen(name) >= VFS_NAME_LEN)      { printf("mkdir: name too long\n");       return -1; }
    if (s_nodes[parent].type != VFS_DIR)   { printf("mkdir: parent is not a dir\n"); return -1; }

    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].parent == (int16_t)parent &&
            strcmp(s_nodes[i].name, name) == 0) {
            printf("mkdir: '%s': already exists\n", name);
            return -1;
        }
    }

    int idx = alloc_node();
    if (idx < 0) { printf("vfs: filesystem full (%d nodes max)\n", VFS_MAX_NODES); return -1; }

    memset(&s_nodes[idx], 0, sizeof(vfs_node_t));
    s_nodes[idx].used        = true;
    s_nodes[idx].type        = VFS_DIR;
    s_nodes[idx].parent      = (int16_t)parent;
    strncpy(s_nodes[idx].name, name, VFS_NAME_LEN - 1);
    s_nodes[idx].created_ms  = now_ms();
    s_nodes[idx].modified_ms = now_ms();
    return idx;
}

int vfs_ls(const char *path) {
    const char *p = (path && *path) ? path : ".";
    int dir = vfs_resolve(p);

    if (dir < 0)                      { printf("ls: '%s': not found\n", p);       return -1; }
    if (s_nodes[dir].type != VFS_DIR) { printf("ls: '%s': not a directory\n", p); return -1; }

    char fullpath[VFS_PATH_LEN];
    node_path(dir, fullpath, sizeof(fullpath));
    printf("%s:\n", fullpath);
    printf("  %-22s  %-4s  %s\n", "name", "type", "size");
    printf("  %-22s  %-4s  %s\n",
           "──────────────────────", "────", "────");

    int count = 0;
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (!s_nodes[i].used)                    continue;
        if (s_nodes[i].parent != (int16_t)dir)   continue;
        if (s_nodes[i].type == VFS_DIR)
            printf("  %-22s  dir \n",          s_nodes[i].name);
        else
            printf("  %-22s  file  %lu B\n",   s_nodes[i].name, s_nodes[i].size);
        count++;
    }
    if (count == 0) printf("  (empty)\n");
    printf("  %d item(s)\n", count);
    return 0;
}

bool vfs_cd(const char *path) {
    const char *p = (path && *path) ? path : "/";
    int idx = vfs_resolve(p);
    if (idx < 0)                        { printf("cd: '%s': not found\n", p);       return false; }
    if (s_nodes[idx].type != VFS_DIR)   { printf("cd: '%s': not a directory\n", p); return false; }
    s_cwd = idx;
    return true;
}

void vfs_pwd(void) {
    char buf[VFS_PATH_LEN];
    node_path(s_cwd, buf, sizeof(buf));
    printf("%s\n", buf);
}


static void tree_print(int dir, const char *prefix) {
    int  children[VFS_MAX_NODES];
    int  nc = 0;
    for (int i = 1; i < VFS_MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].parent == (int16_t)dir)
            children[nc++] = i;

    for (int c = 0; c < nc; c++) {
        bool last   = (c == nc - 1);
        int  idx    = children[c];
        bool is_dir = (s_nodes[idx].type == VFS_DIR);
        printf("%s%s%s%s\n",
               prefix,
               last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " 
                    : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ",
               s_nodes[idx].name,
               is_dir ? "/" : "");
        if (is_dir) {
            char np[VFS_PATH_LEN];
            snprintf(np, sizeof(np), "%s%s", prefix,
                     last ? "    " : "\xe2\x94\x82   ");
            tree_print(idx, np);
        }
    }
}

void vfs_tree(void) {
    printf("/\n");
    tree_print(0, "");
}


int vfs_touch(const char *path) {
    int idx = vfs_resolve(path);
    if (idx >= 0) { s_nodes[idx].modified_ms = now_ms(); return idx; }

    char name[VFS_NAME_LEN];
    int  parent = vfs_resolve_parent(path, name, sizeof(name));

    if (parent < 0)                      { printf("touch: bad path '%s'\n", path);  return -1; }
    if (s_nodes[parent].type != VFS_DIR) { printf("touch: parent not a dir\n");     return -1; }
    if (!name[0] || strlen(name) >= VFS_NAME_LEN) {
        printf("touch: invalid name '%s'\n", name);
        return -1;
    }

    int new_idx = alloc_node();
    if (new_idx < 0) { printf("vfs: filesystem full\n"); return -1; }

    memset(&s_nodes[new_idx], 0, sizeof(vfs_node_t));
    s_nodes[new_idx].used        = true;
    s_nodes[new_idx].type        = VFS_FILE;
    s_nodes[new_idx].parent      = (int16_t)parent;
    strncpy(s_nodes[new_idx].name, name, VFS_NAME_LEN - 1);
    s_nodes[new_idx].created_ms  = now_ms();
    s_nodes[new_idx].modified_ms = now_ms();
    return new_idx;
}

int vfs_write(const char *path, const uint8_t *data, uint32_t len, bool append) {
    int idx = vfs_resolve(path);
    if (idx < 0) {
        idx = vfs_touch(path);
        if (idx < 0) return -1;
    }
    if (s_nodes[idx].type != VFS_FILE) {
        printf("vfs: '%s' is a directory\n", path);
        return -1;
    }

    uint32_t offset = append ? s_nodes[idx].size : 0u;
    if (offset >= VFS_MAX_FILE_SIZE) { printf("vfs: file full\n"); return -1; }
    if (offset + len > VFS_MAX_FILE_SIZE) {
        len = VFS_MAX_FILE_SIZE - offset;
        printf("vfs: content truncated to %lu B (limit %d B/file)\n",
               offset + len, VFS_MAX_FILE_SIZE);
    }

    memcpy(s_nodes[idx].data + offset, data, len);
    s_nodes[idx].size        = offset + len;
    s_nodes[idx].modified_ms = now_ms();
    return (int)len;
}

int vfs_read(const char *path, uint8_t *buf, uint32_t buflen, uint32_t *out_len) {
    int idx = vfs_resolve(path);
    if (idx < 0)                       { printf("vfs: '%s': not found\n", path);       return -1; }
    if (s_nodes[idx].type != VFS_FILE) { printf("vfs: '%s': is a directory\n", path); return -1; }
    uint32_t n = s_nodes[idx].size < buflen ? s_nodes[idx].size : buflen;
    memcpy(buf, s_nodes[idx].data, n);
    if (out_len) *out_len = n;
    return (int)n;
}

int vfs_cat(const char *path) {
    int idx = vfs_resolve(path);
    if (idx < 0)                       { printf("cat: '%s': not found\n", path);             return -1; }
    if (s_nodes[idx].type != VFS_FILE) { printf("cat: '%s': is a directory (use ls)\n", path); return -1; }

    uint32_t size = s_nodes[idx].size;
    if (size == 0) return 0; 

    fwrite(s_nodes[idx].data, 1, size, stdout);
    if (s_nodes[idx].data[size - 1] != '\n') putchar('\n');
    return 0;
}


static void rm_node(int idx) {
    for (int i = 1; i < VFS_MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].parent == (int16_t)idx && i != idx)
            rm_node(i);
    memset(&s_nodes[idx], 0, sizeof(vfs_node_t));
}

int vfs_rm(const char *path, bool recursive) {
    int idx = vfs_resolve(path);
    if (idx < 0)       { printf("rm: '%s': not found\n", path);     return -1; }
    if (idx == 0)      { printf("rm: cannot remove root\n");          return -1; }
    if (idx == s_cwd)  { printf("rm: cannot remove current dir\n");   return -1; }

    if (s_nodes[idx].type == VFS_DIR) {
        bool has_children = false;
        for (int i = 1; i < VFS_MAX_NODES; i++)
            if (s_nodes[i].used && s_nodes[i].parent == (int16_t)idx)
                { has_children = true; break; }
        if (has_children && !recursive) {
            printf("rm: '%s': directory not empty  (use rm -r)\n", path);
            return -1;
        }
    }
    rm_node(idx);
    return 0;
}


int vfs_stat(const char *path) {
    int idx = vfs_resolve(path);
    if (idx < 0) { printf("stat: '%s': not found\n", path); return -1; }

    char fp[VFS_PATH_LEN];
    node_path(idx, fp, sizeof(fp));
    uint32_t uptime = now_ms();

    printf("  path     : %s\n",     fp);
    printf("  type     : %s\n",     s_nodes[idx].type == VFS_DIR ? "directory" : "file");
    printf("  size     : %lu B\n",  s_nodes[idx].size);
    printf("  created  : T+%lu ms\n", s_nodes[idx].created_ms);
    printf("  modified : T+%lu ms\n", s_nodes[idx].modified_ms);
    printf("  age      : %lu ms\n",  uptime - s_nodes[idx].created_ms);
    return 0;
}

int vfs_hexdump(const char *path) {
    int idx = vfs_resolve(path);
    if (idx < 0)                       { printf("hexdump: '%s': not found\n", path);       return -1; }
    if (s_nodes[idx].type != VFS_FILE) { printf("hexdump: '%s': is a directory\n", path); return -1; }

    uint32_t size = s_nodes[idx].size;
    if (size == 0) { printf("(empty file)\n"); return 0; }

    printf("hexdump '%s'  %lu byte(s):\n", s_nodes[idx].name, size);
    for (uint32_t i = 0; i < size; i++) {
        if (i % 16 == 0) printf("  %04lX: ", i);
        printf("%02X ", s_nodes[idx].data[i]);
        if (i % 16 == 15 || i == size - 1) {
            for (uint32_t j = (i % 16) + 1; j < 16; j++) printf("   ");
            printf(" |");
            for (uint32_t j = (i / 16) * 16; j <= i; j++) {
                uint8_t c = s_nodes[idx].data[j];
                putchar((c >= 32 && c < 127) ? (char)c : '.');
            }
            printf("|\n");
        }
    }
    return 0;
}


int vfs_cp(const char *src, const char *dst) {
    int si = vfs_resolve(src);
    if (si < 0)                       { printf("cp: '%s': not found\n", src);       return -1; }
    if (s_nodes[si].type != VFS_FILE) { printf("cp: '%s': is a directory\n", src); return -1; }

    /* if destination is an existing dir, copy into it with the same filename */
    int di = vfs_resolve(dst);
    if (di >= 0 && s_nodes[di].type == VFS_DIR) {
        char newpath[VFS_PATH_LEN];
        char dp[VFS_PATH_LEN];
        node_path(di, dp, sizeof(dp));
        snprintf(newpath, sizeof(newpath), "%s/%s", dp, s_nodes[si].name);
        return vfs_cp(src, newpath);
    }

    int ret = vfs_write(dst, s_nodes[si].data, s_nodes[si].size, false);
    if (ret < 0) return -1;
    printf("cp: '%s' -> '%s'  (%lu B)\n", src, dst, s_nodes[si].size);
    return 0;
}

int vfs_mv(const char *src, const char *dst) {
    int si = vfs_resolve(src);
    if (si < 0)  { printf("mv: '%s': not found\n", src); return -1; }
    if (si == 0) { printf("mv: cannot move root\n");      return -1; }

    int16_t new_parent;
    char    new_name[VFS_NAME_LEN];

    int di = vfs_resolve(dst);
    if (di >= 0 && s_nodes[di].type == VFS_DIR) {
        new_parent = (int16_t)di;
        strncpy(new_name, s_nodes[si].name, VFS_NAME_LEN - 1);
        new_name[VFS_NAME_LEN - 1] = '\0';
    } else {
        int p = vfs_resolve_parent(dst, new_name, sizeof(new_name));
        if (p < 0) { printf("mv: bad destination path\n"); return -1; }
        new_parent = (int16_t)p;
    }

    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (s_nodes[i].used && i != si &&
            s_nodes[i].parent == new_parent &&
            strcmp(s_nodes[i].name, new_name) == 0) {
            printf("mv: '%s' already exists at destination\n", new_name);
            return -1;
        }
    }

    s_nodes[si].parent      = new_parent;
    strncpy(s_nodes[si].name, new_name, VFS_NAME_LEN - 1);
    s_nodes[si].modified_ms = now_ms();
    printf("mv: '%s' -> '%s'\n", src, dst);
    return 0;
}

int vfs_wc(const char *path) {
    int idx = vfs_resolve(path);
    if (idx < 0)                       { printf("wc: '%s': not found\n", path);       return -1; }
    if (s_nodes[idx].type != VFS_FILE) { printf("wc: '%s': is a directory\n", path); return -1; }

    uint32_t lines = 0, words = 0, bytes = s_nodes[idx].size;
    bool in_word = false;
    for (uint32_t i = 0; i < bytes; i++) {
        uint8_t c = s_nodes[idx].data[i];
        if (c == '\n') { lines++;  in_word = false; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { in_word = false; continue; }
        if (!in_word) { words++;  in_word = true; }
    }
    printf("  %4lu lines  %4lu words  %4lu bytes  %s\n",
           lines, words, bytes, s_nodes[idx].name);
    return 0;
}

int vfs_grep(const char *path, const char *pattern) {
    int idx = vfs_resolve(path);
    if (idx < 0)                       { printf("grep: '%s': not found\n", path);       return -1; }
    if (s_nodes[idx].type != VFS_FILE) { printf("grep: '%s': is a directory\n", path); return -1; }

    uint32_t size    = s_nodes[idx].size;
    int      matches = 0;
    uint32_t ls      = 0;
    uint32_t ln      = 1;

    for (uint32_t i = 0; i <= size; i++) {
        if (i == size || s_nodes[idx].data[i] == '\n') {
            uint32_t ll = i - ls;
            if (ll) {
                char line[VFS_MAX_FILE_SIZE + 1];
                if (ll > VFS_MAX_FILE_SIZE) ll = VFS_MAX_FILE_SIZE;
                memcpy(line, s_nodes[idx].data + ls, ll);
                line[ll] = '\0';
                if (strstr(line, pattern)) {
                    printf("%4lu: %s\n", ln, line);
                    matches++;
                }
            }
            ls = i + 1;
            ln++;
        }
    }
    if (!matches) printf("grep: no matches for '%s' in '%s'\n", pattern, path);
    else          printf("(%d match%s)\n", matches, matches == 1 ? "" : "es");
    return matches;
}

static void find_recursive(int dir, const char *pattern) {
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (!s_nodes[i].used || s_nodes[i].parent != (int16_t)dir) continue;
        if (strstr(s_nodes[i].name, pattern)) {
            char fp[VFS_PATH_LEN];
            node_path(i, fp, sizeof(fp));
            printf("  %s%s\n", fp, s_nodes[i].type == VFS_DIR ? "/" : "");
        }
        if (s_nodes[i].type == VFS_DIR) find_recursive(i, pattern);
    }
}

void vfs_find_all(const char *name) {
    printf("find '%s':\n", name);
    find_recursive(0, name);
}

void vfs_df(void) {
    int      used_nodes = 0;
    uint32_t data_bytes = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (s_nodes[i].used) { used_nodes++; data_bytes += s_nodes[i].size; }
    }
    uint32_t data_cap    = (uint32_t)(VFS_MAX_NODES - 1) * VFS_MAX_FILE_SIZE;
    uint32_t struct_ram  = (uint32_t)sizeof(s_nodes);

    printf("  nodes    : %2d / %d used\n",              used_nodes, VFS_MAX_NODES);
    printf("  data     : %lu / %lu B used\n",           data_bytes, data_cap);
    printf("  data free: %lu B  (%lu KB)\n",
           data_cap - data_bytes, (data_cap - data_bytes) / 1024);
    printf("  ram total: %lu B  (%lu KB) for node table\n",
           struct_ram, struct_ram / 1024);
}

const char *vfs_cwd_path(void) {
    static char buf[VFS_PATH_LEN];
    node_path(s_cwd, buf, sizeof(buf));
    return buf;
}