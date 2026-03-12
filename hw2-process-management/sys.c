/* =========================================================
 * PATCH for kernel/sys.c  –  ban getpid
 * =========================================================
 *
 * Find sys_getpid() in kernel/sys.c.  It is defined with the
 * SYSCALL_DEFINE0 macro and looks like this:
 *
 *   SYSCALL_DEFINE0(getpid)
 *   {
 *       return task_tgid_vnr(current);
 *   }
 *
 * Replace it with:
 *
 *   SYSCALL_DEFINE0(getpid)
 *   {
 *       if (current->syscall_bans & 0x01)   // BAN_GETPID
 *           return -EPERM;
 *       return task_tgid_vnr(current);
 *   }
 *
 * =========================================================
 */
