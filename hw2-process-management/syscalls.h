/* These declarations must be added to include/linux/syscalls.h
 * right before the final #endif at the bottom of the file.
 * (After the existing sys_statx declaration.)
 */

asmlinkage long sys_hello(void);
asmlinkage long sys_set_ban(int ban_getpid, int ban_pipe, int ban_kill);
asmlinkage long sys_get_ban(char ban);
asmlinkage long sys_check_ban(pid_t pid, char ban);
asmlinkage long sys_flip_ban_branch(int height, char ban);
