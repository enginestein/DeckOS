#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stdint.h>


#define ED_LINELEN  512
#define ED_MAXLINES 64


#define EDITOR_MODULE_RAM_BYTES  (3u * (uint32_t)ED_MAXLINES * (uint32_t)ED_LINELEN)


bool editor_module_load(void);
void editor_module_unload(void);
bool editor_is_loaded(void);


void editor_run(const char* vfs_path);

#endif
