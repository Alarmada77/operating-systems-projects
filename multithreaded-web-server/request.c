#include "request.h"

/* ================================================================
 * append_stats
 * Matches exactly the format shown in the assignment screenshot.
 * The last stat ends with \r\n\r\n (blank line after stats, before body).
 * ================================================================ */
int append_stats(char *buf, threads_stats t_stats, time_stats tm_stats) {
    int offset = (int)strlen(buf);

    offset += sprintf(buf + offset,
        "Stat-Req-Arrival:: %ld.%06ld\r\n",
        (long)tm_stats.task_arrival.tv_sec,
        (long)tm_stats.task_arrival.tv_usec);
    offset += sprintf(buf + offset,
        "Stat-Req-Dispatch:: %ld.%06ld\r\n",
        (long)tm_stats.task_dispatch.tv_sec,
        (long)tm_stats.task_dispatch.tv_usec);
    offset += sprintf(buf + offset,
        "Stat-Log-Arrival:: %ld.%06ld\r\n",
        (long)tm_stats.log_enter.tv_sec,
        (long)tm_stats.log_enter.tv_usec);
    offset += sprintf(buf + offset,
        "Stat-Log-Dispatch:: %ld.%06ld\r\n",
        (long)tm_stats.log_exit.tv_sec,
        (long)tm_stats.log_exit.tv_usec);
    offset += sprintf(buf + offset,
        "Stat-Thread-Id:: %d\r\n",      t_stats.id);
    offset += sprintf(buf + offset,
        "Stat-Thread-Count:: %d\r\n",   t_stats.total_req);
    offset += sprintf(buf + offset,
        "Stat-Thread-Static:: %d\r\n",  t_stats.stat_req);
    offset += sprintf(buf + offset,
        "Stat-Thread-Dynamic:: %d\r\n", t_stats.dynm_req);
    offset += sprintf(buf + offset,
        "Stat-Thread-Post:: %d\r\n\r\n", t_stats.post_req);

    return offset;
}

/* ================================================================
 * Helpers
 * ================================================================ */

static void get_filetype(char *filename, char *filetype) {
    if      (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))  strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))  strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png"))  strcpy(filetype, "image/png");
    else                                strcpy(filetype, "text/plain");
}

/* Build an error response; stat headers are sent as part of the response. */
static void send_error(int fd, char *cause, char *errnum,
                       char *shortmsg, char *longmsg,
                       threads_stats *ts, time_stats *tm) {
    char body[MAXBUF], hdr[MAXBUF];

    /* body */
    snprintf(body, sizeof(body),
        "<html><title>OS-HW3 Error</title>"
        "<body bgcolor=\"ffffff\">\r\n"
        "%s: %s\r\n"
        "<p>%s: %s\r\n"
        "<hr>OS-HW3 Web Server\r\n",
        errnum, shortmsg, longmsg, cause);

    /* response line + standard headers */
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %s %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n",
        errnum, shortmsg, (int)strlen(body));

    Rio_writen(fd, hdr, hlen);

    /* stat headers + blank line */
    char stats[MAXBUF]; stats[0] = '\0';
    append_stats(stats, *ts, *tm);
    Rio_writen(fd, stats, strlen(stats));

    Rio_writen(fd, body, strlen(body));
}

/* ================================================================
 * Static file handler
 * ================================================================ */
static void serve_static(int fd, char *filename, int filesize,
                         threads_stats *ts, time_stats *tm) {
    char filetype[MAXLINE], hdr[MAXBUF];
    get_filetype(filename, filetype);

    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Server: OS-HW3 Web Server\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: %s\r\n",
        filesize, filetype);
    Rio_writen(fd, hdr, hlen);

    char stats[MAXBUF]; stats[0] = '\0';
    append_stats(stats, *ts, *tm);
    Rio_writen(fd, stats, strlen(stats));

    /* send file */
    int srcfd = Open(filename, O_RDONLY, 0);
    char *srcp = (char *)Malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    Free(srcp);
}

/* ================================================================
 * Dynamic (CGI) handler — do NOT modify the forking logic
 * ================================================================ */
static void serve_dynamic(int fd, char *filename, char *cgiargs,
                          threads_stats *ts, time_stats *tm) {
    char hdr[MAXBUF];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Server: OS-HW3 Web Server\r\n");
    Rio_writen(fd, hdr, hlen);

    char stats[MAXBUF]; stats[0] = '\0';
    append_stats(stats, *ts, *tm);
    Rio_writen(fd, stats, strlen(stats));

    pid_t pid = Fork();
    if (pid == 0) {
        setenv("QUERY_STRING", cgiargs, 1);
        Dup2(fd, STDOUT_FILENO);
        char *argv[] = { filename, NULL };
        Execve(filename, argv, environ);
    }
    Waitpid(pid, NULL, 0);
}

