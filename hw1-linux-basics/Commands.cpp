#include "Commands.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <regex>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <utmpx.h>

struct linux_dirent64 {
    ino64_t        d_ino;
    off64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

extern char** __environ;

SmashShell* SmashShell::instance = nullptr;

// ==================== Helper Functions ====================

std::string _trim(const std::string& s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(WHITESPACE);
    return s.substr(start, end - start + 1);
}

std::vector<std::string> _split(const std::string& s, const std::string& delimiters) {
    std::vector<std::string> tokens;
    size_t start = s.find_first_not_of(delimiters);
    while (start != std::string::npos) {
        size_t end = s.find_first_of(delimiters, start);
        tokens.push_back(s.substr(start, end - start));
        start = (end == std::string::npos) ? std::string::npos : s.find_first_not_of(delimiters, end);
    }
    return tokens;
}

bool _isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-') start = 1;
    if (start == s.size()) return false;
    for (size_t i = start; i < s.size(); i++)
        if (!std::isdigit(s[i])) return false;
    return true;
}

bool _isBackground(const std::string& cmd_line) {
    std::string trimmed = _trim(cmd_line);
    return !trimmed.empty() && trimmed.back() == '&';
}

std::string _removeBackground(const std::string& cmd_line) {
    std::string trimmed = _trim(cmd_line);
    if (!trimmed.empty() && trimmed.back() == '&') {
        trimmed.pop_back();
        return _trim(trimmed);
    }
    return trimmed;
}

// Build argv array from tokens (caller must free)
static char** _buildArgv(const std::vector<std::string>& tokens) {
    char** argv = new char*[tokens.size() + 1];
    for (size_t i = 0; i < tokens.size(); i++) {
        argv[i] = new char[tokens[i].size() + 1];
        std::strcpy(argv[i], tokens[i].c_str());
    }
    argv[tokens.size()] = nullptr;
    return argv;
}


// ==================== SmashShell ====================

bool SmashShell::isReservedKeyword(const std::string& name) const {
    static const std::vector<std::string> reserved = {
        "chprompt","showpid","pwd","cd","jobs","fg","quit","kill",
        "alias","unalias","unsetenv","sysinfo","du","whoami","usbinfo"
    };
    for (auto& kw : reserved)
        if (kw == name) return true;
    return false;
}

bool SmashShell::aliasExists(const std::string& name) const {
    return aliases.count(name) > 0;
}

void SmashShell::addAlias(const std::string& name, const std::string& cmd) {
    aliases[name] = cmd;
    alias_order.push_back(name);
}

void SmashShell::removeAlias(const std::string& name) {
    aliases.erase(name);
    alias_order.erase(std::remove(alias_order.begin(), alias_order.end(), name), alias_order.end());
}

// Resolve first word alias; only replaces command word (before space/special char)
std::string SmashShell::resolveAlias(const std::string& cmd_line) const {
    std::string trimmed = _trim(cmd_line);
    if (trimmed.empty()) return cmd_line;
    // find first word (until space or special char |, >, &)
    size_t end = trimmed.find_first_of(" \t|>&");
    std::string first_word = (end == std::string::npos) ? trimmed : trimmed.substr(0, end);
    auto it = aliases.find(first_word);
    if (it != aliases.end()) {
        std::string rest = (end == std::string::npos) ? "" : trimmed.substr(end);
        return it->second + rest;
    }
    return cmd_line;
}

