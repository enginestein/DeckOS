

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "vfs.h"
#include "file_persist.h"



#define VFS_FLASH_MAGIC   0x56465302u  
#define FLASH_TOTAL_SIZE  (2u * 1024u * 1024u)


#define NODE_TABLE_BYTES  ((uint32_t)(sizeof(vfs_node_t) * VFS_MAX_NODES))
#define HDR_BYTES         ((uint32_t)sizeof(vfs_flash_hdr_t))
#define PAYLOAD_BYTES     (HDR_BYTES + NODE_TABLE_BYTES)
#define SECTORS_NEEDED    ((PAYLOAD_BYTES + FLASH_SECTOR_SIZE - 1u) \
                           / FLASH_SECTOR_SIZE)
#define STORE_SIZE        (SECTORS_NEEDED * FLASH_SECTOR_SIZE)
#define FLASH_TARGET_OFFSET  (FLASH_TOTAL_SIZE - STORE_SIZE)
#define FLASH_STORE_ADDR     (XIP_BASE + FLASH_TARGET_OFFSET)



typedef struct {
    uint32_t magic;
    uint32_t data_len;  
    uint32_t checksum;  
    uint32_t saved_cwd; 
} vfs_flash_hdr_t;



extern vfs_node_t s_nodes[];  
extern int        s_cwd;      


extern void vfs_init(void);
extern void vfs_init_bare(void);  



static uint32_t fnv1a(const void *data, size_t len)
{
    const uint8_t *p   = (const uint8_t *)data;
    uint32_t       hash = 2166136261u;
    while (len--) {
        hash ^= *p++;
        hash *= 16777619u;
    }
    return hash;
}



void vfs_save(void)
{
   
    extern char __flash_binary_end;
    uint32_t fw_end_offset = (uint32_t)&__flash_binary_end - XIP_BASE;
    if (fw_end_offset > FLASH_TARGET_OFFSET) {
        printf("[vfs] ERROR: firmware ends at 0x%05lX, VFS store starts at "
               "0x%05lX -- they overlap!\n",
               (unsigned long)fw_end_offset,
               (unsigned long)FLASH_TARGET_OFFSET);
        printf("[vfs] Reduce VFS_MAX_NODES or VFS_MAX_FILE_SIZE.\n");
        return;
    }

   
    vfs_flash_hdr_t hdr = {
        .magic      = VFS_FLASH_MAGIC,
        .data_len   = NODE_TABLE_BYTES,
        .checksum   = fnv1a(s_nodes, NODE_TABLE_BYTES),
        .saved_cwd  = (uint32_t)s_cwd,
    };

   
    uint8_t *write_buf = (uint8_t *)malloc(STORE_SIZE);
    if (!write_buf) {
        printf("[vfs] save: out of memory for write buffer\n");
        return;
    }
    memset(write_buf, 0xFF, STORE_SIZE);
    memcpy(write_buf,               &hdr,     HDR_BYTES);
    memcpy(write_buf + HDR_BYTES,   s_nodes,  NODE_TABLE_BYTES);

   
    multicore_reset_core1();

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase  (FLASH_TARGET_OFFSET, STORE_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, write_buf, STORE_SIZE);
    restore_interrupts(irq);

    free(write_buf);

   
    extern void core1_restart(void);
    core1_restart();

    printf("[vfs] saved %lu B to flash @ offset 0x%05lX  (%lu sectors)\n",
           (unsigned long)NODE_TABLE_BYTES,
           (unsigned long)FLASH_TARGET_OFFSET,
           (unsigned long)SECTORS_NEEDED);
}

void vfs_load(void)
{
   
    extern char __flash_binary_end;
    uint32_t fw_end_offset = (uint32_t)&__flash_binary_end - XIP_BASE;
    if (fw_end_offset > FLASH_TARGET_OFFSET) {
        printf("[vfs] ERROR: firmware (0x%05lX) overlaps VFS store "
               "(0x%05lX) – fresh init\n",
               (unsigned long)fw_end_offset,
               (unsigned long)FLASH_TARGET_OFFSET);
        vfs_init();
        return;
    }

   
    const vfs_flash_hdr_t *hdr =
        (const vfs_flash_hdr_t *)FLASH_STORE_ADDR;

   
    if (hdr->magic != VFS_FLASH_MAGIC) {
        printf("[vfs] no valid save (magic=0x%08lX) – fresh init\n",
               (unsigned long)hdr->magic);
        vfs_init();
        return;
    }

   
    if (hdr->data_len != NODE_TABLE_BYTES) {
        printf("[vfs] node table size changed (%lu → %lu) – fresh init\n",
               (unsigned long)hdr->data_len,
               (unsigned long)NODE_TABLE_BYTES);
        vfs_init();
        return;
    }

   
    const uint8_t *data_ptr =
        (const uint8_t *)FLASH_STORE_ADDR + HDR_BYTES;

    uint32_t actual_csum = fnv1a(data_ptr, NODE_TABLE_BYTES);
    if (actual_csum != hdr->checksum) {
        printf("[vfs] checksum mismatch (got 0x%08lX, want 0x%08lX) – "
               "fresh init\n",
               (unsigned long)actual_csum,
               (unsigned long)hdr->checksum);
        vfs_init();
        return;
    }

   
    memcpy(s_nodes, data_ptr, NODE_TABLE_BYTES);

   
    if (hdr->saved_cwd < VFS_MAX_NODES && s_nodes[hdr->saved_cwd].used)
        s_cwd = (int)hdr->saved_cwd;
    else
        s_cwd = 0;

    printf("[vfs] restored %lu B from flash (cwd=%d)\n",
           (unsigned long)NODE_TABLE_BYTES, s_cwd);
}

