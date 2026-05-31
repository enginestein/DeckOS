#pragma once
/*
this is a solution to ram limits in rp2040 boards. Things can be turned
into modules, and then can be loaded or unloaded to save ram.
*/
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;          
    const char *description;   
    uint32_t    ram_bytes;    
    bool      (*load)(void);  
    void      (*unload)(void);
    bool        loaded;
} module_t;

void modules_init(void);

int             module_count(void);
const module_t *module_get(int index);
const module_t *module_find(const char *name);
bool            module_is_loaded(const char *name);

bool module_load(const char *name);
bool module_unload(const char *name);
