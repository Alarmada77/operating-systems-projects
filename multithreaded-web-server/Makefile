CC     = gcc
CFLAGS = -Wall -Wextra -g -pthread
OBJS_SERVER = server.o request.o log.o segel.o
OBJS_CLIENT = client.o segel.o

.PHONY: all clean server client

all: server client output.cgi public

server: $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $^

client: $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^

output.cgi: output.o
	$(CC) $(CFLAGS) -o $@ $^

server.o:  server.c  segel.h request.h log.h
request.o: request.c segel.h request.h log.h
log.o:     log.c     segel.h log.h
segel.o:   segel.c   segel.h
client.o:  client.c  segel.h
output.o:  output.c

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

public:
	mkdir -p public
	@if [ ! -f public/home.html ]; then \
	  printf '<html>\n<head><title>OS-HW3 Test Web Page</title></head>\n<body>\n<h2>OS-HW3 Test Web Page</h2>\n<p>Test web page: simply awesome.</p>\n</body>\n</html>\n' > public/home.html; \
	fi

clean:
	rm -f *.o server client output.cgi
