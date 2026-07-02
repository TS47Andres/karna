#include "io/file_ops.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

std::optional<std::string> FileOps::read_file(const std::string& path)
{
    if (!fs::exists(path)) return std::nullopt;

    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

bool FileOps::write_file(const std::string& path, const std::string& content)
{
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << content;
    return true;
}

bool FileOps::append_file(const std::string& path, const std::string& content)
{
    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) return false;

    file << content;
    return true;
}

bool FileOps::create_directory(const std::string& path)
{
    return fs::create_directories(path);
}

bool FileOps::file_exists(const std::string& path)
{
    return fs::exists(path);
}

bool FileOps::remove_file(const std::string& path)
{
    return fs::remove(path);
}

std::vector<std::string> FileOps::list_directory(const std::string& path)
{
    std::vector<std::string> entries;
    if (!fs::exists(path)) return entries;

    for (const auto& entry : fs::directory_iterator(path)) {
        entries.push_back(entry.path().string());
    }

    std::sort(entries.begin(), entries.end());
    return entries;
}
