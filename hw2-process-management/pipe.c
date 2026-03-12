/* =========================================================
 * PATCH for fs/pipe.c  –  ban pipe
 * =========================================================
 *
 * Find sys_pipe() (or sys_pipe2()) in fs/pipe.c.
 * It is defined with SYSCALL_DEFINE1 (pipe) or SYSCALL_DEFINE2 (pipe2).
 *
 * sys_pipe() just calls sys_pipe2() with flags=0, so you only
 * need to patch sys_pipe2().  It looks like:
 *
 *   SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
 *   {
 *       struct file *files[2];
 *       int fd[2];
 *       int error;
 *       ...
 *   }
 *
 * Add the ban check as the very first line:
 *
 *   SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
 *   {
 *       if (current->syscall_bans & 0x02)   // BAN_PIPE
 *           return -EPERM;
 *
 *       struct file *files[2];
 *       ...
 *   }
 *
 * If sys_pipe() does NOT directly call sys_pipe2() but has its
 * own body, patch it the same way (add the check as first line).
 *
 * =========================================================
 */
