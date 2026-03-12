/* test_hw2.cxx
 * Compile: g++ test_hw2.cxx -o test_hw2
 * Run as root: sudo ./test_hw2
 */
#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>
#include <cassert>
#include <sys/wait.h>

/* Syscall numbers */
#define SYS_HELLO           333
#define SYS_SET_BAN         334
#define SYS_GET_BAN         335
#define SYS_CHECK_BAN       336
#define SYS_FLIP_BAN_BRANCH 337

static long hw2_hello() {
    return syscall(SYS_HELLO);
}
static long hw2_set_ban(int bg, int bp, int bk) {
    return syscall(SYS_SET_BAN, bg, bp, bk);
}
static long hw2_get_ban(char ban) {
    return syscall(SYS_GET_BAN, (long)ban);
}
static long hw2_check_ban(pid_t pid, char ban) {
    return syscall(SYS_CHECK_BAN, pid, (long)ban);
}
static long hw2_flip_ban_branch(int height, char ban) {
    return syscall(SYS_FLIP_BAN_BRANCH, height, (long)ban);
}

#define CHECK(expr, expected) do { \
    long _r = (expr); \
    if (_r == (expected)) { \
        std::cout << "PASS: " #expr " == " << (expected) << std::endl; \
    } else { \
        std::cout << "FAIL: " #expr " returned " << _r \
                  << ", expected " << (expected) << std::endl; \
    } \
} while(0)

int main() {
    std::cout << "=== HW2 Syscall Tests ===" << std::endl;

    /* --- Test sys_hello --- */
    std::cout << "\n-- sys_hello --" << std::endl;
    CHECK(hw2_hello(), 0);

    /* --- Test sys_set_ban: invalid args --- */
    std::cout << "\n-- sys_set_ban: invalid args --" << std::endl;
    CHECK(hw2_set_ban(-1, 0, 0), -EINVAL);
    CHECK(hw2_set_ban(0, -1, 0), -EINVAL);
    CHECK(hw2_set_ban(0, 0, -1), -EINVAL);

    /* --- Test sys_set_ban: success (must run as root) --- */
    std::cout << "\n-- sys_set_ban: set bans --" << std::endl;
    CHECK(hw2_set_ban(1, 1, 1), 0);   /* ban all three */

    /* --- Test sys_get_ban --- */
    std::cout << "\n-- sys_get_ban --" << std::endl;
    CHECK(hw2_get_ban('g'), 1);
    CHECK(hw2_get_ban('p'), 1);
    CHECK(hw2_get_ban('k'), 1);
    CHECK(hw2_get_ban('x'), -EINVAL);

    /* --- Test getpid is now banned --- */
    std::cout << "\n-- getpid banned --" << std::endl;
    long r = syscall(SYS_getpid);
    if (r == -EPERM || (r == -1 && errno == EPERM))
        std::cout << "PASS: getpid returned -EPERM as expected" << std::endl;
    else
        std::cout << "FAIL: getpid returned " << r << " (errno=" << errno << ")" << std::endl;

    /* --- Lift bans --- */
    std::cout << "\n-- sys_set_ban: lift bans --" << std::endl;
    CHECK(hw2_set_ban(0, 0, 0), 0);
    CHECK(hw2_get_ban('g'), 0);
    CHECK(hw2_get_ban('p'), 0);
    CHECK(hw2_get_ban('k'), 0);

    /* --- getpid works again --- */
    std::cout << "\n-- getpid works again --" << std::endl;
    r = syscall(SYS_getpid);
    if (r > 0)
        std::cout << "PASS: getpid returned " << r << std::endl;
    else
        std::cout << "FAIL: getpid returned " << r << std::endl;

    /* --- Test sys_check_ban --- */
    std::cout << "\n-- sys_check_ban --" << std::endl;
    pid_t my_pid = getpid();
    CHECK(hw2_check_ban(my_pid, 'g'), 0);  /* not banned */
    CHECK(hw2_set_ban(1, 0, 0), 0);
    CHECK(hw2_check_ban(my_pid, 'g'), 1);  /* now banned */
    /* caller is banned from getpid, so check_ban('g') should return -EPERM */
    CHECK(hw2_check_ban(my_pid, 'g'), -EPERM);
    CHECK(hw2_set_ban(0, 0, 0), 0);        /* lift ban */

    /* invalid pid */
    CHECK(hw2_check_ban(-1, 'g'), -ESRCH);
    /* invalid ban char */
    CHECK(hw2_check_ban(my_pid, 'x'), -EINVAL);

    /* --- Test inheritance via fork --- */
    std::cout << "\n-- ban inheritance via fork --" << std::endl;
    CHECK(hw2_set_ban(1, 0, 0), 0);  /* ban getpid in parent */
    pid_t child = fork();
    if (child == 0) {
        /* child should have inherited ban */
        long c = hw2_get_ban('g');
        if (c == 1)
            std::cout << "PASS: child inherited getpid ban" << std::endl;
        else
            std::cout << "FAIL: child did not inherit ban (got " << c << ")" << std::endl;
        exit(0);
    } else {
        waitpid(child, nullptr, 0);
        CHECK(hw2_set_ban(0, 0, 0), 0);  /* lift ban */
    }

    /* --- Test flip_ban_branch --- */
    std::cout << "\n-- flip_ban_branch --" << std::endl;
    CHECK(hw2_flip_ban_branch(0, 'g'), -EINVAL);   /* height <= 0 */
    CHECK(hw2_flip_ban_branch(-1, 'g'), -EINVAL);
    CHECK(hw2_flip_ban_branch(1, 'x'), -EINVAL);   /* invalid ban */

    /* Ban self from getpid, then try flip -> -EPERM */
    CHECK(hw2_set_ban(1, 0, 0), 0);
    CHECK(hw2_flip_ban_branch(5, 'g'), -EPERM);
    CHECK(hw2_set_ban(0, 0, 0), 0);

    /* Normal flip: travel up 3 levels, count how many got banned */
    long flipped = hw2_flip_ban_branch(3, 'p');
    std::cout << "flip_ban_branch(3, 'p') returned " << flipped
              << " (should be 0-3)" << std::endl;

    std::cout << "\n=== Done ===" << std::endl;
    return 0;
}
