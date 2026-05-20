#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "syslog.h"
#include "bt.h"
#include "spinlock_util.h"

#define SYSLOG_SLOTS  64
#define SYSLOG_MSG_LEN 64
#define SYSLOG_TAG_LEN 12

typedef struct {
    uint32_t    timestamp_ms;
    log_level_t level;
    char        tag[SYSLOG_TAG_LEN];
    char        msg[SYSLOG_MSG_LEN];
} log_entry_t;

static log_entry_t  s_ring[SYSLOG_SLOTS];
static int          s_head  = 0;   // next write position
static int          s_count = 0;   // entries present (≤ SYSLOG_SLOTS)
static uint32_t     s_total = 0;   // total entries ever written

static const char* level_str(log_level_t l) {
    switch (l) {
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERR:   return "ERR";
        default:        return "???";
    }
}

static const char* level_color(log_level_t l) {
    switch (l) {
        case LOG_DEBUG: return "\033[90m";   // dark grey
        case LOG_INFO:  return "\033[0m";    // normal
        case LOG_WARN:  return "\033[33m";   // yellow
        case LOG_ERR:   return "\033[31m";   // red
        default:        return "\033[0m";
    }
}

void syslog_init(void) {
    memset(s_ring, 0, sizeof(s_ring));
    s_head  = 0;
    s_count = 0;
    s_total = 0;
    // Plain runtime message - avoids macro-in-string-literal issues
    char init_msg[32];
    snprintf(init_msg, sizeof(init_msg), "ring log ready (%d slots)", SYSLOG_SLOTS);
    syslog_write(LOG_INFO, "syslog", init_msg);
}

void syslog_write(log_level_t lvl, const char* tag, const char* msg) {
    uint32_t saved = syslog_lock();

    log_entry_t* e = &s_ring[s_head];
    e->timestamp_ms = to_ms_since_boot(get_absolute_time());
    e->level        = lvl;
    strncpy(e->tag, tag ? tag : "?", SYSLOG_TAG_LEN - 1);
    e->tag[SYSLOG_TAG_LEN - 1] = '\0';
    strncpy(e->msg, msg ? msg : "", SYSLOG_MSG_LEN - 1);
    e->msg[SYSLOG_MSG_LEN - 1] = '\0';
    s_head = (s_head + 1) % SYSLOG_SLOTS;
    if (s_count < SYSLOG_SLOTS) s_count++;
    s_total++;

    syslog_unlock(saved);

    if (bt_log_is_enabled()) {
        bt_log_mirror(level_str(lvl), tag, msg,
                      to_ms_since_boot(get_absolute_time()));
    }
}
void syslog_dump(log_level_t min_level, int tail) {
    uint32_t saved = syslog_lock();

    if (s_count == 0) {
        syslog_unlock(saved);
        printf("(log empty)\n");
        return;
    }

    int show  = (tail > 0 && tail < s_count) ? tail : s_count;
    int start = (s_head - show + SYSLOG_SLOTS * 2) % SYSLOG_SLOTS;

    log_entry_t snap[SYSLOG_SLOTS];
    for (int i = 0; i < show; i++)
        snap[i] = s_ring[(start + i) % SYSLOG_SLOTS];

    syslog_unlock(saved);

    int printed = 0;
    for (int i = 0; i < show; i++) {
        log_entry_t* e = &snap[i];
        if (e->level < min_level) continue;
        uint32_t s  = e->timestamp_ms / 1000;
        uint32_t ms = e->timestamp_ms % 1000;
        printf("%s[%4lu.%03lu] [%s] [%-10s] %s\033[0m\n",
               level_color(e->level), s, ms,
               level_str(e->level), e->tag, e->msg);
        printed++;
    }
    if (printed == 0) printf("(no entries at this level)\n");
}

void syslog_clear(void) {
    uint32_t saved = syslog_lock();
    uint32_t total_snap = s_total;
    memset(s_ring, 0, sizeof(s_ring));
    s_head  = 0;
    s_count = 0;
    syslog_unlock(saved);

    printf("syslog cleared  (%lu total entries discarded)\n", total_snap);
}

uint32_t syslog_total(void) { return s_total; }