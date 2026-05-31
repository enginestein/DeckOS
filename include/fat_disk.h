#pragma once

#include <stdint.h>
#include <stdbool.h>

#define FATDISK_BLOCK_SIZE   512u
#define FATDISK_BLOCK_NUM    32u            


void     fat_disk_init(void);               
void     fat_disk_format(void);             


uint32_t fat_disk_block_size(void);
uint32_t fat_disk_block_count(void);
int32_t  fat_disk_read (uint32_t lba, uint32_t offset, void *buf, uint32_t bufsize);
int32_t  fat_disk_write(uint32_t lba, uint32_t offset, const uint8_t *buf, uint32_t bufsize);


int      fat_disk_add_file(const char *name, const uint8_t *data, uint32_t len);
int      fat_disk_delete_file(const char *name);
int      fat_disk_read_file(const char *name, uint8_t *buf, uint32_t buflen, uint32_t *out_len);
int      fat_disk_count(void);                                 
int      fat_disk_entry(int index, char name_out[13], uint32_t *size_out);
uint32_t fat_disk_bytes_used(void);