Command* SmashShell::createCommand(const std::string& cmd_line) {
    std::string trimmed = _trim(cmd_line);
    if (trimmed.empty()) return nullptr;

    // Check for pipe (|& before |)
    // Find |& first, then |
    size_t pipe_stderr = trimmed.find("|&");
    size_t pipe_stdout = std::string::npos;
    // find | that is not part of |&
    {
        size_t pos = 0;
        while (pos < trimmed.size()) {
            size_t f = trimmed.find('|', pos);
            if (f == std::string::npos) break;
            if (f + 1 < trimmed.size() && trimmed[f+1] == '&') {
                pos = f + 2;
                continue;
            }
            pipe_stdout = f;
            break;
        }
    }
    if (pipe_stderr != std::string::npos) {
        return new PipeCommand(trimmed, true);
    }
    if (pipe_stdout != std::string::npos) {
        return new PipeCommand(trimmed, false);
    }

    // Check for redirection >> before >
    size_t redir_append = trimmed.find(">>");
    size_t redir_over   = std::string::npos;
    {
        size_t pos = 0;
        while (pos < trimmed.size()) {
            size_t f = trimmed.find('>', pos);
            if (f == std::string::npos) break;
            if (f + 1 < trimmed.size() && trimmed[f+1] == '>') {
                pos = f + 2;
                continue;
            }
            redir_over = f;
            break;
        }
    }
    if (redir_append != std::string::npos) {
        return new RedirectionCommand(trimmed, true);
    }
    if (redir_over != std::string::npos) {
        return new RedirectionCommand(trimmed, false);
    }

    // Determine effective command after alias resolution
    // (already resolved before createCommand is called in executeCommand)
    bool bg = _isBackground(trimmed);
    std::string clean = bg ? _removeBackground(trimmed) : trimmed;
    std::vector<std::string> args = _split(clean);
    if (args.empty()) return nullptr;
    std::string cmd_name = args[0];

    if (cmd_name == "chprompt")  return new ChpromptCommand(trimmed);
    if (cmd_name == "showpid")   return new ShowPidCommand(trimmed);
    if (cmd_name == "pwd")       return new GetCurrDirCommand(trimmed);
    if (cmd_name == "cd")        return new ChangeDirCommand(trimmed);
    if (cmd_name == "jobs")      return new JobsCommand(trimmed);
    if (cmd_name == "fg")        return new ForegroundCommand(trimmed);
    if (cmd_name == "quit")      return new QuitCommand(trimmed);
    if (cmd_name == "kill")      return new KillCommand(trimmed);
    if (cmd_name == "alias")     return new AliasCommand(trimmed);
    if (cmd_name == "unalias")   return new UnaliasCommand(trimmed);
    if (cmd_name == "unsetenv")  return new UnsetenvCommand(trimmed);
    if (cmd_name == "sysinfo")   return new SysinfoCommand(trimmed);
    if (cmd_name == "du")        return new DuCommand(trimmed);
    if (cmd_name == "whoami")    return new WhoamiCommand(trimmed);
    if (cmd_name == "usbinfo")   return new UsbinfoCommand(trimmed);

    return new ExternalCommand(trimmed, bg);
}

void SmashShell::executeCommand(const std::string& cmd_line) {
    std::string trimmed = _trim(cmd_line);
    if (trimmed.empty()) return;

    // Remove finished jobs before executing any command
    jobs.removeFinishedJobs();

    // Resolve alias (only for non-alias, non-pipe, non-redirect commands)
    // Check first word
    std::string resolved = resolveAlias(trimmed);

    Command* cmd = createCommand(resolved);
    if (!cmd) return;
    cmd->execute();
    delete cmd;
}

// ==================== JobsList ====================

void JobsList::removeFinishedJobs() {
    auto it = jobs.begin();
    while (it != jobs.end()) {
        int status;
        pid_t ret = waitpid(it->pid, &status, WNOHANG);
        if (ret > 0 || (ret == -1 && errno == ECHILD)) {
            it = jobs.erase(it);
        } else {
            ++it;
        }
    }
}

void JobsList::addJob(pid_t pid, const std::string& cmd_line) {
    removeFinishedJobs();
    int new_id = getMaxJobId() + 1;
    jobs.push_back(JobEntry(new_id, pid, cmd_line));
}

int JobsList::getMaxJobId() const {
    int max_id = 0;
    for (auto& j : jobs)
        if (j.job_id > max_id) max_id = j.job_id;
    return max_id;
}

void JobsList::removeJobById(int job_id) {
    auto it = std::find_if(jobs.begin(), jobs.end(), [job_id](const JobEntry& j){ return j.job_id == job_id; });
    if (it != jobs.end()) jobs.erase(it);
}

