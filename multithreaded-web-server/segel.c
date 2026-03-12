#include "segel.h"

void unix_error(char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(1);
}
void posix_error(int code, char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(code));
    exit(1);
}
void app_error(char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/* Pthread wrappers */
void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
                    void *(*routine)(void *), void *argp) {
    int rc;
    if ((rc = pthread_create(tidp, attrp, routine, argp)) != 0)
        posix_error(rc, "Pthread_create error");
}
void Pthread_join(pthread_t tid, void **thread_return) {
    int rc;
    if ((rc = pthread_join(tid, thread_return)) != 0)
        posix_error(rc, "Pthread_join error");
}
void Pthread_detach(pthread_t tid) {
    int rc;
    if ((rc = pthread_detach(tid)) != 0)
        posix_error(rc, "Pthread_detach error");
}
void Pthread_exit(void *retval) { pthread_exit(retval); }
pthread_t Pthread_self(void)    { return pthread_self(); }

void Pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    int rc;
    if ((rc = pthread_mutex_init(mutex, attr)) != 0)
        posix_error(rc, "Pthread_mutex_init error");
}
void Pthread_mutex_lock(pthread_mutex_t *mutex) {
    int rc;
    if ((rc = pthread_mutex_lock(mutex)) != 0)
        posix_error(rc, "Pthread_mutex_lock error");
}
void Pthread_mutex_unlock(pthread_mutex_t *mutex) {
    int rc;
    if ((rc = pthread_mutex_unlock(mutex)) != 0)
        posix_error(rc, "Pthread_mutex_unlock error");
}
void Pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr) {
    int rc;
    if ((rc = pthread_cond_init(cond, attr)) != 0)
        posix_error(rc, "Pthread_cond_init error");
}
void Pthread_cond_signal(pthread_cond_t *cond) {
    int rc;
    if ((rc = pthread_cond_signal(cond)) != 0)
        posix_error(rc, "Pthread_cond_signal error");
}
void Pthread_cond_broadcast(pthread_cond_t *cond) {
    int rc;
    if ((rc = pthread_cond_broadcast(cond)) != 0)
        posix_error(rc, "Pthread_cond_broadcast error");
}
void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int rc;
    if ((rc = pthread_cond_wait(cond, mutex)) != 0)
        posix_error(rc, "Pthread_cond_wait error");
}

/* Rio package */
ssize_t rio_readn(int fd, void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    while (nleft > 0) {
        if ((nread = read(fd, bufp, nleft)) < 0) {
            if (errno == EINTR) nread = 0;
            else return -1;
        } else if (nread == 0) break;
        nleft -= nread; bufp += nread;
    }
    return (n - nleft);
}
ssize_t rio_writen(int fd, void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;
    while (nleft > 0) {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) {
            if (errno == EINTR) nwritten = 0;
            else return -1;
        }
        nleft -= nwritten; bufp += nwritten;
    }
    return n;
}
void rio_readinitb(rio_t *rp, int fd) {
    rp->rio_fd = fd; rp->rio_cnt = 0; rp->rio_bufptr = rp->rio_buf;
}
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n) {
    int cnt;
    while (rp->rio_cnt <= 0) {
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) { if (errno != EINTR) return -1; }
        else if (rp->rio_cnt == 0) return 0;
        else rp->rio_bufptr = rp->rio_buf;
    }
    cnt = (int)n;
    if (rp->rio_cnt < cnt) cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt; rp->rio_cnt -= cnt;
    return cnt;
}
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
    size_t nleft = n; ssize_t nread; char *bufp = usrbuf;
    while (nleft > 0) {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) {
            if (errno == EINTR) nread = 0; else return -1;
        } else if (nread == 0) break;
        nleft -= nread; bufp += nread;
    }
    return (n - nleft);
}
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
    int n; ssize_t rc; char c, *bufp = usrbuf;
    for (n = 1; n < (int)maxlen; n++) {
        if ((rc = rio_read(rp, &c, 1)) == 1) {
            *bufp++ = c; if (c == '\n') break;
        } else if (rc == 0) { if (n == 1) return 0; else break; }
        else return -1;
    }
    *bufp = 0; return n;
}

