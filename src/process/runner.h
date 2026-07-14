#pragma once

#include <string>
#include <vector>
#include <functional>

struct ProcessResult {
    std::string stdout_str;
    std::string stderr_str;
    int exit_code{0};
    bool timed_out{false};
    bool cancelled{false};
};

class ProcessRunner {
public:
    static ProcessResult run(
        const std::string& command,
        const std::string& working_dir = "",
        int timeout_ms = 60000,
        std::function<void(const std::string&)> on_output = {},
        std::function<bool()> should_cancel = {}
    );
};
