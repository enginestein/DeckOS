#pragma once


#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif



typedef enum {
    MODULE_EVENT_BOOT_COMPLETE,      
    MODULE_EVENT_TICK,               
    MODULE_EVENT_PRE_COMMAND,        
    MODULE_EVENT_POST_COMMAND,       
    MODULE_EVENT_PEER_DISCOVERED,    
    MODULE_EVENT_CUSTOM_START = 100, 
} module_event_t;

typedef void (*module_event_cb_t)(module_event_t event, void *data);



typedef struct {
    const char *name;                                      
    const char *description;                               
    void      (*handler)(int argc, char *argv[]);          
} module_cmd_t;



typedef struct {
    bool           (*init)(void);                          
    void           (*deinit)(void);                        
    const module_cmd_t *commands;                          
    int                 command_count;                     
    module_event_cb_t   on_event;                          
} plugin_api_t;



typedef struct {
    const char *name;                                      
    const char *description;                               
    uint32_t    ram_bytes;                                 
    bool      (*load)(void);                               
    void      (*unload)(void);                             
    bool        loaded;                                    

   
    const char       *version;                             
    const module_cmd_t *commands;                          
    int                 command_count;                     
    module_event_cb_t   on_event;                          
    bool                is_builtin;                        
} module_t;



void modules_init(void);

int             module_count(void);
const module_t *module_get(int index);
int             module_total_count(void);
const module_t *module_total_get(int index);
const module_t *module_find(const char *name);
bool            module_is_loaded(const char *name);

bool module_load(const char *name);
bool module_unload(const char *name);




bool module_register_plugin(const module_t *plugin);


bool module_unregister_plugin(const char *name);


void module_fire_event(module_event_t event, void *data);


int module_loaded_count(void);


const module_t *module_get_loaded(int index);


void module_set_cmd_api(
    void (*register_cmd)(const char *name, const char *desc,
                         void (*handler)(int, char **)),
    void (*unregister_cmd)(const char *name));

#ifdef __cplusplus
}
#endif
