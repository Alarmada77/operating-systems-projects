#ifndef __SEGEL_H__
#define __SEGEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <dirent.h>
#include <stdarg.h>

typedef struct sockaddr SA;

#define RIO_BUFSIZE 8192
typedef struct {
    int   rio_fd;
    int   rio_cnt;
    char *rio_bufptr;
    char  rio_buf[RIO_BUFSIZE];
} rio_t;

extern char **environ;

#define MAXLINE  8192
#define MAXBUF   8192
#define LISTENQ  1024

/* Error handlers */
void unix_error(char *msg);
void posix_error(int code, char *msg);
void app_error(char *msg);

/* Pthread wrappers */
void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
                    void *(*routine)(void *), void *argp);
void Pthread_join(pthread_t tid, void **thread_return);
void Pthread_detach(pthread_t tid);
void Pthread_exit(void *retval);
pthread_t Pthread_self(void);

void Pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
void Pthread_mutex_lock(pthread_mutex_t *mutex);
void Pthread_mutex_unlock(pthread_mutex_t *mutex);

void Pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);
void Pthread_cond_signal(pthread_cond_t *cond);
void Pthread_cond_broadcast(pthread_cond_t *cond);
void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

/* Rio package */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void    rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

ssize_t Rio_readn(int fd, void *usrbuf, size_t n);
void    Rio_writen(int fd, void *usrbuf, size_t n);
void    Rio_readinitb(rio_t *rp, int fd);
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

/* Memory */
void *Malloc(size_t size);
void *Realloc(void *ptr, size_t size);
void *Calloc(size_t nmemb, size_t size);
void  Free(void *ptr);

/* Process control */
pid_t Fork(void);
void  Execve(const char *filename, char *const argv[], char *const envp[]);
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *iptr, int options);
void  Kill(pid_t pid, int signum);

/* Unix I/O */
int     Open(const char *pathname, int flags, mode_t mode);
ssize_t Read(int fd, void *buf, size_t count);
ssize_t Write(int fd, const void *buf, size_t count);
off_t   Lseek(int fildes, off_t offset, int whence);
void    Close(int fd);
int     Dup2(int fd1, int fd2);
void    Stat(const char *filename, struct stat *buf);
void    Fstat(int fd, struct stat *buf);

/* Sockets */
int  Socket(int domain, int type, int protocol);
void Setsockopt(int s, int level, int optname, const void *optval, int optlen);
void Bind(int sockfd, struct sockaddr *my_addr, int addrlen);
void Listen(int s, int backlog);
int  Accept(int s, struct sockaddr *addr, socklen_t *addrlen);
void Connect(int sockfd, struct sockaddr *serv_addr, int addrlen);

/* DNS */
struct hostent *Gethostbyname(const char *name);

#endif /* __SEGEL_H__ */
