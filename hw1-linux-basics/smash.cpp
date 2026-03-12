#include "Commands.h"
#include "signals.h"
#include <iostream>
#include <string>
#include <signal.h>

int main() {
    // Set up signal handler for Ctrl+C
    struct sigaction sa;
    sa.sa_handler = ctrlCHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        perror("smash error: sigaction failed");
        return 1;
    }

    SmashShell& smash = SmashShell::getInstance();

    while (true) {
        std::cout << smash.getPrompt() << "> ";
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            // EOF
            break;
        }

        smash.executeCommand(line);
    }

    return 0;
}
