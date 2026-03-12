/* =========================================================
 * PATCH for kernel/signal.c  –  ban kill
 * =========================================================
 *
 * Find sys_kill() in kernel/signal.c.  It is defined with the
 * SYSCALL_DEFINE2 macro and looks like this:
 *
 *   SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
 *   {
 *       struct siginfo info;
 *       ...
 *       <first real line of body>
 *   }
 *
 * Add the ban check as the very first line of the function body,
 * BEFORE any other work is done:
 *
 *   SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
 *   {
 *       if (current->syscall_bans & 0x04)   // BAN_KILL
 *           return -EPERM;
 *
 *       struct siginfo info;
 *       ...
 *   }
 *
 * =========================================================
 */
