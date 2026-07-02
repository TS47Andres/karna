#pragma once

#include <string>
#include <vector>
#include <optional>

class FileOps {
public:
    static std::optional<std::string> read_file(const std::string& path);
    static bool write_file(const std::string& path, const std::string& content);
    static bool append_file(const std::string& path, const std::string& content);
    static bool create_directory(const std::string& path);
    static bool file_exists(const std::string& path);
    static bool remove_file(const std::string& path);
    static std::vector<std::string> list_directory(const std::string& path);
};
