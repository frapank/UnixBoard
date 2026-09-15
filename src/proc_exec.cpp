#include "proc_exec.hpp"

#include <array>
#include <sys/wait.h>
#include <unistd.h>

namespace {

    std::vector<char*> to_c_argv(const std::vector<std::string>& argv)
    {
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv)
            c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        return c_argv;
    }

} // namespace

pid_t exec_spawn(const std::vector<std::string>& argv)
{
    pid_t pid = fork();
    if (pid == 0) {
        auto c_argv = to_c_argv(argv);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execvp(c_argv[0], c_argv.data());
        _exit(127);
    }
    return pid;
}

ExecResult exec_capture(const std::vector<std::string>& argv)
{
    ExecResult result;
    int out_pipe[2];
    if (pipe(out_pipe) != 0)
        return result;

    pid_t pid = fork();
    if (pid == 0) {
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);
        freopen("/dev/null", "w", stderr);
        auto c_argv = to_c_argv(argv);
        execvp(c_argv[0], c_argv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    std::array<char, 4096> buf{};
    ssize_t n;
    while ((n = read(out_pipe[0], buf.data(), buf.size())) > 0) {
        result.stdout_text.append(buf.data(), static_cast<size_t>(n));
    }
    close(out_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}
