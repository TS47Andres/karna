#include "tools/search.h"
#include "net/curl_setup.h"

#include <curl/curl.h>
#include <sstream>

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total);
    return total;
}

SearchTool::SearchTool(ExaConfig config)
    : config_(std::move(config))
{}

std::string SearchTool::name() const { return "search"; }

std::string SearchTool::description() const
{
    return "Search the web for information. Uses Exa AI to find relevant web pages and return their contents. "
           "Use this when you need up-to-date information, documentation, or answers not in the training data.";
}

json SearchTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"query", {
                {"type", "string"},
                {"description", "Natural language search query describing what to find"}
            }},
            {"num_results", {
                {"type", "integer"},
                {"description", "Number of results to return (1-25)"},
                {"default", 5}
            }}
        }},
        {"required", {"query"}}
    };
}

ToolResult SearchTool::execute(const json& params)
{
    std::string query = params["query"].get<std::string>();
    int num_results = params.value("num_results", 5);
    return perform_exa_search(query, num_results);
}

ToolResult SearchTool::perform_exa_search(const std::string& query, int num_results) const
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ToolResult::fail("Failed to initialize HTTP client");
    }
    configure_curl_ssl(curl);

    json body;
    body["query"] = query;
    body["numResults"] = num_results;
    body["type"] = "auto";
    body["contents"] = json::object();
    body["contents"]["text"] = json::object();
    body["contents"]["text"]["maxCharacters"] = 3000;

    std::string body_str = body.dump();
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "x-api-key: " + config_.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.exa.ai/search");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "karna/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return ToolResult::fail("Search request failed: " + std::string(curl_easy_strerror(res)));
    }

    try {
        auto j = json::parse(response);

        if (j.contains("results") && !j["results"].empty()) {
            std::ostringstream out;
            int rank = 1;
            for (const auto& r : j["results"]) {
                out << "Result " << rank << ":\n";
                out << "  Title: " << r.value("title", "(no title)") << "\n";
                out << "  URL: " << r.value("url", "(no url)") << "\n";
                if (r.contains("publishedDate") && !r["publishedDate"].is_null()) {
                    out << "  Published: " << r["publishedDate"].get<std::string>() << "\n";
                }
                if (r.contains("author") && !r["author"].is_null()) {
                    out << "  Author: " << r["author"].get<std::string>() << "\n";
                }
                if (r.contains("text") && !r["text"].is_null()) {
                    std::string text = r["text"].get<std::string>();
                    if (text.size() > 2000) {
                        text = text.substr(0, 2000) + "... [truncated]";
                    }
                    out << "  Content:\n" << text << "\n";
                }
                out << "\n";
                ++rank;
            }
            return ToolResult::ok(out.str());
        }

        if (j.contains("error")) {
            return ToolResult::fail("Search API error: " + j["error"].dump());
        }

        return ToolResult::ok("No results found for: " + query);
    } catch (const std::exception& e) {
        return ToolResult::fail("Failed to parse search response: " + std::string(e.what()));
    }
}
