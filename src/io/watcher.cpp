#include "io/watcher.h"

#include <filesystem>
#include <unordered_map>
#include <chrono>

namespace fs = std::filesystem;

FileWatcher::FileWatcher(std::string path, std::chrono::milliseconds delay)
    : path_(std::move(path)), delay_(delay)
{}

FileWatcher::~FileWatcher()
{
    stop();
}

static std::unordered_map<std::string, fs::file_time_type> snapshot_directory(const fs::path& dir)
{
    std::unordered_map<std::string, fs::file_time_type> snap;
    if (!fs::exists(dir)) return snap;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            snap[entry.path().string()] = entry.last_write_time();
        }
    }
    return snap;
}

void FileWatcher::start(std::function<void(const std::string&)> on_change)
{
    if (running_) return;
    running_ = true;

    watch_thread_ = std::thread([this, on_change = std::move(on_change)]() {
        auto last_snapshot = snapshot_directory(path_);

        while (running_) {
            std::this_thread::sleep_for(delay_);

            auto current_snapshot = snapshot_directory(path_);

            for (const auto& [p, time] : current_snapshot) {
                auto it = last_snapshot.find(p);
                if (it == last_snapshot.end() || it->second != time) {
                    on_change(p);
                }
            }

            last_snapshot = std::move(current_snapshot);
        }
    });
}

void FileWatcher::stop()
{
    running_ = false;
    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }
}
