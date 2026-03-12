#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/errno.h>

/*
 * Ban bit layout (stored in task_struct.syscall_bans, type: char):
 *   bit 0 (0x01): ban getpid
 *   bit 1 (0x02): ban pipe
 *   bit 2 (0x04): ban kill
 */

#define BAN_GETPID  0x01
#define BAN_PIPE    0x02
#define BAN_KILL    0x04

/* Helper: char -> bitmask */
static int ban_char_to_mask(char ban)
{
    if (ban == 'g') return BAN_GETPID;
    if (ban == 'p') return BAN_PIPE;
    if (ban == 'k') return BAN_KILL;
    return -1; /* invalid */
}

/*
 * sys_hello (333): prints "Hello, World!" to the kernel log.
 */
asmlinkage long sys_hello(void)
{
    printk("Hello, World!\n");
    return 0;
}

/*
 * sys_set_ban (334):
 *   Sets bans for the current process. Requires root privileges.
 *   ban_getpid/ban_pipe/ban_kill: 1 = ban, 0 = lift ban.
 *   Values > 1 are treated as 1. Negative values -> -EINVAL.
 *   Error priority: -EINVAL > -EPERM.
 */
asmlinkage long sys_set_ban(int ban_getpid, int ban_pipe, int ban_kill)
{
    /* Validate arguments first (highest priority error) */
    if (ban_getpid < 0 || ban_pipe < 0 || ban_kill < 0)
        return -EINVAL;

    /* Check root privileges */
    if (!uid_eq(current_uid(), GLOBAL_ROOT_UID))
        return -EPERM;

    /* Clamp values > 1 to 1 */
    if (ban_getpid > 1) ban_getpid = 1;
    if (ban_pipe   > 1) ban_pipe   = 1;
    if (ban_kill   > 1) ban_kill   = 1;

    /* Build bitmask and store */
    current->syscall_bans = (char)(
        (ban_getpid ? BAN_GETPID : 0) |
        (ban_pipe   ? BAN_PIPE   : 0) |
        (ban_kill   ? BAN_KILL   : 0)
    );

    return 0;
}

/*
 * sys_get_ban (335):
 *   Returns 1 if the current process is banned from using the syscall
 *   identified by 'ban' ('g', 'p', 'k'), 0 if not.
 *   Returns -EINVAL for invalid 'ban'.
 */
asmlinkage long sys_get_ban(char ban)
{
    int mask = ban_char_to_mask(ban);
    if (mask < 0)
        return -EINVAL;

    return (current->syscall_bans & mask) ? 1 : 0;
}

/*
 * sys_check_ban (336):
 *   Checks if process with given pid has a specific ban.
 *   The calling process must itself NOT be banned from the syscall 'ban'.
 *   Error priority: -EINVAL > -ESRCH > -EPERM.
 */
asmlinkage long sys_check_ban(pid_t pid, char ban)
{
    struct task_struct *target;
    int mask;
    long ret;

    /* Validate ban character first */
    mask = ban_char_to_mask(ban);
    if (mask < 0)
        return -EINVAL;

    /* Find target process */
    rcu_read_lock();
    target = find_task_by_vpid(pid);
    if (!target) {
        rcu_read_unlock();
        return -ESRCH;
    }

    /* Check if caller is itself banned from 'ban' */
    if (current->syscall_bans & mask) {
        rcu_read_unlock();
        return -EPERM;
    }

    ret = (target->syscall_bans & mask) ? 1 : 0;
    rcu_read_unlock();

    return ret;
}

/*
 * sys_flip_ban_branch (337):
 *   Inverts a specific ban for each direct ancestor of the calling process,
 *   up to 'height' levels up the process tree.
 *   Returns the number of parents that ended up BANNED (ban was imposed).
 *   Error: -EINVAL if height <= 0 or invalid ban char.
 *          -EPERM  if the calling process is banned from using syscall 'ban'.
 */
asmlinkage long sys_flip_ban_branch(int height, char ban)
{
    struct task_struct *proc;
    int mask;
    long banned_count = 0;
    int i;

    /* Validate */
    if (height <= 0)
        return -EINVAL;

    mask = ban_char_to_mask(ban);
    if (mask < 0)
        return -EINVAL;

    /* Check if caller is banned from 'ban' */
    if (current->syscall_bans & mask)
        return -EPERM;

    rcu_read_lock();

    proc = current->real_parent;
    for (i = 0; i < height; i++) {
        /* Stop if we've reached init (pid 1) or a process that is its own parent */
        if (!proc || proc == proc->real_parent)
            break;

        /* Flip the ban bit */
        proc->syscall_bans ^= (char)mask;

        /* Count if the ban is now SET (we imposed it) */
        if (proc->syscall_bans & mask)
            banned_count++;

        proc = proc->real_parent;
    }

    rcu_read_unlock();

    return banned_count;
}
