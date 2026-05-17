#pragma once
#include <stdint.h>
#include <stddef.h>

// Wraps malloc/free with tracking.
// Include this header AFTER stdlib.h to override.
// Must call heap_track_init() once before use.

void  heap_track_init(void);
void* heap_track_malloc(size_t size, const char* tag);
void  heap_track_free(void* ptr);
void  heap_track_print(void);

uint32_t heap_track_alloc_count(void);
uint32_t heap_track_free_count(void);
uint32_t heap_track_peak_bytes(void);
uint32_t heap_track_current_bytes(void);