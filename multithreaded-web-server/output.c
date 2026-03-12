/*
 * output.c - CGI test program.
 * Use: GET /output.cgi?sleep=N  to make server take N seconds.
 * Compile: gcc -o output.cgi output.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char *qs = getenv("QUERY_STRING");
    int   secs = 0;
    if (qs) {
        char *p = strstr(qs, "sleep=");
        if (p) secs = atoi(p + 6);
    }
    if (secs > 0) sleep(secs);

    printf("Content-Type: text/html\r\n\r\n");
    printf("<html><body><h1>CGI Output</h1>");
    printf("<p>Slept %d seconds.</p></body></html>\r\n", secs);
    fflush(stdout);
    return 0;
}
