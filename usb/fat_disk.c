#include "fat_disk.h"

#include <string.h>

#include <stdio.h>

#ifdef FATDISK_HOST_TEST
#define DISK_LOCK() (0u)
#define DISK_UNLOCK(x) ((void)(x))
#else
#include "hardware/sync.h"

#define DISK_LOCK() save_and_disable_interrupts()
#define DISK_UNLOCK(x) restore_interrupts(x)
#endif

#define BPS FATDISK_BLOCK_SIZE
#define TOTAL_SECTORS FATDISK_BLOCK_NUM
#define SEC_PER_CLUS 1u
#define RESERVED_SEC 1u
#define NUM_FATS 2u
#define FAT_SECTORS 1u
#define ROOT_ENTRIES 16u
#define ROOT_SECTORS ((ROOT_ENTRIES * 32u + BPS - 1u) / BPS)

#define FAT1_SECTOR (RESERVED_SEC)
#define FAT2_SECTOR (FAT1_SECTOR + FAT_SECTORS)
#define ROOT_SECTOR (FAT2_SECTOR + FAT_SECTORS)
#define DATA_SECTOR (ROOT_SECTOR + ROOT_SECTORS)
#define DATA_CLUSTERS (TOTAL_SECTORS - DATA_SECTOR)
#define FIRST_CLUSTER 2u
#define LAST_CLUSTER (FIRST_CLUSTER + DATA_CLUSTERS - 1u)
#define CLUSTER_EOC 0xFFFu

#define DIR_ENTRY_SIZE 32u

static uint8_t s_disk[TOTAL_SECTORS][BPS];

static inline void put16(uint8_t * p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}
static inline void put32(uint8_t * p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}
static inline uint16_t get16(const uint8_t * p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t get32(const uint8_t * p) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint8_t * root_dir(void) {
  return s_disk[ROOT_SECTOR];
}
static uint8_t * fat1(void) {
  return s_disk[FAT1_SECTOR];
}
static uint8_t * fat2(void) {
  return s_disk[FAT2_SECTOR];
}
static uint8_t * cluster_ptr(uint32_t clus) {
  return s_disk[DATA_SECTOR + (clus - FIRST_CLUSTER)];
}

static uint32_t fat_get(uint32_t n) {
  const uint8_t * f = fat1();
  uint32_t off = n + (n / 2);
  uint16_t pair = (uint16_t)(f[off] | (f[off + 1] << 8));
  return (n & 1) ? (pair >> 4) : (pair & 0x0FFF);
}

static void fat_set_one(uint8_t * f, uint32_t n, uint32_t val) {
  uint32_t off = n + (n / 2);
  val &= 0x0FFF;
  if (n & 1) {
    f[off] = (uint8_t)((f[off] & 0x0F) | ((val << 4) & 0xF0));
    f[off + 1] = (uint8_t)((val >> 4) & 0xFF);
  } else {
    f[off] = (uint8_t)(val & 0xFF);
    f[off + 1] = (uint8_t)((f[off + 1] & 0xF0) | ((val >> 8) & 0x0F));
  }
}

static void fat_set(uint32_t n, uint32_t val) {
  fat_set_one(fat1(), n, val);
  fat_set_one(fat2(), n, val);
}

static uint32_t fat_find_free(void) {
  for (uint32_t c = FIRST_CLUSTER; c <= LAST_CLUSTER; c++)
    if (fat_get(c) == 0) return c;
  return 0;
}

static void to_83(const char * in, char out[11]) {
  memset(out, ' ', 11);

  const char * slash = strrchr(in, '/');
  if (slash) in = slash + 1;

  const char * dot = strrchr(in, '.');
  int base_len = dot ? (int)(dot - in) : (int) strlen(in);
  int i;
  for (i = 0; i < 8 && i < base_len; i++) {
    char c = in [i];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c == ' ') c = '_';
    out[i] = c;
  }
  if (dot) {
    const char * ext = dot + 1;
    for (i = 0; i < 3 && ext[i]; i++) {
      char c = ext[i];
      if (c >= 'a' && c <= 'z') c -= 32;
      out[8 + i] = c;
    }
  }
}

static void from_83(const uint8_t raw[11], char out[13]) {
  int o = 0;
  for (int i = 0; i < 8; i++) {
    if (raw[i] == ' ') break;
    out[o++] = (char) raw[i];
  }
  if (raw[8] != ' ') {
    out[o++] = '.';
    for (int i = 8; i < 11; i++) {
      if (raw[i] == ' ') break;
      out[o++] = (char) raw[i];
    }
  }
  out[o] = '\0';
}

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LFN 0x0F

