#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_DRIVERS 16

typedef enum {
    DRIVER_UNLOADED = 0,
    DRIVER_OK,
    DRIVER_ERROR,
} driver_status_t;

typedef struct {
    const char*      name;
    driver_status_t  status;
    int (*init)(void);
    void (*deinit)(void);
} driver_t;

void            drivers_init_all(void);
void            drivers_list(void);
driver_status_t driver_status(const char* name);
int             driver_register(const char* name,
                                int  (*init)(void),
                                void (*deinit)(void));