JobEntry* JobsList::getJobById(int job_id) {
    for (auto& j : jobs)
        if (j.job_id == job_id) return &j;
    return nullptr;
}

JobEntry* JobsList::getLastJob() {
    if (jobs.empty()) return nullptr;
    JobEntry* last = nullptr;
    for (auto& j : jobs)
        if (!last || j.job_id > last->job_id) last = &j;
    return last;
}

void JobsList::printJobsList() {
    removeFinishedJobs();
    // Sort by job_id
    std::vector<JobEntry> sorted = jobs;
    std::sort(sorted.begin(), sorted.end(), [](const JobEntry& a, const JobEntry& b){ return a.job_id < b.job_id; });
    for (auto& j : sorted)
        std::cout << "[" << j.job_id << "] " << j.cmd_line << std::endl;
}

void JobsList::killAllJobs() {
    removeFinishedJobs();
    std::cout << "smash: sending SIGKILL signal to " << jobs.size() << " jobs:" << std::endl;
    std::vector<JobEntry> sorted = jobs;
    std::sort(sorted.begin(), sorted.end(), [](const JobEntry& a, const JobEntry& b){ return a.job_id < b.job_id; });
    for (auto& j : sorted) {
        std::cout << j.pid << ": " << j.cmd_line << std::endl;
        kill(j.pid, SIGKILL);
    }
}

// ==================== ChpromptCommand ====================

void ChpromptCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);
    if (args.size() <= 1) {
        smash.setPrompt("smash");
    } else {
        smash.setPrompt(args[1]);
    }
}

// ==================== ShowPidCommand ====================

void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << std::endl;
}

// ==================== GetCurrDirCommand ====================

void GetCurrDirCommand::execute() {
    char buf[PATH_MAX];
    if (getcwd(buf, PATH_MAX) == nullptr) {
        perror("smash error: getcwd failed");
        return;
    }
    std::cout << buf << std::endl;
}

// ==================== ChangeDirCommand ====================

void ChangeDirCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() > 2) {
        std::cerr << "smash error: cd: too many arguments" << std::endl;
        return;
    }
    if (args.size() == 1) return; // no argument, no impact

    std::string path = args[1];

    char cwd[PATH_MAX];
    if (getcwd(cwd, PATH_MAX) == nullptr) {
        perror("smash error: getcwd failed");
        return;
    }

    if (path == "-") {
        if (smash.getLastPwd().empty()) {
            std::cerr << "smash error: cd: OLDPWD not set" << std::endl;
            return;
        }
        std::string old = smash.getLastPwd();
        smash.setLastPwd(std::string(cwd));
        if (chdir(old.c_str()) == -1) {
            perror("smash error: chdir failed");
            smash.setLastPwd(std::string(cwd)); // revert
            return;
        }
    } else {
        std::string old_last = smash.getLastPwd();
        if (chdir(path.c_str()) == -1) {
            perror("smash error: chdir failed");
            return;
        }
        smash.setLastPwd(std::string(cwd));
    }
}

// ==================== JobsCommand ====================

void JobsCommand::execute() {
    SmashShell::getInstance().getJobs().printJobsList();
}

// ==================== ForegroundCommand ====================

void ForegroundCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() > 2) {
        std::cerr << "smash error: fg: invalid arguments" << std::endl;
        return;
    }

    JobsList& jobs = smash.getJobs();
    jobs.removeFinishedJobs();

    JobEntry* job = nullptr;

    if (args.size() == 1) {
        // no job_id: take max
        if (jobs.isEmpty()) {
            std::cerr << "smash error: fg: jobs list is empty" << std::endl;
            return;
        }
        job = jobs.getLastJob();
    } else {
        std::string id_str = args[1];
        if (!_isNumber(id_str) || id_str[0] == '-') {
            std::cerr << "smash error: fg: invalid arguments" << std::endl;
            return;
        }
        int job_id = std::stoi(id_str);
        job = jobs.getJobById(job_id);
        if (!job) {
            std::cerr << "smash error: fg: job-id " << job_id << " does not exist" << std::endl;
            return;
        }
    }

    std::cout << job->cmd_line << " " << job->pid << std::endl;
    pid_t pid = job->pid;
    std::string job_cmd = job->cmd_line;
    jobs.removeJobById(job->job_id);

    smash.setFgPid(pid);
    smash.setFgCmd(job_cmd);

    if (waitpid(pid, nullptr, 0) == -1 && errno != ECHILD) {
        perror("smash error: waitpid failed");
    }

    smash.setFgPid(-1);
    smash.setFgCmd("");
}