/* ================================================================
 * POST handler — reads log and returns it
 * ================================================================ */
static void serve_post(int fd, threads_stats *ts, time_stats *tm,
                       server_log_t *log) {
    char *log_buf = NULL;
    int   log_len = log_read(log, &log_buf,
                             &tm->log_enter, &tm->log_exit);

    const char *body     = log_buf;
    const char *fallback = "Log is not implemented.\n";
    if (log_len == 0) { body = fallback; log_len = (int)strlen(fallback); }

    char hdr[MAXBUF];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Server: OS-HW3 Web Server\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/plain\r\n",
        log_len);
    Rio_writen(fd, hdr, hlen);

    char stats[MAXBUF]; stats[0] = '\0';
    append_stats(stats, *ts, *tm);
    Rio_writen(fd, stats, strlen(stats));

    Rio_writen(fd, (void *)body, log_len);

    if (log_buf) Free(log_buf);
}

/* ================================================================
 * handle_request — main entry point called by worker threads
 * ================================================================ */
void handle_request(int fd, threads_stats *ts,
                    time_stats *tm, server_log_t *log) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
    struct stat sbuf;

    /* log timestamps default to dispatch time until log actually runs */
    tm->log_enter = tm->task_dispatch;
    tm->log_exit  = tm->task_dispatch;

    Rio_readinitb(&rio, fd);
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) return;
    sscanf(buf, "%s %s %s", method, uri, version);

    /* drain remaining request headers */
    while (strcmp(buf, "\r\n") != 0 && strlen(buf) > 0)
        if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) break;

    /* every request counts */
    ts->total_req++;

    /* ---- unsupported method ---- */
    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "POST") != 0) {
        send_error(fd, method, "501", "Not Implemented",
                   "OS-HW3 Server does not implement this method", ts, tm);
        return;
    }

    /* ---- POST ---- */
    if (strcasecmp(method, "POST") == 0) {
        ts->post_req++;
        serve_post(fd, ts, tm, log);
        return;
    }

    /* ---- GET: parse URI ---- */
    int is_dynamic = (strstr(uri, ".cgi") != NULL);
    cgiargs[0] = '\0';

    if (is_dynamic) {
        char *q = strchr(uri, '?');
        if (q) { strncpy(cgiargs, q + 1, MAXLINE - 1); cgiargs[MAXLINE-1] = '\0'; *q = '\0'; }
        snprintf(filename, MAXLINE, ".%s", uri);
    } else {
        snprintf(filename, MAXLINE, "./public%s", uri);
        if (uri[strlen(uri) - 1] == '/')
            strncat(filename, "home.html", MAXLINE - strlen(filename) - 1);
    }

    if (stat(filename, &sbuf) < 0) {
        send_error(fd, filename, "404", "Not found",
                   "OS-HW3 Server could not find this file", ts, tm);
        return;
    }

    if (is_dynamic) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            send_error(fd, filename, "403", "Forbidden",
                       "OS-HW3 Server could not run the CGI program", ts, tm);
            return;
        }
        ts->dynm_req++;

        /* build log entry BEFORE writing to log so timestamps are in order */
        char entry[MAXBUF]; entry[0] = '\0';
        append_stats(entry, *ts, *tm);
        log_write(log, entry, &tm->log_enter, &tm->log_exit);

        serve_dynamic(fd, filename, cgiargs, ts, tm);
    } else {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            send_error(fd, filename, "403", "Forbidden",
                       "OS-HW3 Server could not read the file", ts, tm);
            return;
        }
        ts->stat_req++;

        /* write log entry, then serve (log timestamps get set inside log_write) */
        char entry[MAXBUF]; entry[0] = '\0';
        append_stats(entry, *ts, *tm);
        log_write(log, entry, &tm->log_enter, &tm->log_exit);

        serve_static(fd, filename, sbuf.st_size, ts, tm);
    }
}

/* ================================================================
 * Open_listenfd
 * ================================================================ */
int Open_listenfd(int port) {
    int listenfd, optval = 1;
    struct sockaddr_in sa;

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);
    Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((unsigned short)port);
    Bind(listenfd, (SA *)&sa, sizeof(sa));
    Listen(listenfd, LISTENQ);
    return listenfd;
}
