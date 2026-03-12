#include "segel.h"

/* Usage: ./client <hostname> <port> <filename> <method> */

static void print_response(int fd) {
    rio_t rio;
    char  buf[MAXBUF];
    int   length = 0;
    ssize_t n;

    Rio_readinitb(&rio, fd);
    n = Rio_readlineb(&rio, buf, MAXBUF);
    while (strcmp(buf, "\r\n") != 0 && n > 0) {
        printf("Header: %s", buf);
        if (strncasecmp(buf, "Content-Length: ", 16) == 0)
            length = atoi(buf + 16);
        n = Rio_readlineb(&rio, buf, MAXBUF);
    }
    printf("Length = %d\n", length);

    int remaining = length;
    while (remaining > 0) {
        int chunk = remaining < MAXBUF ? remaining : MAXBUF;
        n = Rio_readnb(&rio, buf, chunk);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("%s", buf);
        remaining -= (int)n;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <hostname> <port> <filename> <method>\n",
                argv[0]);
        exit(1);
    }
    char *hostname = argv[1];
    int   port     = atoi(argv[2]);
    char *filename = argv[3];
    char *method   = argv[4];

    struct hostent *hp = Gethostbyname(hostname);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, hp->h_addr, hp->h_length);
    sa.sin_port = htons(port);

    int fd = Socket(AF_INET, SOCK_STREAM, 0);
    Connect(fd, (SA *)&sa, sizeof(sa));
    printf("Connected to server. clientfd = %d\n", fd);

    char req[MAXBUF];
    snprintf(req, sizeof(req),
             "%s /%s HTTP/1.1\r\nhost: %s\r\n\r\n",
             method, filename, hostname);
    printf("Request:\n%s\n", req);
    Rio_writen(fd, req, strlen(req));

    print_response(fd);
    Close(fd);
    return 0;
}