// ==================== QuitCommand ====================

void QuitCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    bool do_kill = (args.size() >= 2 && args[1] == "kill");
    if (do_kill) {
        smash.getJobs().killAllJobs();
    }
    exit(0);
}

// ==================== KillCommand ====================

void KillCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() != 3) {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }

    std::string sigarg = args[1];
    std::string jobarg = args[2];

    // sigarg must start with '-' followed by digits
    if (sigarg.empty() || sigarg[0] != '-') {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }
    std::string signum_str = sigarg.substr(1);
    if (!_isNumber(signum_str)) {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }
    if (!_isNumber(jobarg)) {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }

    int signum = std::stoi(signum_str);
    int job_id = std::stoi(jobarg);

    JobEntry* job = smash.getJobs().getJobById(job_id);
    if (!job) {
        std::cerr << "smash error: kill: job-id " << job_id << " does not exist" << std::endl;
        return;
    }

    pid_t pid = job->pid;
    if (kill(pid, signum) == -1) {
        perror("smash error: kill failed");
        return;
    }
    std::cout << "signal number " << signum << " was sent to pid " << pid << std::endl;
}

// ==================== AliasCommand ====================

void AliasCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);

    // If just "alias" with no args, list all
    std::vector<std::string> args = _split(clean);
    if (args.size() == 1) {
        for (auto& name : smash.getAliasOrder()) {
            auto it = smash.getAliases().find(name);
            if (it != smash.getAliases().end())
                std::cout << name << "='" << it->second << "'" << std::endl;
        }
        return;
    }

    // Validate format using regex over the stripped command
    // ^alias [a-zA-Z0-9_]+='[^']*'$
    std::regex alias_regex("^alias [a-zA-Z0-9_]+='[^']*'$");
    if (!std::regex_match(clean, alias_regex)) {
        std::cerr << "smash error: alias: invalid alias format" << std::endl;
        return;
    }

    // Parse name and command
    // Format: alias name='cmd'
    size_t eq_pos = clean.find('=');
    // name is between first space and '='
    size_t space_pos = clean.find(' ');
    std::string name = clean.substr(space_pos + 1, eq_pos - space_pos - 1);
    // cmd is between first ' and last '
    size_t first_quote = clean.find('\'', eq_pos);
    size_t last_quote  = clean.rfind('\'');
    std::string cmd_val = clean.substr(first_quote + 1, last_quote - first_quote - 1);

    // Check reserved keywords or already existing alias
    if (smash.isReservedKeyword(name) || smash.aliasExists(name)) {
        std::cerr << "smash error: alias: " << name << " already exists or is a reserved command" << std::endl;
        return;
    }

    smash.addAlias(name, cmd_val);
}

// ==================== UnaliasCommand ====================

void UnaliasCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() == 1) {
        std::cerr << "smash error: unalias: not enough arguments" << std::endl;
        return;
    }

    for (size_t i = 1; i < args.size(); i++) {
        if (!smash.aliasExists(args[i])) {
            std::cerr << "smash error: unalias: " << args[i] << " alias does not exist" << std::endl;
            return;
        }
        smash.removeAlias(args[i]);
    }
}

// ==================== UnsetenvCommand ====================

