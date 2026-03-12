#ifndef __LOG_H__
#define __LOG_H__

#include "segel.h"

/*
 * Server log with writer-priority reader-writer synchronization.
 *   GET requests  → writers (exclusive access, appends an entry)
 *   POST requests → readers (shared access, reads all entries)
 * Writer priority: if a writer is waiting, new readers block.
 */
typedef struct {
    char *data;          /* heap buffer of concatenated stat entries */
    int   len;           /* bytes currently used */
    int   capacity;      /* bytes allocated */

    /* RW-lock state */
    pthread_mutex_t mutex;
    pthread_cond_t  can_read;
    pthread_cond_t  can_write;
    int readers_active;
    int writers_active;
    int writers_waiting;

    int debug_sleep_time; /* >0 → sleep this many seconds inside CS */
} server_log_t;

void log_init   (server_log_t *log, int debug_sleep_time);
void log_destroy(server_log_t *log);

/* Writer: appends entry string; sets log_enter/log_exit timestamps */
void log_write(server_log_t *log, const char *entry,
               struct timeval *log_enter, struct timeval *log_exit);

/* Reader: copies log contents into *out_buf (caller must Free it).
 * Returns byte count. Sets log_enter/log_exit timestamps. */
int  log_read(server_log_t *log, char **out_buf,
              struct timeval *log_enter, struct timeval *log_exit);

#endif