static bool entry_is_file(const uint8_t * e) {
  if (e[0] == 0x00 || e[0] == 0xE5) return false;
  uint8_t attr = e[11];
  if (attr == ATTR_LFN) return false;
  if (attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY)) return false;
  return true;
}

static uint8_t * find_entry_83(const char raw[11]) {
  uint8_t * dir = root_dir();
  for (uint32_t i = 0; i < ROOT_ENTRIES; i++) {
    uint8_t * e = dir + i * DIR_ENTRY_SIZE;
    if (e[0] == 0x00) break;
    if (!entry_is_file(e)) continue;
    if (memcmp(e, raw, 11) == 0) return e;
  }
  return NULL;
}

static uint8_t * find_free_entry(void) {
  uint8_t * dir = root_dir();
  for (uint32_t i = 0; i < ROOT_ENTRIES; i++) {
    uint8_t * e = dir + i * DIR_ENTRY_SIZE;
    if (e[0] == 0x00 || e[0] == 0xE5) return e;
  }
  return NULL;
}

static void free_chain(uint32_t clus) {
  while (clus >= FIRST_CLUSTER && clus <= LAST_CLUSTER) {
    uint32_t next = fat_get(clus);
    fat_set(clus, 0);
    if (next < FIRST_CLUSTER || next > LAST_CLUSTER) break;
    clus = next;
  }
}

static void write_boot_sector(void) {
  uint8_t * b = s_disk[0];
  memset(b, 0, BPS);

  b[0] = 0xEB;
  b[1] = 0x3C;
  b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  put16(b + 11, BPS);
  b[13] = SEC_PER_CLUS;
  put16(b + 14, RESERVED_SEC);
  b[16] = NUM_FATS;
  put16(b + 17, ROOT_ENTRIES);
  put16(b + 19, TOTAL_SECTORS);
  b[21] = 0xF8;
  put16(b + 22, FAT_SECTORS);
  put16(b + 24, 1);
  put16(b + 26, 1);
  put32(b + 28, 0);
  put32(b + 32, 0);

  b[36] = 0x80;
  b[38] = 0x29;
  put32(b + 39, 0xDEC05005);
  memcpy(b + 43, "DECKOS     ", 11);
  memcpy(b + 54, "FAT12   ", 8);

  b[510] = 0x55;
  b[511] = 0xAA;
}

static void init_fats(void) {
  memset(fat1(), 0, FAT_SECTORS * BPS);
  memset(fat2(), 0, FAT_SECTORS * BPS);

  fat_set(0, 0xFF8);
  fat_set(1, 0xFFF);
}

void fat_disk_format(void) {
  uint32_t irq = DISK_LOCK();
  memset(s_disk, 0, sizeof(s_disk));
  write_boot_sector();
  init_fats();
  memset(root_dir(), 0, ROOT_SECTORS * BPS);

  uint8_t * vol = root_dir();
  memcpy(vol, "DECKOS     ", 11);
  vol[11] = ATTR_VOLUME_ID;
  put16(vol + 16, 0x4A21);
  put16(vol + 24, 0x4A21);
  DISK_UNLOCK(irq);

  static
  const char readme[] =
    "DeckOS Portable USB disk.\r\n"
  "Drop .ds scripts here, then run `usb import <NAME>` in the shell.\r\n"
  "Use `usb export <vfspath>` to copy files from DeckOS onto this disk.\r\n";
  fat_disk_add_file("README.TXT", (const uint8_t * ) readme, (uint32_t)(sizeof(readme) - 1));
}

static bool s_inited = false;
void fat_disk_init(void) {
  if (s_inited) return;
  s_inited = true;
  fat_disk_format();
}

uint32_t fat_disk_block_size(void) {
  return BPS;
}
uint32_t fat_disk_block_count(void) {
  return TOTAL_SECTORS;
}

int32_t fat_disk_read(uint32_t lba, uint32_t offset, void * buf, uint32_t bufsize) {
  if (lba >= TOTAL_SECTORS) return -1;
  if (offset + bufsize > BPS) return -1;
  memcpy(buf, s_disk[lba] + offset, bufsize);
  return (int32_t) bufsize;
}

int32_t fat_disk_write(uint32_t lba, uint32_t offset,
  const uint8_t * buf, uint32_t bufsize) {
  if (lba >= TOTAL_SECTORS) return -1;
  if (offset + bufsize > BPS) return -1;
  memcpy(s_disk[lba] + offset, buf, bufsize);
  return (int32_t) bufsize;
}