static bool _envVarExists(const std::string& var_name) {
    // Open /proc/self/environ and scan NUL-separated entries
    int fd = open("/proc/self/environ", O_RDONLY);
    if (fd == -1) return false;

    char buf[65536];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';

    std::string prefix = var_name + "=";
    size_t i = 0;
    while (i < (size_t)n) {
        std::string entry(buf + i);
        if (entry.substr(0, prefix.size()) == prefix) return true;
        i += entry.size() + 1;
    }
    return false;
}

void UnsetenvCommand::execute() {
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() == 1) {
        std::cerr << "smash error: unsetenv: not enough arguments" << std::endl;
        return;
    }

    for (size_t i = 1; i < args.size(); i++) {
        if (!_envVarExists(args[i])) {
            std::cerr << "smash error: unsetenv: " << args[i] << " does not exist" << std::endl;
            return;
        }
        // Remove from __environ directly
        std::string prefix = args[i] + "=";
        for (int j = 0; __environ[j] != nullptr; j++) {
            if (std::string(__environ[j]).substr(0, prefix.size()) == prefix) {
                // Shift rest up
                int k = j;
                while (__environ[k] != nullptr) {
                    __environ[k] = __environ[k + 1];
                    k++;
                }
                break;
            }
        }
    }
}

// ==================== SysinfoCommand ====================

void SysinfoCommand::execute() {
    struct utsname uts;
    if (uname(&uts) == -1) {
        perror("smash error: uname failed");
        return;
    }

    std::cout << "System: " << uts.sysname << std::endl;
    std::cout << "Hostname: " << uts.nodename << std::endl;
    std::cout << "Kernel: " << uts.release << std::endl;
    std::cout << "Architecture: " << uts.machine << std::endl;

    // Boot time from /proc/stat
    int fd = open("/proc/stat", O_RDONLY);
    if (fd == -1) {
        perror("smash error: open failed");
        return;
    }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    // Find "btime <seconds>"
    std::string content(buf);
    size_t pos = content.find("btime ");
    if (pos == std::string::npos) return;
    pos += 6;
    size_t end = content.find('\n', pos);
    std::string btime_str = content.substr(pos, end - pos);
    time_t btime = (time_t)std::stol(btime_str);

    struct tm* tm_info = localtime(&btime);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    std::cout << "Boot Time: " << time_buf << std::endl;
}

// ==================== ExternalCommand ====================

void ExternalCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string effective = is_background ? _removeBackground(cmd_line) : _trim(cmd_line);
    bool is_complex = (effective.find('*') != std::string::npos || effective.find('?') != std::string::npos);

    pid_t pid = fork();
    if (pid == -1) {
        perror("smash error: fork failed");
        return;
    }
    if (pid == 0) {
        // Child
        setpgrp();
        if (is_complex) {
            char* argv[] = { (char*)"/bin/bash", (char*)"-c", (char*)effective.c_str(), nullptr };
            execv("/bin/bash", argv);
        } else {
            std::vector<std::string> tokens = _split(effective);
            char** argv = _buildArgv(tokens);
            execvp(argv[0], argv);
            // If execvp fails:
            perror("smash error: execvp failed");
            // free not needed since we exit
            exit(1);
        }
        perror("smash error: execv failed");
        exit(1);
    } else {
        // Parent
        if (is_background) {
            smash.getJobs().addJob(pid, cmd_line);
        } else {
            smash.setFgPid(pid);
            smash.setFgCmd(effective);
            if (waitpid(pid, nullptr, 0) == -1 && errno != ECHILD) {
                perror("smash error: waitpid failed");
            }
            smash.setFgPid(-1);
            smash.setFgCmd("");
        }
    }
}

// ==================== RedirectionCommand ====================

void RedirectionCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string trimmed = _trim(cmd_line);

    std::string sep = append ? ">>" : ">";
    size_t sep_pos = std::string::npos;
    if (append) {
        sep_pos = trimmed.find(">>");
    } else {
        // find > that is not >>
        size_t pos = 0;
        while (pos < trimmed.size()) {
            size_t f = trimmed.find('>', pos);
            if (f == std::string::npos) break;
            if (f + 1 < trimmed.size() && trimmed[f+1] == '>') {
                pos = f + 2;
                continue;
            }
            sep_pos = f;
            break;
        }
    }
    if (sep_pos == std::string::npos) return;

    std::string left_cmd = _trim(trimmed.substr(0, sep_pos));
    std::string filename = _trim(trimmed.substr(sep_pos + sep.size()));

    // Resolve alias in left_cmd
    left_cmd = smash.resolveAlias(left_cmd);

    pid_t pid = fork();
    if (pid == -1) {
        perror("smash error: fork failed");
        return;
    }
    if (pid == 0) {
        setpgrp();
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(filename.c_str(), flags, 0666);
        if (fd == -1) {
            perror("smash error: open failed");
            exit(1);
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("smash error: dup2 failed");
            exit(1);
        }
        close(fd);

        // Now execute left_cmd
        // Check if complex
        bool is_complex = (left_cmd.find('*') != std::string::npos || left_cmd.find('?') != std::string::npos);
        if (is_complex) {
            char* argv[] = { (char*)"/bin/bash", (char*)"-c", (char*)left_cmd.c_str(), nullptr };
            execv("/bin/bash", argv);
            perror("smash error: execv failed");
            exit(1);
        }

        // Check if it's a built-in command: execute directly in this process
        std::vector<std::string> args = _split(left_cmd);
        if (!args.empty()) {
            std::string cmd_name = args[0];
            // For built-in commands in redirection, we execute them and exit
            static const std::vector<std::string> builtins = {
                "chprompt","showpid","pwd","cd","jobs","fg","quit","kill",
                "alias","unalias","unsetenv","sysinfo","du","whoami","usbinfo"
            };
            bool is_builtin = false;
            for (auto& b : builtins)
                if (b == cmd_name) { is_builtin = true; break; }

            if (is_builtin) {
                Command* cmd = smash.createCommand(left_cmd);
                if (cmd) { cmd->execute(); delete cmd; }
                exit(0);
            }
        }

        // External
        std::vector<std::string> tokens = _split(left_cmd);
        char** argv = _buildArgv(tokens);
        execvp(argv[0], argv);
        perror("smash error: execvp failed");
        exit(1);
    } else {
        smash.setFgPid(pid);
        smash.setFgCmd(left_cmd);
        if (waitpid(pid, nullptr, 0) == -1 && errno != ECHILD) {
            perror("smash error: waitpid failed");
        }
        smash.setFgPid(-1);
        smash.setFgCmd("");
    }
}

// ==================== PipeCommand ====================

