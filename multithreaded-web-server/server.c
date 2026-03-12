#include "segel.h"
#include "request.h"
#include "log.h"

/* ================================================================
 * Bounded FIFO request queue
 * ================================================================ */
typedef struct {
    int            connfd;
    struct timeval arrival;
} queue_item_t;

typedef struct {
    queue_item_t   *items;
    int             cap;
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} req_queue_t;

static void queue_init(req_queue_t *q, int cap) {
    q->items = (queue_item_t *)Malloc(cap * sizeof(queue_item_t));
    q->cap = cap; q->head = 0; q->tail = 0; q->count = 0;
    Pthread_mutex_init(&q->mutex, NULL);
    Pthread_cond_init(&q->not_full,  NULL);
    Pthread_cond_init(&q->not_empty, NULL);
}

static void queue_push(req_queue_t *q, int connfd, struct timeval *arr) {
    Pthread_mutex_lock(&q->mutex);
    while (q->count == q->cap)
        Pthread_cond_wait(&q->not_full, &q->mutex);
    q->items[q->tail].connfd  = connfd;
    q->items[q->tail].arrival = *arr;
    q->tail  = (q->tail + 1) % q->cap;
    q->count++;
    Pthread_cond_signal(&q->not_empty);
    Pthread_mutex_unlock(&q->mutex);
}

static queue_item_t queue_pop(req_queue_t *q) {
    Pthread_mutex_lock(&q->mutex);
    while (q->count == 0)
        Pthread_cond_wait(&q->not_empty, &q->mutex);
    queue_item_t item = q->items[q->head];
    q->head  = (q->head + 1) % q->cap;
    q->count--;
    Pthread_cond_signal(&q->not_full);
    Pthread_mutex_unlock(&q->mutex);
    return item;
}

/* ================================================================
 * Globals
 * ================================================================ */
static req_queue_t   g_queue;
static server_log_t  g_log;
static threads_stats *g_tstats;   /* array [0..num_threads-1] */

/* ================================================================
 * Worker thread
 * ================================================================ */
typedef struct { int idx; } worker_arg_t;

static void *worker(void *arg) {
    int idx = ((worker_arg_t *)arg)->idx;
    Free(arg);

    while (1) {
        queue_item_t item = queue_pop(&g_queue);

        time_stats tm;
        tm.task_arrival  = item.arrival;
        gettimeofday(&tm.task_dispatch, NULL);
        tm.log_enter = tm.task_dispatch;
        tm.log_exit  = tm.task_dispatch;

        handle_request(item.connfd, &g_tstats[idx], &tm, &g_log);
        Close(item.connfd);
    }
    return NULL;
}

/* ================================================================
 * main
 * ================================================================ */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <port> <threads> <queue_size> [debug_sleep_time]\n",
            argv[0]);
        exit(1);
    }

    int port             = atoi(argv[1]);
    int num_threads      = atoi(argv[2]);
    int queue_size       = atoi(argv[3]);
    int debug_sleep_time = (argc >= 5) ? atoi(argv[4]) : 0;

    if (num_threads <= 0 || queue_size <= 0) {
        fprintf(stderr, "threads and queue_size must be positive\n");
        exit(1);
    }

    log_init(&g_log, debug_sleep_time);
    queue_init(&g_queue, queue_size);

    /* per-thread stats */
    g_tstats = (threads_stats *)Calloc(num_threads, sizeof(threads_stats));
    for (int i = 0; i < num_threads; i++)
        g_tstats[i].id = i + 1;

    /* spawn worker threads */
    pthread_t *tids = (pthread_t *)Malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        worker_arg_t *a = (worker_arg_t *)Malloc(sizeof(worker_arg_t));
        a->idx = i;
        Pthread_create(&tids[i], NULL, worker, a);
    }

    /* master: accept → enqueue */
    int listenfd = Open_listenfd(port);
    fprintf(stdout,
        "Server: port=%d threads=%d queue=%d debug_sleep=%d\n",
        port, num_threads, queue_size, debug_sleep_time);
    fflush(stdout);

    while (1) {
        struct sockaddr_in ca;
        socklen_t clen = sizeof(ca);
        int connfd = Accept(listenfd, (SA *)&ca, &clen);

        struct timeval arr;
        gettimeofday(&arr, NULL);
        queue_push(&g_queue, connfd, &arr);
    }

    /* unreachable */
    Free(tids);
    Free(g_tstats);
    log_destroy(&g_log);
    return 0;
}
