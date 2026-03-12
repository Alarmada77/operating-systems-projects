# Multithreaded Web Server

Implementation of a concurrent HTTP server written in C.

The server uses a fixed-size thread pool and a producer-consumer architecture
to handle multiple client requests simultaneously.

## Features

- Thread pool using POSIX threads
- Bounded FIFO request queue
- Reader-writer synchronization for the server log
- Request statistics collection
- Support for GET and POST HTTP methods

## Technologies

- C
- POSIX Threads (pthreads)
- Synchronization primitives (mutexes, condition variables)