void PipeCommand::execute() {
    SmashShell& smash = SmashShell::getInstance();
    std::string trimmed = _trim(cmd_line);

    std::string sep = stderr_pipe ? "|&" : "|";
    size_t sep_pos = std::string::npos;
    if (stderr_pipe) {
        sep_pos = trimmed.find("|&");
    } else {
        size_t pos = 0;
        while (pos < trimmed.size()) {
            size_t f = trimmed.find('|', pos);
            if (f == std::string::npos) break;
            if (f + 1 < trimmed.size() && trimmed[f+1] == '&') {
                pos = f + 2;
                continue;
            }
            sep_pos = f;
            break;
        }
    }
    if (sep_pos == std::string::npos) return;

    std::string cmd1 = _trim(trimmed.substr(0, sep_pos));
    std::string cmd2 = _trim(trimmed.substr(sep_pos + sep.size()));

    // Resolve aliases
    cmd1 = smash.resolveAlias(cmd1);
    cmd2 = smash.resolveAlias(cmd2);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("smash error: pipe failed");
        return;
    }

    // Fork cmd1
    pid_t pid1 = fork();
    if (pid1 == -1) {
        perror("smash error: fork failed");
        close(pipefd[0]); close(pipefd[1]);
        return;
    }
    if (pid1 == 0) {
        setpgrp();
        close(pipefd[0]);
        int target_fd = stderr_pipe ? STDERR_FILENO : STDOUT_FILENO;
        if (dup2(pipefd[1], target_fd) == -1) {
            perror("smash error: dup2 failed");
            exit(1);
        }
        close(pipefd[1]);

        bool is_complex = (cmd1.find('*') != std::string::npos || cmd1.find('?') != std::string::npos);
        if (is_complex) {
            char* argv[] = { (char*)"/bin/bash", (char*)"-c", (char*)cmd1.c_str(), nullptr };
            execv("/bin/bash", argv);
            perror("smash error: execv failed");
            exit(1);
        }
        // Check built-in
        std::vector<std::string> args = _split(cmd1);
        if (!args.empty()) {
            std::string cn = args[0];
            static const std::vector<std::string> builtins = {
                "chprompt","showpid","pwd","cd","jobs","fg","quit","kill",
                "alias","unalias","unsetenv","sysinfo","du","whoami","usbinfo"
            };
            bool is_builtin = false;
            for (auto& b : builtins) if (b == cn) { is_builtin = true; break; }
            if (is_builtin) {
                Command* cmd = smash.createCommand(cmd1);
                if (cmd) { cmd->execute(); delete cmd; }
                exit(0);
            }
        }
        std::vector<std::string> tokens = _split(cmd1);
        char** argv = _buildArgv(tokens);
        execvp(argv[0], argv);
        perror("smash error: execvp failed");
        exit(1);
    }

    // Fork cmd2
    pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("smash error: fork failed");
        close(pipefd[0]); close(pipefd[1]);
        return;
    }
    if (pid2 == 0) {
        setpgrp();
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("smash error: dup2 failed");
            exit(1);
        }
        close(pipefd[0]);

        bool is_complex = (cmd2.find('*') != std::string::npos || cmd2.find('?') != std::string::npos);
        if (is_complex) {
            char* argv[] = { (char*)"/bin/bash", (char*)"-c", (char*)cmd2.c_str(), nullptr };
            execv("/bin/bash", argv);
            perror("smash error: execv failed");
            exit(1);
        }
        std::vector<std::string> tokens = _split(cmd2);
        char** argv = _buildArgv(tokens);
        execvp(argv[0], argv);
        perror("smash error: execvp failed");
        exit(1);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    // Wait for both
    smash.setFgPid(pid1);
    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);
    smash.setFgPid(-1);
    smash.setFgCmd("");
}

// ==================== DuCommand ====================

static long long _duRecursive(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) == -1) return 0;

    if (S_ISLNK(st.st_mode)) return 0; // don't follow symlinks

    long long total = st.st_blocks * 512; // st_blocks is in 512-byte units

    if (S_ISDIR(st.st_mode)) {
        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd == -1) return total;

        char buf[4096];
        while (true) {
            int nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
            if (nread <= 0) break;
            for (int bpos = 0; bpos < nread;) {
                struct linux_dirent64* d = (struct linux_dirent64*)(buf + bpos);
                std::string name(d->d_name);
                bpos += d->d_reclen;
                if (name == "." || name == "..") continue;
                total += _duRecursive(path + "/" + name);
            }
        }
        close(fd);
    }
    return total;
}

void DuCommand::execute() {
    std::string clean = _removeBackground(cmd_line);
    std::vector<std::string> args = _split(clean);

    if (args.size() > 2) {
        std::cerr << "smash error: du: too many arguments" << std::endl;
        return;
    }

    std::string path;
    if (args.size() == 1) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, PATH_MAX) == nullptr) {
            perror("smash error: getcwd failed");
            return;
        }
        path = std::string(cwd);
    } else {
        path = args[1];
    }

    long long bytes = _duRecursive(path);
    // Convert to KB, rounding up
    long long kb = (bytes + 1023) / 1024;
    std::cout << "Total disk usage: " << kb << " KB" << std::endl;
}

// ==================== WhoamiCommand ====================