int fat_disk_add_file(const char * name,
  const uint8_t * data, uint32_t len) {
  char raw[11];
  to_83(name, raw);

  uint32_t need = (len + BPS - 1) / BPS;

  uint32_t irq = DISK_LOCK();

  uint8_t * e = find_entry_83(raw);
  if (e) {
    uint32_t old = get16(e + 26);
    if (old >= FIRST_CLUSTER) free_chain(old);
    memset(e, 0, DIR_ENTRY_SIZE);
    e[0] = 0xE5;
  } else {
    e = find_free_entry();
  }
  if (!e) {
    DISK_UNLOCK(irq);
    return -1;
  }

  uint32_t first = 0, prev = 0, remaining = len;
  const uint8_t * src = data;
  for (uint32_t i = 0; i < need; i++) {
    uint32_t c = fat_find_free();
    if (c == 0) {
      if (first) free_chain(first);
      DISK_UNLOCK(irq);
      return -2;
    }
    fat_set(c, CLUSTER_EOC);
    if (prev) fat_set(prev, c);
    else first = c;

    uint32_t chunk = remaining < BPS ? remaining : BPS;
    uint8_t * dst = cluster_ptr(c);
    memset(dst, 0, BPS);
    if (chunk) memcpy(dst, src, chunk);
    src += chunk;
    remaining -= chunk;
    prev = c;
  }

  memset(e, 0, DIR_ENTRY_SIZE);
  memcpy(e, raw, 11);
  e[11] = 0x20;
  put16(e + 14, 0);
  put16(e + 16, 0x4A21);
  put16(e + 22, 0);
  put16(e + 24, 0x4A21);
  put16(e + 26, (uint16_t) first);
  put32(e + 28, len);

  DISK_UNLOCK(irq);
  return 0;
}

int fat_disk_delete_file(const char * name) {
  char raw[11];
  to_83(name, raw);
  uint32_t irq = DISK_LOCK();
  uint8_t * e = find_entry_83(raw);
  if (!e) {
    DISK_UNLOCK(irq);
    return -1;
  }
  uint32_t first = get16(e + 26);
  if (first >= FIRST_CLUSTER) free_chain(first);
  memset(e, 0, DIR_ENTRY_SIZE);
  e[0] = 0xE5;
  DISK_UNLOCK(irq);
  return 0;
}

int fat_disk_read_file(const char * name, uint8_t * buf, uint32_t buflen, uint32_t * out_len) {
  char raw[11];
  to_83(name, raw);

  uint32_t irq = DISK_LOCK();

  uint8_t * e = find_entry_83(raw);
  if (!e) {
    DISK_UNLOCK(irq);
    return -1;
  }

  uint32_t size = get32(e + 28);
  uint32_t clus = get16(e + 26);
  uint32_t copied = 0;

  while (copied < size && clus >= FIRST_CLUSTER && clus <= LAST_CLUSTER) {
    uint32_t chunk = size - copied;
    if (chunk > BPS) chunk = BPS;
    if (copied + chunk > buflen) chunk = (copied < buflen) ? (buflen - copied) : 0;
    if (chunk) memcpy(buf + copied, cluster_ptr(clus), chunk);
    copied += chunk;
    if (copied >= buflen) break;
    clus = fat_get(clus);
  }

  DISK_UNLOCK(irq);
  if (out_len) * out_len = copied;
  return 0;
}

int fat_disk_count(void) {
  int n = 0;
  uint32_t irq = DISK_LOCK();
  uint8_t * dir = root_dir();
  for (uint32_t i = 0; i < ROOT_ENTRIES; i++) {
    uint8_t * e = dir + i * DIR_ENTRY_SIZE;
    if (e[0] == 0x00) break;
    if (entry_is_file(e)) n++;
  }
  DISK_UNLOCK(irq);
  return n;
}

int fat_disk_entry(int index, char name_out[13], uint32_t * size_out) {
  int n = 0;
  uint32_t irq = DISK_LOCK();
  uint8_t * dir = root_dir();
  for (uint32_t i = 0; i < ROOT_ENTRIES; i++) {
    uint8_t * e = dir + i * DIR_ENTRY_SIZE;
    if (e[0] == 0x00) break;
    if (!entry_is_file(e)) continue;
    if (n == index) {
      from_83(e, name_out);
      if (size_out) * size_out = get32(e + 28);
      DISK_UNLOCK(irq);
      return 0;
    }
    n++;
  }
  DISK_UNLOCK(irq);
  return -1;
}

uint32_t fat_disk_bytes_used(void) {
  uint32_t used = 0;
  uint32_t irq = DISK_LOCK();
  for (uint32_t c = FIRST_CLUSTER; c <= LAST_CLUSTER; c++)
    if (fat_get(c) != 0) used += BPS;
  DISK_UNLOCK(irq);
  return used;
}