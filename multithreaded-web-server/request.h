#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "segel.h"
#include "log.h"

/* Per-request timing */
typedef struct {
    struct timeval task_arrival;   /* master: time connection was accepted  */
    struct timeval task_dispatch;  /* worker: time job was dequeued         */
    struct timeval log_enter;      /* time request entered log CS           */
    struct timeval log_exit;       /* time request left log CS              */
} time_stats;

/* Per-thread counters */
typedef struct {
    int id;        /* 1-based worker id */
    int total_req; /* all requests (including errors) */
    int stat_req;  /* successful static GET */
    int dynm_req;  /* successful dynamic GET */
    int post_req;  /* successful POST */
} threads_stats;

/* Appends stat headers to buf (buf already contains some content).
 * Returns new total length of buf. */
int append_stats(char *buf, threads_stats t_stats, time_stats tm_stats);

/* Handle one HTTP request on fd. Updates *t_stats and *tm_stats. */
void handle_request(int fd, threads_stats *t_stats,
                    time_stats *tm_stats, server_log_t *log);

/* Open a listening socket on port */
int Open_listenfd(int port);

#endif
