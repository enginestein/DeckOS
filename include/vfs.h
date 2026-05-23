#pragma once

#include <stdint.h>
#include <stdbool.h>

#define VFS_MAX_NODES      32  
#define VFS_NAME_LEN       32
#define VFS_MAX_FILE_SIZE  512
#define VFS_PATH_LEN       128

typedef enum { VFS_FILE = 0, VFS_DIR = 1 } vfs_type_t;

typedef struct {
    bool        used;
    vfs_type_t  type;
    char        name[VFS_NAME_LEN];
    int16_t     parent;        
    uint32_t    size;
    uint32_t    created_ms;
    uint32_t    modified_ms;
    uint8_t     data[VFS_MAX_FILE_SIZE];
} vfs_node_t;

extern vfs_node_t s_nodes[VFS_MAX_NODES];
extern int        s_cwd;

void    vfs_init(void);

int     vfs_resolve(const char *path);
int     vfs_resolve_parent(const char *path, char *out_name, int name_len);

int     vfs_mkdir(const char *path);
int     vfs_ls   (const char *path);  
bool    vfs_cd   (const char *path);
void    vfs_pwd  (void);
void    vfs_tree (void);

int     vfs_touch  (const char *path);
int     vfs_write  (const char *path, const uint8_t *data, uint32_t len, bool append);
int     vfs_read   (const char *path, uint8_t *buf, uint32_t buflen, uint32_t *out_len);
int     vfs_cat    (const char *path);          /* print text to stdout     */
int     vfs_rm     (const char *path, bool recursive);
int     vfs_stat   (const char *path);
int     vfs_hexdump(const char *path);
int     vfs_cp     (const char *src, const char *dst);
int     vfs_mv     (const char *src, const char *dst);
int     vfs_wc     (const char *path);
int     vfs_grep   (const char *path, const char *pattern);

void    vfs_find_all(const char *name);   
void    vfs_df      (void);          
void vfs_inject_tests(void);

const char *vfs_cwd_path(void);  