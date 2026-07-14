#include "net/curl_setup.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::filesystem::path executable_directory()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0) {
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

} // namespace

void configure_curl_ssl(CURL* curl)
{
    if (!curl) {
        return;
    }

    if (const char* override_bundle = std::getenv("CURL_CA_BUNDLE");
        override_bundle && *override_bundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, override_bundle);
        return;
    }

    const auto bundled_bundle = executable_directory() / "cacert.pem";
    if (std::filesystem::is_regular_file(bundled_bundle)) {
        const std::string path = bundled_bundle.string();
        curl_easy_setopt(curl, CURLOPT_CAINFO, path.c_str());
    }
}
