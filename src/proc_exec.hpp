#pragma once
#include <string>
#include <sys/types.h>
#include <vector>

struct ExecResult {
    int exit_code = -1;
    std::string stdout_text;
};

ExecResult exec_capture(const std::vector<std::string>& argv);

pid_t exec_spawn(const std::vector<std::string>& argv);