ssize_t Rio_readn(int fd, void *ptr, size_t nbytes) {
    ssize_t n;
    if ((n = rio_readn(fd, ptr, nbytes)) < 0) unix_error("Rio_readn error");
    return n;
}
void Rio_writen(int fd, void *usrbuf, size_t n) {
    if (rio_writen(fd, usrbuf, n) != (ssize_t)n) unix_error("Rio_writen error");
}
void    Rio_readinitb(rio_t *rp, int fd) { rio_readinitb(rp, fd); }
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
    ssize_t rc;
    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) unix_error("Rio_readnb error");
    return rc;
}
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;
    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) unix_error("Rio_readlineb error");
    return rc;
}

/* Memory */
void *Malloc(size_t size) {
    void *p; if ((p = malloc(size)) == NULL) unix_error("Malloc error"); return p;
}
void *Realloc(void *ptr, size_t size) {
    void *p; if ((p = realloc(ptr, size)) == NULL) unix_error("Realloc error"); return p;
}
void *Calloc(size_t nmemb, size_t size) {
    void *p; if ((p = calloc(nmemb, size)) == NULL) unix_error("Calloc error"); return p;
}
void Free(void *ptr) { free(ptr); }

/* Process control */
pid_t Fork(void) {
    pid_t pid; if ((pid = fork()) < 0) unix_error("Fork error"); return pid;
}
void Execve(const char *filename, char *const argv[], char *const envp[]) {
    if (execve(filename, argv, envp) < 0) unix_error("Execve error");
}
pid_t Wait(int *status) {
    pid_t pid; if ((pid = wait(status)) < 0) unix_error("Wait error"); return pid;
}
pid_t Waitpid(pid_t pid, int *iptr, int options) {
    pid_t retpid;
    if ((retpid = waitpid(pid, iptr, options)) < 0) unix_error("Waitpid error");
    return retpid;
}
void Kill(pid_t pid, int signum) {
    if (kill(pid, signum) < 0) unix_error("Kill error");
}

/* Unix I/O */
int Open(const char *pathname, int flags, mode_t mode) {
    int rc; if ((rc = open(pathname, flags, mode)) < 0) unix_error("Open error"); return rc;
}
ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t rc; if ((rc = read(fd, buf, count)) < 0) unix_error("Read error"); return rc;
}
ssize_t Write(int fd, const void *buf, size_t count) {
    ssize_t rc; if ((rc = write(fd, buf, count)) < 0) unix_error("Write error"); return rc;
}
off_t Lseek(int fildes, off_t offset, int whence) {
    off_t rc; if ((rc = lseek(fildes, offset, whence)) < 0) unix_error("Lseek error"); return rc;
}
void Close(int fd) { if (close(fd) < 0) unix_error("Close error"); }
int  Dup2(int fd1, int fd2) {
    int rc; if ((rc = dup2(fd1, fd2)) < 0) unix_error("Dup2 error"); return rc;
}
void Stat(const char *filename, struct stat *buf) {
    if (stat(filename, buf) < 0) unix_error("Stat error");
}
void Fstat(int fd, struct stat *buf) {
    if (fstat(fd, buf) < 0) unix_error("Fstat error");
}

/* Sockets */
int Socket(int domain, int type, int protocol) {
    int rc; if ((rc = socket(domain, type, protocol)) < 0) unix_error("Socket error"); return rc;
}
void Setsockopt(int s, int level, int optname, const void *optval, int optlen) {
    if (setsockopt(s, level, optname, optval, optlen) < 0) unix_error("Setsockopt error");
}
void Bind(int sockfd, struct sockaddr *my_addr, int addrlen) {
    if (bind(sockfd, my_addr, addrlen) < 0) unix_error("Bind error");
}
void Listen(int s, int backlog) {
    if (listen(s, backlog) < 0) unix_error("Listen error");
}
int Accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int rc; if ((rc = accept(s, addr, addrlen)) < 0) unix_error("Accept error"); return rc;
}
void Connect(int sockfd, struct sockaddr *serv_addr, int addrlen) {
    if (connect(sockfd, serv_addr, addrlen) < 0) unix_error("Connect error");
}

/* DNS */
struct hostent *Gethostbyname(const char *name) {
    struct hostent *p;
    if ((p = gethostbyname(name)) == NULL) app_error("Gethostbyname error");
    return p;
}
