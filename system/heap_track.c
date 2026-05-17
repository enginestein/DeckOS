#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "heap_track.h"

#define TRACK_SLOTS 64

typedef struct {
    void*       ptr;
    size_t      size;
    const char* tag;
    bool        live;
} alloc_slot_t;

static alloc_slot_t s_slots[TRACK_SLOTS];
static uint32_t s_alloc_count   = 0;
static uint32_t s_free_count    = 0;
static uint32_t s_current_bytes = 0;
static uint32_t s_peak_bytes    = 0;
static bool     s_inited        = false;

void heap_track_init(void) {
    memset(s_slots, 0, sizeof(s_slots));
    s_alloc_count   = 0;
    s_free_count    = 0;
    s_current_bytes = 0;
    s_peak_bytes    = 0;
    s_inited        = true;
}

void* heap_track_malloc(size_t size, const char* tag) {
    void* ptr = malloc(size);
    if (!ptr || !s_inited) return ptr;

    // Find a free slot
    for (int i = 0; i < TRACK_SLOTS; i++) {
        if (!s_slots[i].live) {
            s_slots[i].ptr  = ptr;
            s_slots[i].size = size;
            s_slots[i].tag  = tag ? tag : "?";
            s_slots[i].live = true;
            break;
        }
    }

    s_alloc_count++;
    s_current_bytes += size;
    if (s_current_bytes > s_peak_bytes)
        s_peak_bytes = s_current_bytes;

    return ptr;
}

void heap_track_free(void* ptr) {
    if (!ptr) return;
    if (s_inited) {
        for (int i = 0; i < TRACK_SLOTS; i++) {
            if (s_slots[i].live && s_slots[i].ptr == ptr) {
                s_current_bytes -= s_slots[i].size;
                s_slots[i].live  = false;
                s_slots[i].ptr   = NULL;
                s_free_count++;
                break;
            }
        }
    }
    free(ptr);
}

void heap_track_print(void) {
    // Estimate total heap from linker symbols
    extern char __StackLimit, __bss_end__;
    extern char __end__;
    uint32_t heap_total = (uint32_t)(&__StackLimit - &__end__);

    printf("=== heap tracker ===\n");
    printf("  total heap    : %lu KB  (%lu B)\n", heap_total / 1024, heap_total);
    printf("  current used  : %lu B\n", s_current_bytes);
    printf("  peak used     : %lu B\n", s_peak_bytes);
    printf("  alloc calls   : %lu\n",   s_alloc_count);
    printf("  free  calls   : %lu\n",   s_free_count);
    printf("  live allocs   : %lu\n",   s_alloc_count - s_free_count);

    // Fragmentation heuristic: count live slots and gaps
    int live  = 0;
    int slots = 0;
    for (int i = 0; i < TRACK_SLOTS; i++) {
        slots++;
        if (s_slots[i].live) live++;
    }
    printf("  tracked slots : %d / %d\n", live, TRACK_SLOTS);
    if (heap_total > 0) {
        uint32_t used_pct = s_peak_bytes * 100 / heap_total;
        printf("  peak usage    : %lu%%\n", used_pct);
    }

    if (live > 0) {
        printf("\n  LIVE ALLOCATIONS:\n");
        printf("  %-6s  %-8s  TAG\n", "BYTES", "PTR");
        for (int i = 0; i < TRACK_SLOTS; i++) {
            if (s_slots[i].live)
                printf("  %-6u  0x%08lX  %s\n",
                       (unsigned)s_slots[i].size,
                       (uint32_t)s_slots[i].ptr,
                       s_slots[i].tag);
        }
    }
    printf("====================\n");
}

uint32_t heap_track_alloc_count(void)   { return s_alloc_count; }
uint32_t heap_track_free_count(void)    { return s_free_count; }
uint32_t heap_track_peak_bytes(void)    { return s_peak_bytes; }
uint32_t heap_track_current_bytes(void) { return s_current_bytes; }