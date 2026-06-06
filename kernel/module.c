#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "module.h"
#include "editor.h"


#define MAX_PLUGINS 16
static module_t s_plugins[MAX_PLUGINS];
static int       s_plugin_count = 0;


static void (*s_reg_cmd)(const char *, const char *, void (*)(int, char **)) = NULL;
static void (*s_unreg_cmd)(const char *) = NULL;

void module_set_cmd_api(
    void (*reg)(const char *, const char *, void (*)(int, char **)),
    void (*unreg)(const char *))
{
    s_reg_cmd = reg;
    s_unreg_cmd = unreg;
}


static module_t s_modules[] = {
    {
        .name        = "editor",
        .description = "nano-style text editor (edit command)",
        .ram_bytes   = EDITOR_MODULE_RAM_BYTES,
        .load        = editor_module_load,
        .unload      = editor_module_unload,
        .loaded      = false,
        .version     = "1.0.0",
        .commands    = NULL,
        .command_count = 0,
        .on_event    = NULL,
        .is_builtin  = true,
    },
};

static const int s_module_count = (int)(sizeof(s_modules) / sizeof(s_modules[0]));


void modules_init(void) {
    for (int i = 0; i < s_module_count; i++) s_modules[i].loaded = false;
    s_plugin_count = 0;

   
    extern plugin_api_t MOD_EXAMPLE;
    module_t plugin;
    plugin.name        = "plugin-example";
    plugin.description = "Example community plugin -- copy this to write your own";
    plugin.ram_bytes   = 1024;
    plugin.load        = MOD_EXAMPLE.init;
    plugin.unload      = MOD_EXAMPLE.deinit;
    plugin.loaded      = false;
    plugin.version     = "1.0.0";
    plugin.commands    = MOD_EXAMPLE.commands;
    plugin.command_count = MOD_EXAMPLE.command_count;
    plugin.on_event    = MOD_EXAMPLE.on_event;
    plugin.is_builtin  = true;
    module_register_plugin(&plugin);

   
    extern plugin_api_t MOD_WIFI;
    plugin.name = "wifi";
    plugin.description = "ESP8266 WiFi control (init/status/ping/scan/join/ip/shell/deinit/bridge/serve/telnet/get/post)";
    plugin.ram_bytes = 8192;
    plugin.load = MOD_WIFI.init;
    plugin.unload = MOD_WIFI.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_WIFI.commands;
    plugin.command_count = MOD_WIFI.command_count;
    plugin.on_event = MOD_WIFI.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_BT;
    plugin.name = "bt";
    plugin.description = "HC-05 Bluetooth control (shell/log/exec/top/send/recv/sniff/at/name/pin/baud/status/init)";
    plugin.ram_bytes = 4096;
    plugin.load = MOD_BT.init;
    plugin.unload = MOD_BT.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_BT.commands;
    plugin.command_count = MOD_BT.command_count;
    plugin.on_event = MOD_BT.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_SERVO;
    plugin.name = "servo";
    plugin.description = "Servo control (pin/sweep/bg/stop)";
    plugin.ram_bytes = 2048;
    plugin.load = MOD_SERVO.init;
    plugin.unload = MOD_SERVO.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_SERVO.commands;
    plugin.command_count = MOD_SERVO.command_count;
    plugin.on_event = MOD_SERVO.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_OLED;
    plugin.name = "oled";
    plugin.description = "SSD1306 OLED display (init/on/off/clear/text/line/rect)";
    plugin.ram_bytes = 16384;
    plugin.load = MOD_OLED.init;
    plugin.unload = MOD_OLED.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_OLED.commands;
    plugin.command_count = MOD_OLED.command_count;
    plugin.on_event = MOD_OLED.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_SWARM;
    plugin.name = "swarm";
    plugin.description = "ESP-NOW mesh network (init/id/mac/peer/pub/list/stop)";
    plugin.ram_bytes = 6144;
    plugin.load = MOD_SWARM.init;
    plugin.unload = MOD_SWARM.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_SWARM.commands;
    plugin.command_count = MOD_SWARM.command_count;
    plugin.on_event = MOD_SWARM.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_MQTT;
    plugin.name = "mqtt";
    plugin.description = "MQTT client (pub/sub)";
    plugin.ram_bytes = 4096;
    plugin.load = MOD_MQTT.init;
    plugin.unload = MOD_MQTT.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_MQTT.commands;
    plugin.command_count = MOD_MQTT.command_count;
    plugin.on_event = MOD_MQTT.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_IMU;
    plugin.name = "imu";
    plugin.description = "MPU6050 accelerometer/gyro (read/stream/attitude)";
    plugin.ram_bytes = 4096;
    plugin.load = MOD_IMU.init;
    plugin.unload = MOD_IMU.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_IMU.commands;
    plugin.command_count = MOD_IMU.command_count;
    plugin.on_event = MOD_IMU.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_TONE;
    plugin.name = "tone";
    plugin.description = "Audio tone generation (tone/melody/piano)";
    plugin.ram_bytes = 1024;
    plugin.load = MOD_TONE.init;
    plugin.unload = MOD_TONE.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_TONE.commands;
    plugin.command_count = MOD_TONE.command_count;
    plugin.on_event = MOD_TONE.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);

    extern plugin_api_t MOD_MORSE;
    plugin.name = "morse";
    plugin.description = "Morse code signalling";
    plugin.ram_bytes = 1024;
    plugin.load = MOD_MORSE.init;
    plugin.unload = MOD_MORSE.deinit;
    plugin.loaded = false;
    plugin.version = "1.0.0";
    plugin.commands = MOD_MORSE.commands;
    plugin.command_count = MOD_MORSE.command_count;
    plugin.on_event = MOD_MORSE.on_event;
    plugin.is_builtin = true;
    module_register_plugin(&plugin);
}


