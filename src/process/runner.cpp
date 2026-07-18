#include "process/runner.h"

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>

namespace {
std::string quote_windows_argument(const std::string& argument)
{
    std::string quoted = "\"";
    size_t backslashes = 0;
    for (const char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '\"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('\"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(ch);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('\"');
    return quoted;
}
}
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#endif

#include <thread>
#include <chrono>
#include <cstdio>
#include <memory>
#include <algorithm>

namespace {
constexpr int kDefaultTimeoutMs = 60 * 1000;
}

ProcessResult ProcessRunner::run(
    const std::string& command,
    const std::string& working_dir,
    int timeout_ms,
    std::function<void(const std::string&)> on_output,
    std::function<bool()> should_cancel)
{
    ProcessResult result;
    if (timeout_ms <= 0) {
        timeout_ms = kDefaultTimeoutMs;
    }

#ifdef _WIN32
    std::string cmd = "powershell.exe -NoLogo -NoProfile -NonInteractive -Command " +
        quote_windows_argument(command);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE hStdoutRead, hStdoutWrite;
    HANDLE hStderrRead, hStderrWrite;

    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::string workdir = working_dir.empty() ? "." : working_dir;

    char* cmd_cstr = const_cast<char*>(cmd.c_str());

    if (!CreateProcessA(nullptr, cmd_cstr, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr,
                        workdir.empty() ? nullptr : workdir.c_str(),
                        &si, &pi)) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        result.exit_code = -1;
        result.stderr_str = "Failed to create PowerShell process";
        return result;
    }

    // Keep the entire command tree together so cancelling a command also
    // terminates PowerShell and its descendants.
    HANDLE hJob = CreateJobObjectA(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(hJob, pi.hProcess)) {
            CloseHandle(hJob);
            hJob = nullptr;
        }
    }

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    const auto read_available = [&](HANDLE pipe, std::string& target) {
        char buf[4096];
        DWORD available = 0;
        while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD bytes_read = 0;
            const DWORD request = (std::min)(available, static_cast<DWORD>(sizeof(buf) - 1));
            if (!ReadFile(pipe, buf, request, &bytes_read, nullptr) || bytes_read == 0) {
                break;
            }
            buf[bytes_read] = '\0';
            std::string chunk(buf, bytes_read);
            target += chunk;
            if (on_output) {
                on_output(chunk);
            }
        }
    };

    const auto start_time = std::chrono::steady_clock::now();
    bool process_done = false;
    while (!process_done) {
        read_available(hStdoutRead, result.stdout_str);
        read_available(hStderrRead, result.stderr_str);

        const DWORD wait_result = WaitForSingleObject(pi.hProcess, 0);
        if (wait_result == WAIT_OBJECT_0) {
            process_done = true;
        } else {
            if (should_cancel && should_cancel()) {
                if (hJob) {
                    TerminateJobObject(hJob, 1);
                } else {
                    TerminateProcess(pi.hProcess, 1);
                }
                result.cancelled = true;
                WaitForSingleObject(pi.hProcess, INFINITE);
                process_done = true;
                continue;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= timeout_ms) {
                if (hJob) {
                    TerminateJobObject(hJob, 1);
                } else {
                    TerminateProcess(pi.hProcess, 1);
                }
                result.timed_out = true;
                WaitForSingleObject(pi.hProcess, INFINITE);
                process_done = true;
            } else {
                Sleep(50);
            }
        }
    }

    read_available(hStdoutRead, result.stdout_str);
    read_available(hStderrRead, result.stderr_str);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);

    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    if (hJob) CloseHandle(hJob);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

#else
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        result.exit_code = -1;
        result.stderr_str = "Failed to create pipes";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.exit_code = -1;
        result.stderr_str = "Failed to fork";
        return result;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Put the shell and its descendants in their own process group so
        // cancellation can stop the complete command tree.
        setpgid(0, 0);

        if (!working_dir.empty()) {
            chdir(working_dir.c_str());
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    auto start_time = std::chrono::steady_clock::now();
    bool done = false;

    while (!done && !result.timed_out && !result.cancelled) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(stdout_pipe[0], &read_fds);
        FD_SET(stderr_pipe[0], &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        int ret = select(std::max(stdout_pipe[0], stderr_pipe[0]) + 1, &read_fds, nullptr, nullptr, &tv);

        if (ret > 0) {
            char buf[4096];
            ssize_t n;

            if (FD_ISSET(stdout_pipe[0], &read_fds)) {
                n = read(stdout_pipe[0], buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    std::string chunk(buf, static_cast<size_t>(n));
                    result.stdout_str += chunk;
                    if (on_output) on_output(chunk);
                }
            }

            if (FD_ISSET(stderr_pipe[0], &read_fds)) {
                n = read(stderr_pipe[0], buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    std::string chunk(buf, static_cast<size_t>(n));
                    result.stderr_str += chunk;
                    if (on_output) on_output(chunk);
                }
            }
        }

        if (should_cancel && should_cancel()) {
            kill(-pid, SIGTERM);
            result.cancelled = true;
            int status = 0;
            if (waitpid(pid, &status, 0) == pid) {
                if (WIFEXITED(status)) {
                    result.exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    result.exit_code = -WTERMSIG(status);
                }
            }
            continue;
        }

        int status;
        pid_t wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == pid) {
            done = true;
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = -WTERMSIG(status);
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeout_ms) {
            kill(-pid, SIGTERM);
            result.timed_out = true;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = -WTERMSIG(status);
            }
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
#endif

    return result;
}
