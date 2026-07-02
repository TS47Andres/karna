#include "process/runner.h"

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
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

ProcessResult ProcessRunner::run(
    const std::string& command,
    const std::string& working_dir,
    int timeout_ms)
{
    ProcessResult result;

#ifdef _WIN32
    std::string cmd = "cmd.exe /c " + command;

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
        result.stderr_str = "Failed to create process";
        return result;
    }

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.timed_out = true;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);

    char buf[4096];
    DWORD bytes_read;

    while (ReadFile(hStdoutRead, buf, sizeof(buf) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        result.stdout_str += buf;
    }

    while (ReadFile(hStderrRead, buf, sizeof(buf) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        result.stderr_str += buf;
    }

    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
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

    while (!done && !result.timed_out) {
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
                    result.stdout_str += buf;
                }
            }

            if (FD_ISSET(stderr_pipe[0], &read_fds)) {
                n = read(stderr_pipe[0], buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    result.stderr_str += buf;
                }
            }
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
            kill(pid, SIGTERM);
            result.timed_out = true;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
#endif

    return result;
}
