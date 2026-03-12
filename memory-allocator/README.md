# Custom Memory Allocator

Implementation of a dynamic memory allocator as part of the Operating Systems course.

This project implements replacements for the standard memory allocation functions:

- malloc
- free
- calloc
- realloc

without using the standard libc implementations.

## Implementation Stages

### Part 1 – Naive Allocator
Basic heap expansion using `sbrk()`.

### Part 2 – Free List Allocator
Metadata structures and linked lists for tracking allocated blocks.

### Part 3 – Buddy Allocator
Power-of-two block allocation with block splitting and merging to reduce fragmentation.

## Technologies

- C++
- Linux system calls (`sbrk`, `mmap`)
