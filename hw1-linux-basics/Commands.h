#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <vector>
#include <string>
#include <map>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define WHITESPACE std::string(" \t\r\n")
#define JOBS_MAX_SIZE (100)

class Command {
protected:
    std::string cmd_line;
public:
    explicit Command(const std::string& cmd_line) : cmd_line(cmd_line) {}
    virtual ~Command() = default;
    virtual void execute() = 0;
    const std::string& getCmdLine() const { return cmd_line; }
};

class BuiltInCommand : public Command {
public:
    explicit BuiltInCommand(const std::string& cmd_line) : Command(cmd_line) {}
    virtual ~BuiltInCommand() = default;
};

class ExternalCommand : public Command {
    bool is_background;
public:
    ExternalCommand(const std::string& cmd_line, bool is_background)
        : Command(cmd_line), is_background(is_background) {}
    virtual ~ExternalCommand() = default;
    void execute() override;
    bool isBackground() const { return is_background; }
};

class PipeCommand : public Command {
    bool stderr_pipe; // true for |&
public:
    PipeCommand(const std::string& cmd_line, bool stderr_pipe)
        : Command(cmd_line), stderr_pipe(stderr_pipe) {}
    virtual ~PipeCommand() = default;
    void execute() override;
};

class RedirectionCommand : public Command {
    bool append; // true for >>
public:
    RedirectionCommand(const std::string& cmd_line, bool append)
        : Command(cmd_line), append(append) {}
    virtual ~RedirectionCommand() = default;
    void execute() override;
};

struct JobEntry {
    int job_id;
    pid_t pid;
    std::string cmd_line;
    JobEntry(int job_id, pid_t pid, const std::string& cmd_line)
        : job_id(job_id), pid(pid), cmd_line(cmd_line) {}
};

class JobsList {
    std::vector<JobEntry> jobs;
public:
    JobsList() = default;
    ~JobsList() = default;
    void addJob(pid_t pid, const std::string& cmd_line);
    void removeFinishedJobs();
    void removeJobById(int job_id);
    JobEntry* getJobById(int job_id);
    JobEntry* getLastJob();
    bool isEmpty() const { return jobs.empty(); }
    void printJobsList();
    void killAllJobs();
    int getMaxJobId() const;
    const std::vector<JobEntry>& getJobs() const { return jobs; }
};

class SmashShell {
    std::string prompt;
    std::string last_pwd;
    JobsList jobs;
    pid_t fg_pid;
    std::string fg_cmd;
    std::map<std::string, std::string> aliases; // name -> command
    std::vector<std::string> alias_order;       // insertion order

    static SmashShell* instance;
    SmashShell() : prompt("smash"), last_pwd(""), fg_pid(-1), fg_cmd("") {}

public:
    static SmashShell& getInstance() {
        if (!instance) instance = new SmashShell();
        return *instance;
    }
    ~SmashShell() = default;

    void executeCommand(const std::string& cmd_line);
    Command* createCommand(const std::string& cmd_line);

    // Getters/Setters
    const std::string& getPrompt() const { return prompt; }
    void setPrompt(const std::string& p) { prompt = p; }
    const std::string& getLastPwd() const { return last_pwd; }
    void setLastPwd(const std::string& p) { last_pwd = p; }
    JobsList& getJobs() { return jobs; }
    pid_t getFgPid() const { return fg_pid; }
    void setFgPid(pid_t pid) { fg_pid = pid; }
    const std::string& getFgCmd() const { return fg_cmd; }
    void setFgCmd(const std::string& cmd) { fg_cmd = cmd; }
    std::map<std::string, std::string>& getAliases() { return aliases; }
    std::vector<std::string>& getAliasOrder() { return alias_order; }

    // Alias helpers
    bool aliasExists(const std::string& name) const;
    void addAlias(const std::string& name, const std::string& cmd);
    void removeAlias(const std::string& name);
    std::string resolveAlias(const std::string& cmd_line) const;
    bool isReservedKeyword(const std::string& name) const;
};

// ==================== Built-in Commands ====================

class ChpromptCommand : public BuiltInCommand {
public:
    explicit ChpromptCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
public:
    explicit ShowPidCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
public:
    explicit GetCurrDirCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand {
public:
    explicit ChangeDirCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class JobsCommand : public BuiltInCommand {
public:
    explicit JobsCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class ForegroundCommand : public BuiltInCommand {
public:
    explicit ForegroundCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class QuitCommand : public BuiltInCommand {
public:
    explicit QuitCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class KillCommand : public BuiltInCommand {
public:
    explicit KillCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class AliasCommand : public BuiltInCommand {
public:
    explicit AliasCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class UnaliasCommand : public BuiltInCommand {
public:
    explicit UnaliasCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class UnsetenvCommand : public BuiltInCommand {
public:
    explicit UnsetenvCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class SysinfoCommand : public BuiltInCommand {
public:
    explicit SysinfoCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

// ==================== Special Commands ====================

class DuCommand : public BuiltInCommand {
public:
    explicit DuCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class WhoamiCommand : public BuiltInCommand {
public:
    explicit WhoamiCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

class UsbinfoCommand : public BuiltInCommand {
public:
    explicit UsbinfoCommand(const std::string& cmd_line) : BuiltInCommand(cmd_line) {}
    void execute() override;
};

// ==================== Helper Functions ====================
std::string _trim(const std::string& s);
std::vector<std::string> _split(const std::string& s, const std::string& delimiters = WHITESPACE);
bool _isNumber(const std::string& s);
std::string _removeBackground(const std::string& cmd_line);
bool _isBackground(const std::string& cmd_line);

#endif //SMASH_COMMAND_H_
