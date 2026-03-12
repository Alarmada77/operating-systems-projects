#include "signals.h"
#include "Commands.h"
#include <iostream>
#include <unistd.h>
#include <signal.h>

void ctrlCHandler(int sig_num) {
    std::cout << "smash: got ctrl-C" << std::endl;
    SmashShell& smash = SmashShell::getInstance();
    pid_t fg_pid = smash.getFgPid();
    if (fg_pid > 0) {
        if (kill(fg_pid, SIGKILL) == -1) {
            perror("smash error: kill failed");
            return;
        }
        std::cout << "smash: process " << fg_pid << " was killed" << std::endl;
        smash.setFgPid(-1);
        smash.setFgCmd("");
    }
}
