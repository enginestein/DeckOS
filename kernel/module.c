/*
 * DeckOS loadable module registry
 */
#include <stdio.h>
#include <string.h>
#include "module.h"
#include "editor.h"

static module_t s_modules[] = {
    {
        .name        = "editor",
        .description = "nano-style text editor (edit command)",
        .ram_bytes   = EDITOR_MODULE_RAM_BYTES,
        .load        = editor_module_load,
        .unload      = editor_module_unload,
        .loaded      = false,
    },
};

static const int s_module_count = (int)(sizeof(s_modules) / sizeof(s_modules[0]));

void modules_init(void) {
    for (int i = 0; i < s_module_count; i++) s_modules[i].loaded = false;
}

int module_count(void) { return s_module_count; }

const module_t *module_get(int index) {
    if (index < 0 || index >= s_module_count) return NULL;
    return &s_modules[index];
}

static module_t *find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_module_count; i++)
        if (strcmp(s_modules[i].name, name) == 0) return &s_modules[i];
    return NULL;
}

const module_t *module_find(const char *name) { return find(name); }

bool module_is_loaded(const char *name) {
    const module_t *m = find(name);
    return m && m->loaded;
}

bool module_load(const char *name) {
    module_t *m = find(name);
    if (!m) { printf("module: no such module '%s'\n", name ? name : ""); return false; }
    if (m->loaded) { printf("module: '%s' already loaded\n", m->name); return true; }

    if (m->load && !m->load()) {
        printf("module: failed to load '%s' (out of memory?)\n", m->name);
        m->loaded = false;
        return false;
    }
    m->loaded = true;
    printf("module: loaded '%s'  (~%lu KB)\n",
           m->name, (unsigned long)(m->ram_bytes / 1024));
    return true;
}

bool module_unload(const char *name) {
    module_t *m = find(name);
    if (!m) { printf("module: no such module '%s'\n", name ? name : ""); return false; }
    if (!m->loaded) { printf("module: '%s' is not loaded\n", m->name); return false; }

    if (m->unload) m->unload();
    m->loaded = false;
    printf("module: unloaded '%s'  (freed ~%lu KB)\n",
           m->name, (unsigned long)(m->ram_bytes / 1024));
    return true;
}