void WhoamiCommand::execute() {
    uid_t uid = getuid();
    gid_t gid = getgid();

    // Get username and home dir from /etc/passwd manually
    // Per FAQ: getpwuid() is not allowed as it abstracts what we implement
    // Read /proc/self/status for uid, then parse /etc/passwd
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd == -1) {
        perror("smash error: open failed");
        return;
    }
    char buf[65536];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    std::string content(buf);
    std::string username = "N/A", homedir = "N/A";

    // Parse /etc/passwd: username:x:uid:gid:gecos:homedir:shell
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        std::vector<std::string> fields;
        std::istringstream ls(line);
        std::string field;
        while (std::getline(ls, field, ':'))
            fields.push_back(field);
        if (fields.size() >= 7) {
            uid_t file_uid = (uid_t)std::stoul(fields[2]);
            if (file_uid == uid) {
                username = fields[0];
                homedir = fields[5];
                break;
            }
        }
    }

    std::cout << uid << std::endl;
    std::cout << gid << std::endl;
    std::cout << username << " " << homedir << std::endl;
}

// ==================== UsbinfoCommand ====================

void UsbinfoCommand::execute() {
    // Read from /sys/bus/usb/devices/
    // For each device, read devnum, idVendor, idProduct, manufacturer, product, bMaxPower
    // Use open/read syscalls only

    std::string base = "/sys/bus/usb/devices";
    int base_fd = open(base.c_str(), O_RDONLY | O_DIRECTORY);
    if (base_fd == -1) {
        std::cerr << "smash error: usbinfo: no USB devices found" << std::endl;
        return;
    }

    // Collect device entries
    struct DevInfo {
        int devnum;
        std::string vendor, product_id, manufacturer, product_name, max_power;
    };
    std::vector<DevInfo> devices;

    char buf[4096];
    while (true) {
        int nread = syscall(SYS_getdents64, base_fd, buf, sizeof(buf));
        if (nread <= 0) break;
        for (int bpos = 0; bpos < nread;) {
            struct linux_dirent64* d = (struct linux_dirent64*)(buf + bpos);
            std::string name(d->d_name);
            bpos += d->d_reclen;
            if (name == "." || name == "..") continue;
            // Skip non-device entries (interfaces like 1-1:1.0 contain ':')
            if (name.find(':') != std::string::npos) continue;

            std::string dev_path = base + "/" + name;

            auto read_file = [](const std::string& path) -> std::string {
                int f = open(path.c_str(), O_RDONLY);
                if (f == -1) return "N/A";
                char b[256];
                ssize_t n = read(f, b, sizeof(b) - 1);
                close(f);
                if (n <= 0) return "N/A";
                b[n] = '\0';
                // Trim trailing newline
                std::string s(b);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                    s.pop_back();
                return s.empty() ? "N/A" : s;
            };

            std::string devnum_str = read_file(dev_path + "/devnum");
            if (devnum_str == "N/A") continue;
            int devnum = 0;
            try { devnum = std::stoi(devnum_str); } catch(...) { continue; }

            DevInfo di;
            di.devnum = devnum;
            di.vendor      = read_file(dev_path + "/idVendor");
            di.product_id  = read_file(dev_path + "/idProduct");
            di.manufacturer= read_file(dev_path + "/manufacturer");
            di.product_name= read_file(dev_path + "/product");
            std::string mp = read_file(dev_path + "/bMaxPower");
            // Remove "mA" suffix if present
            if (mp != "N/A") {
                size_t ma_pos = mp.find("mA");
                if (ma_pos != std::string::npos) mp = mp.substr(0, ma_pos);
                mp = _trim(mp);
            }
            di.max_power = mp;
            devices.push_back(di);
        }
    }
    close(base_fd);

    if (devices.empty()) {
        std::cerr << "smash error: usbinfo: no USB devices found" << std::endl;
        return;
    }

    std::sort(devices.begin(), devices.end(), [](const DevInfo& a, const DevInfo& b){ return a.devnum < b.devnum; });

    for (auto& di : devices) {
        std::cout << "Device " << di.devnum << ": ID " << di.vendor << ":" << di.product_id
                  << " " << di.manufacturer << " " << di.product_name
                  << "  MaxPower: " << di.max_power << "mA" << std::endl;
    }
}
