#include "log.h"

#define LOG_INIT_CAP 4096

void log_init(server_log_t *log, int debug_sleep_time) {
    log->data     = (char *)Malloc(LOG_INIT_CAP);
    log->data[0]  = '\0';
    log->len      = 0;
    log->capacity = LOG_INIT_CAP;

    Pthread_mutex_init(&log->mutex, NULL);
    Pthread_cond_init(&log->can_read,  NULL);
    Pthread_cond_init(&log->can_write, NULL);

    log->readers_active  = 0;
    log->writers_active  = 0;
    log->writers_waiting = 0;
    log->debug_sleep_time = (debug_sleep_time > 0) ? debug_sleep_time : 0;
}

void log_destroy(server_log_t *log) {
    Free(log->data);
    pthread_mutex_destroy(&log->mutex);
    pthread_cond_destroy(&log->can_read);
    pthread_cond_destroy(&log->can_write);
}

/* ---------- internal lock helpers ---------- */

static void rw_write_lock(server_log_t *log) {
    Pthread_mutex_lock(&log->mutex);
    log->writers_waiting++;
    while (log->readers_active > 0 || log->writers_active > 0)
        Pthread_cond_wait(&log->can_write, &log->mutex);
    log->writers_waiting--;
    log->writers_active = 1;
    Pthread_mutex_unlock(&log->mutex);
}

static void rw_write_unlock(server_log_t *log) {
    Pthread_mutex_lock(&log->mutex);
    log->writers_active = 0;
    /* Give priority to other waiting writers */
    if (log->writers_waiting > 0)
        Pthread_cond_signal(&log->can_write);
    else
        Pthread_cond_broadcast(&log->can_read);
    Pthread_mutex_unlock(&log->mutex);
}

static void rw_read_lock(server_log_t *log) {
    Pthread_mutex_lock(&log->mutex);
    /* Block if any writer is active or waiting (writer priority) */
    while (log->writers_active > 0 || log->writers_waiting > 0)
        Pthread_cond_wait(&log->can_read, &log->mutex);
    log->readers_active++;
    Pthread_mutex_unlock(&log->mutex);
}

static void rw_read_unlock(server_log_t *log) {
    Pthread_mutex_lock(&log->mutex);
    log->readers_active--;
    if (log->readers_active == 0)
        Pthread_cond_signal(&log->can_write);
    Pthread_mutex_unlock(&log->mutex);
}

/* ---------- public API ---------- */

void log_write(server_log_t *log, const char *entry,
               struct timeval *log_enter, struct timeval *log_exit) {
    rw_write_lock(log);

    gettimeofday(log_enter, NULL);
    if (log->debug_sleep_time > 0)
        sleep(log->debug_sleep_time);

    int elen = (int)strlen(entry);
    while (log->len + elen + 1 > log->capacity) {
        log->capacity *= 2;
        log->data = (char *)Realloc(log->data, log->capacity);
    }
    memcpy(log->data + log->len, entry, elen);
    log->len += elen;
    log->data[log->len] = '\0';

    gettimeofday(log_exit, NULL);

    rw_write_unlock(log);
}

int log_read(server_log_t *log, char **out_buf,
             struct timeval *log_enter, struct timeval *log_exit) {
    rw_read_lock(log);

    gettimeofday(log_enter, NULL);
    if (log->debug_sleep_time > 0)
        sleep(log->debug_sleep_time);

    int len  = log->len;
    *out_buf = (char *)Malloc(len + 1);
    memcpy(*out_buf, log->data, len + 1);

    gettimeofday(log_exit, NULL);

    rw_read_unlock(log);
    return len;
}