int module_count(void) { return s_module_count; }

const module_t *module_get(int index) {
    if (index < 0 || index >= s_module_count) return NULL;
    return &s_modules[index];
}

int module_total_count(void) { return s_module_count + s_plugin_count; }

const module_t *module_total_get(int index) {
    if (index < 0) return NULL;
    if (index < s_module_count) return &s_modules[index];
    int pi = index - s_module_count;
    if (pi < s_plugin_count) return &s_plugins[pi];
    return NULL;
}

static module_t *find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_module_count; i++)
        if (strcmp(s_modules[i].name, name) == 0) return &s_modules[i];
    for (int i = 0; i < s_plugin_count; i++)
        if (strcmp(s_plugins[i].name, name) == 0) return &s_plugins[i];
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
        printf("module: failed to load '%s'\n", m->name);
        m->loaded = false;
        return false;
    }

   
    if (s_reg_cmd && m->commands) {
        for (int i = 0; i < m->command_count; i++)
            s_reg_cmd(m->commands[i].name, m->commands[i].description, m->commands[i].handler);
    }

    m->loaded = true;
    printf("module: loaded '%s'  (~%lu KB)\n",
           m->name, (unsigned long)(m->ram_bytes / 1024));

   
    if (m->on_event) m->on_event(MODULE_EVENT_BOOT_COMPLETE, (void*)m);

    return true;
}

bool module_unload(const char *name) {
    module_t *m = find(name);
    if (!m) { printf("module: no such module '%s'\n", name ? name : ""); return false; }
    if (!m->loaded) { printf("module: '%s' is not loaded\n", m->name); return false; }

   
    if (s_unreg_cmd && m->commands) {
        for (int i = 0; i < m->command_count; i++)
            s_unreg_cmd(m->commands[i].name);
    }

    if (m->unload) m->unload();
    m->loaded = false;
    printf("module: unloaded '%s'  (freed ~%lu KB)\n",
           m->name, (unsigned long)(m->ram_bytes / 1024));
    return true;
}


bool module_register_plugin(const module_t *plugin) {
    if (!plugin || !plugin->name) return false;
    if (s_plugin_count >= MAX_PLUGINS) {
        printf("module: max plugins (%d) reached\n", MAX_PLUGINS);
        return false;
    }
    if (find(plugin->name)) {
        printf("module: plugin '%s' already registered\n", plugin->name);
        return false;
    }
    s_plugins[s_plugin_count] = *plugin;
    s_plugins[s_plugin_count].loaded = false;
    s_plugin_count++;
    printf("module: plugin '%s' v%s registered\n", plugin->name,
           plugin->version ? plugin->version : "?");
    return true;
}

bool module_unregister_plugin(const char *name) {
    if (!name) return false;
    for (int i = 0; i < s_plugin_count; i++) {
        if (strcmp(s_plugins[i].name, name) == 0) {
            if (s_plugins[i].loaded) module_unload(name);
            s_plugins[i] = s_plugins[--s_plugin_count];
            printf("module: plugin '%s' unregistered\n", name);
            return true;
        }
    }
    return false;
}


void module_fire_event(module_event_t event, void *data) {
    for (int i = 0; i < s_module_count; i++) {
        if (s_modules[i].loaded && s_modules[i].on_event)
            s_modules[i].on_event(event, data);
    }
    for (int i = 0; i < s_plugin_count; i++) {
        if (s_plugins[i].loaded && s_plugins[i].on_event)
            s_plugins[i].on_event(event, data);
    }
}

int module_loaded_count(void) {
    int n = 0;
    for (int i = 0; i < s_module_count; i++)
        if (s_modules[i].loaded) n++;
    for (int i = 0; i < s_plugin_count; i++)
        if (s_plugins[i].loaded) n++;
    return n;
}

const module_t *module_get_loaded(int index) {
    int n = 0;
    for (int i = 0; i < s_module_count; i++) {
        if (s_modules[i].loaded) {
            if (n == index) return &s_modules[i];
            n++;
        }
    }
    for (int i = 0; i < s_plugin_count; i++) {
        if (s_plugins[i].loaded) {
            if (n == index) return &s_plugins[i];
            n++;
        }
    }
    return NULL;
}
