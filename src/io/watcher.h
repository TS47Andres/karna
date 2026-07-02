#pragma once

#include <string>
#include <functional>
#include <thread>
#include <chrono>

class FileWatcher {
public:
    FileWatcher(std::string path, std::chrono::milliseconds delay = std::chrono::milliseconds(500));
    ~FileWatcher();

    void start(std::function<void(const std::string&)> on_change);
    void stop();

private:
    std::string path_;
    std::chrono::milliseconds delay_;
    std::thread watch_thread_;
    bool running_{false};
};
