/* =========================================================
 * HOW TO ADD syscall_bans TO task_struct
 * File: include/linux/sched.h
 * =========================================================
 *
 * Find the task_struct definition. Look for a group of small
 * single-byte fields near the end of the struct. A good place
 * to add it is right after the existing field:
 *
 *     u8 in_execve;
 *
 * Add this single line:
 *
 *     char syscall_bans;   /* HW2: per-process syscall ban bitmap */
 *
 * Layout of the byte:
 *   bit 0 (0x01) = getpid banned
 *   bit 1 (0x02) = pipe   banned
 *   bit 2 (0x04) = kill   banned
 *
 * This adds exactly 1 byte to task_struct (may be absorbed
 * into existing alignment padding, so net growth can be 0).
 *
 * The field is automatically zero-initialized (no bans) for
 * every new process because task_struct is zeroed on allocation
 * and fork() copies it from the parent -- which is exactly the
 * inheritance semantics required by the spec.
 * =========================================================
 */
