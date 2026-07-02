#pragma once

#include <string>
#include <vector>

struct ProcessResult {
    std::string stdout_str;
    std::string stderr_str;
    int exit_code{0};
    bool timed_out{false};
};

class ProcessRunner {
public:
    static ProcessResult run(
        const std::string& command,
        const std::string& working_dir = "",
        int timeout_ms = 30000
    );
};
