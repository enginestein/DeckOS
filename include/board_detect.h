#pragma once
#include <stdint.h>

typedef struct {
    const char* name;    
    uint32_t    flash_kb;   
    uint32_t    sram_kb;   
    uint32_t    cpu_mhz;   
} board_info_t;

board_info_t board_detect(void);