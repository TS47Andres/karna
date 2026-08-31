#include "tui/chat_view.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef RGB
#undef RGB
#endif
#endif

using namespace ftxui;

namespace {

std::vector<std::string> split_lines(const std::string& content)
{
    std::vector<std::string> lines;
    if (content.empty()) return lines;
    std::stringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

// Bright foreground colors chosen for readability on the black terminal
// background. Keep these distinct from diff's semantic red/green colors.
Color tool_color(const std::string& tool_name)
{
    if (tool_name == "read") return Color::RGB(105, 195, 255);
    if (tool_name == "write") return Color::RGB(100, 235, 155);
    if (tool_name == "edit") return Color::RGB(255, 180, 90);
    if (tool_name == "bash") return Color::RGB(215, 145, 255);
    if (tool_name == "sub_agent") return Color::RGB(255, 135, 205);
    if (tool_name == "glob") return Color::RGB(75, 225, 195);
    if (tool_name == "grep") return Color::RGB(255, 220, 95);
    if (tool_name == "search") return Color::RGB(120, 165, 255);
    return Color::GrayLight;
}

// Load the bundled welcome artwork from the current working directory or source tree.
std::vector<std::string> load_welcome_art()
{
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path("assets") / "Karna.txt",
        std::filesystem::path("Karna.txt"),
#ifdef KARNA_SOURCE_DIR
        std::filesystem::path(KARNA_SOURCE_DIR) / "assets" / "Karna.txt",
#endif
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (!file) {
            continue;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(std::move(line));
        }
        return lines;
    }

    return {};
}

// Convert the artwork file into an FTXUI element without wrapping its lines.
Element render_welcome_art()
{
    static const std::vector<std::string> lines = load_welcome_art();
    if (lines.empty()) {
        return emptyElement();
    }

    Elements art_lines;
    art_lines.reserve(lines.size());
    for (const auto& line : lines) {
        art_lines.push_back(text(line));
    }

    return vbox(std::move(art_lines)) |
        color(Color::White) |
        hcenter;
}

struct WelcomeImage {
    int width;
    int height;
    std::vector<std::uint8_t> luminance;
};

// Decode the monochrome JPG through the native Windows image decoder.
std::shared_ptr<const WelcomeImage> load_welcome_image()
{
#ifdef _WIN32
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path("assets") / "Karna.jpg",
        std::filesystem::path("Karna.jpg"),
#ifdef KARNA_SOURCE_DIR
        std::filesystem::path(KARNA_SOURCE_DIR) / "assets" / "Karna.jpg",
#endif
    };

    std::filesystem::path image_path;
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            image_path = path;
            break;
        }
    }
    if (image_path.empty()) {
        return {};
    }

    Gdiplus::GdiplusStartupInput startup_input;
    ULONG_PTR startup_token = 0;
    if (Gdiplus::GdiplusStartup(&startup_token, &startup_input, nullptr) != Gdiplus::Ok) {
        return {};
    }

    const std::wstring image_path_string = image_path.wstring();
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(image_path_string.c_str());
    if (bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
        bitmap.reset();
        Gdiplus::GdiplusShutdown(startup_token);
        return {};
    }

    const int width = static_cast<int>(bitmap->GetWidth());
    const int height = static_cast<int>(bitmap->GetHeight());
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bitmap_data{};
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppARGB, &bitmap_data) != Gdiplus::Ok) {
        bitmap.reset();
        Gdiplus::GdiplusShutdown(startup_token);
        return {};
    }

    auto image = std::make_shared<WelcomeImage>();
    image->width = width;
    image->height = height;
    image->luminance.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    const int stride = std::abs(bitmap_data.Stride);
    const auto* pixels = static_cast<const std::uint8_t*>(bitmap_data.Scan0);
    for (int y = 0; y < height; ++y) {
        const int source_row = bitmap_data.Stride >= 0 ? y : height - 1 - y;
        const auto* row = pixels + static_cast<size_t>(source_row) * static_cast<size_t>(stride);
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + static_cast<size_t>(x) * 4;
            const int luminance =
                (299 * static_cast<int>(pixel[2]) +
                 587 * static_cast<int>(pixel[1]) +
                 114 * static_cast<int>(pixel[0])) / 1000;
            image->luminance[static_cast<size_t>(y) * static_cast<size_t>(width) +
                             static_cast<size_t>(x)] = static_cast<std::uint8_t>(luminance);
        }
    }

    bitmap->UnlockBits(&bitmap_data);
    bitmap.reset();
    Gdiplus::GdiplusShutdown(startup_token);
    return image;
#else
    return {};
#endif
}

// Render the JPG as a compact 32x16 terminal-cell Braille image.
Element render_welcome_visual()
{
    static const auto image = load_welcome_image();
    if (!image) {
        return render_welcome_art();
    }

    constexpr int kImageCellsWide = 32;
    constexpr int kImageCellsHigh = 16;
    constexpr int kCanvasWidth = kImageCellsWide * 2;
    constexpr int kCanvasHeight = kImageCellsHigh * 4;
    constexpr int kLuminanceThreshold = 96;

    return canvas(kCanvasWidth, kCanvasHeight, [](Canvas& target) {
        for (int y = 0; y < kCanvasHeight; ++y) {
            const int source_y = y * image->height / kCanvasHeight;
            for (int x = 0; x < kCanvasWidth; ++x) {
                const int source_x = x * image->width / kCanvasWidth;
                const auto value = image->luminance[
                    static_cast<size_t>(source_y) * static_cast<size_t>(image->width) +
                    static_cast<size_t>(source_x)];
                if (value >= kLuminanceThreshold) {
                    target.DrawPoint(x, y, true, Color::White);
                }
            }
        }
    }) | hcenter;
}

} // namespace

ChatView::ChatView()
{}

Component ChatView::build()
{
    auto renderer = Renderer([this] { return render(); });
    // Scroll events are handled by the app-level component so they never
    // compete with the focused input component. The view itself only renders
    // the chat and animates toward its requested scroll position.
    component_ = renderer;
    return component_;
}

void ChatView::focus()
{
    return;
}

void ChatView::add_message(const Message& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool was_empty = messages_.empty();
    std::string name = msg.name ? *msg.name : "";
    if (msg.display) {
        const auto& source = *msg.display;
        DisplayMessage display;
        display.role = msg.role;
        display.content = msg.content;
        display.tool_name = source.tool_name.empty() ? name : source.tool_name;
        display.tool_parameter = source.parameter;
        display.tool_extra = source.extra;

        switch (source.kind) {
            case MessageDisplayKind::Activity:
                display.content = source.label;
                display.tool_display = ToolDisplay::Activity;
                break;
            case MessageDisplayKind::Diff: {
                display.tool_display = ToolDisplay::Diff;
                display.content = source.parameter;
                display.before = source.before;
                display.after = source.after;
                const auto before_lines = split_lines(display.before);
                const auto after_lines = split_lines(display.after);
                size_t prefix = 0;
                while (prefix < before_lines.size() && prefix < after_lines.size() &&
                       before_lines[prefix] == after_lines[prefix]) {
                    ++prefix;
                }
                size_t suffix = 0;
                while (suffix < before_lines.size() - prefix &&
                       suffix < after_lines.size() - prefix &&
                       before_lines[before_lines.size() - 1 - suffix] ==
                           after_lines[after_lines.size() - 1 - suffix]) {
                    ++suffix;
                }
                display.added_lines = static_cast<int>(after_lines.size() - prefix - suffix);
                display.deleted_lines = static_cast<int>(before_lines.size() - prefix - suffix);
                display.tool_extra = "+" + std::to_string(display.added_lines) +
                    " -" + std::to_string(display.deleted_lines);
                break;
            }
            case MessageDisplayKind::Panel:
                display.tool_display = ToolDisplay::Bash;
                display.bash_success = source.success;
                break;
            case MessageDisplayKind::SubAgent:
                display.tool_name = "sub_agent";
                display.tool_display = ToolDisplay::SubAgent;
                display.subagent_mode = source.mode;
                display.subagent_task = source.task;
                display.subagent_latest_tool = source.latest_tool;
                display.subagent_tools_used = source.tools_used;
                display.subagent_transcript = source.transcript;
                display.subagent_success = source.success;
                break;
        }
        messages_.push_back(std::move(display));
    } else {
        messages_.push_back({msg.role, msg.content, name});
    }
    if (was_empty) {
        scroll_to_end_requested_ = true;
    }
}

void ChatView::append_to_last(const std::string& content)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!messages_.empty()) {
        messages_.back().content += content;
    }
}

void ChatView::add_delta(const Delta& delta)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (delta.content) {
        if (messages_.empty() || messages_.back().role != MessageRole::Assistant) {
            messages_.push_back({MessageRole::Assistant, *delta.content});
        } else {
            messages_.back().content += *delta.content;
        }
    }
    if (delta.tool_call) {
        if (messages_.empty() || messages_.back().role != MessageRole::Assistant) {
            messages_.push_back({MessageRole::Assistant, ""});
        }
    }
}

void ChatView::show_system_message(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back({MessageRole::System, msg});
}

void ChatView::show_tool_activity(const std::string& tool_name, const std::string& label)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisplayMessage message;
    message.role = MessageRole::Tool;
    message.content = label;
    message.tool_name = tool_name;
    message.tool_display = ToolDisplay::Activity;
    messages_.push_back(std::move(message));
}

void ChatView::show_tool_diff(
    const std::string& tool_name,
    const std::string& path,
    const std::string& before,
    const std::string& after)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisplayMessage message;
    message.role = MessageRole::Tool;
    message.content = path;
    message.tool_name = tool_name;
    message.tool_parameter = path;
    const auto before_lines = split_lines(before);
    const auto after_lines = split_lines(after);
    size_t prefix = 0;
    while (prefix < before_lines.size() && prefix < after_lines.size() &&
           before_lines[prefix] == after_lines[prefix]) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < before_lines.size() - prefix && suffix < after_lines.size() - prefix &&
           before_lines[before_lines.size() - 1 - suffix] ==
               after_lines[after_lines.size() - 1 - suffix]) {
        ++suffix;
    }
    message.added_lines = static_cast<int>(after_lines.size() - prefix - suffix);
    message.deleted_lines = static_cast<int>(before_lines.size() - prefix - suffix);
    message.tool_extra = "+" + std::to_string(message.added_lines) +
        " -" + std::to_string(message.deleted_lines);
    message.tool_display = ToolDisplay::Diff;
    message.before = before;
    message.after = after;
    messages_.push_back(std::move(message));
}

void ChatView::show_bash_started(
    const std::string& key,
    const std::string& command,
    const std::string& timeout_label,
    const std::string& tool_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisplayMessage message;
    message.role = MessageRole::Tool;
    message.tool_name = tool_name;
    message.tool_parameter = command;
    message.tool_extra = timeout_label;
    message.display_key = key;
    message.tool_display = ToolDisplay::Bash;
    message.bash_running = true;
    messages_.push_back(std::move(message));
}

void ChatView::append_bash_output(const std::string& key, const std::string& output)
{
    if (output.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->tool_display == ToolDisplay::Bash && it->display_key == key) {
            it->content += output;
            return;
        }
    }
}

void ChatView::finish_bash(const std::string& key, const std::string& output, bool success)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->tool_display == ToolDisplay::Bash && it->display_key == key) {
            it->content = output;
            it->bash_running = false;
            it->bash_success = success;
            return;
        }
    }
}

void ChatView::show_subagent_started(const std::string& key, const std::string& task,
                                     const std::string& mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisplayMessage message;
    message.role = MessageRole::Tool;
    message.tool_name = "sub_agent";
    message.tool_parameter = task;
    message.display_key = key;
    message.tool_display = ToolDisplay::SubAgent;
    message.subagent_mode = mode;
    message.subagent_task = task;
    message.subagent_transcript = "### Task\n\n" + task +
        "\n\n*Starting sub-agent...*";
    message.subagent_running = true;
    messages_.push_back(std::move(message));
}

void ChatView::update_subagent(const std::string& key, const std::string& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->tool_display != ToolDisplay::SubAgent || it->display_key != key) {
            continue;
        }
        if (event == "status:thinking") {
            it->subagent_transcript += "\n\n*Thinking...*";
        } else if (event.rfind("assistant\n", 0) == 0) {
            const std::string content = event.substr(10);
            if (!content.empty()) {
                it->subagent_transcript += "\n\n### Sub-agent\n\n" + content;
            }
        } else if (event.rfind("tool_call\n", 0) == 0) {
            const std::string payload = event.substr(10);
            const size_t separator = payload.find('\n');
            const std::string name = separator == std::string::npos
                ? payload : payload.substr(0, separator);
            const std::string arguments = separator == std::string::npos
                ? "{}" : payload.substr(separator + 1);
            it->subagent_latest_tool = name;
            ++it->subagent_tools_used;
            it->subagent_tool_output_started = false;
            it->subagent_transcript += "\n\n#### Tool: `" + name + "`\n\n";
            it->subagent_transcript += "```json\n" + arguments +
                "\n```\n\n**Result**\n\n```text\n";
        } else if (event.rfind("tool_output\n", 0) == 0) {
            const std::string payload = event.substr(12);
            const size_t separator = payload.find('\n');
            if (separator != std::string::npos) {
                const std::string output = payload.substr(separator + 1);
                if (!output.empty()) {
                    it->subagent_transcript += output;
                    it->subagent_tool_output_started = true;
                }
            }
        } else if (event.rfind("tool_result\n", 0) == 0) {
            const std::string payload = event.substr(12);
            const size_t separator = payload.find('\n');
            const std::string output = separator == std::string::npos
                ? "" : payload.substr(separator + 1);
            if (!it->subagent_tool_output_started && !output.empty()) {
                it->subagent_transcript += output;
            }
            it->subagent_transcript += "\n```";
            it->subagent_tool_output_started = false;
        }
        return;
    }
}

void ChatView::finish_subagent(const std::string& key, const std::string& report,
                               bool success)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->tool_display == ToolDisplay::SubAgent && it->display_key == key) {
            it->content = report;
            if (it->subagent_tool_output_started) {
                it->subagent_transcript += "\n```";
                it->subagent_tool_output_started = false;
            }
            it->subagent_transcript += "\n\n### Final report\n\n" + report;
            it->subagent_running = false;
            it->subagent_success = success;
            return;
        }
    }
}

bool ChatView::focus_next_tool()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (messages_.empty()) {
        focused_tool_index_ = -1;
        return false;
    }

    const auto is_tool_panel = [](const DisplayMessage& message) {
        return message.tool_display == ToolDisplay::Bash ||
               message.tool_display == ToolDisplay::SubAgent;
    };

    const int count = static_cast<int>(messages_.size());
    const int start = focused_tool_index_ < 0
        ? count - 1
        : (focused_tool_index_ - 1 + count) % count;
    for (int offset = 0; offset < count; ++offset) {
        const int candidate = (start - offset + count) % count;
        if (is_tool_panel(messages_[candidate])) {
            focused_tool_index_ = candidate;
            return true;
        }
    }
    return false;
}

bool ChatView::has_focused_tool() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return focused_tool_index_ >= 0 &&
           focused_tool_index_ < static_cast<int>(messages_.size()) &&
           (messages_[focused_tool_index_].tool_display == ToolDisplay::Bash ||
            messages_[focused_tool_index_].tool_display == ToolDisplay::SubAgent);
}

bool ChatView::enter_focused_tool_view()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (focused_tool_index_ < 0 || focused_tool_index_ >= static_cast<int>(messages_.size())) {
        return false;
    }
    const auto display = messages_[focused_tool_index_].tool_display;
    if (display != ToolDisplay::Bash && display != ToolDisplay::SubAgent) {
        return false;
    }
    tool_view_mode_ = true;
    return true;
}

bool ChatView::exit_tool_view()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!tool_view_mode_) {
        return false;
    }
    tool_view_mode_ = false;
    return true;
}

bool ChatView::in_tool_view() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tool_view_mode_;
}

void ChatView::set_model(const std::string& model)
{
    std::lock_guard<std::mutex> lock(mutex_);
    model_ = model;
}

void ChatView::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    scroll_position_ = scroll_target_ = 0.0f;
    max_scroll_line_ = 0;
    scroll_to_end_requested_ = false;
    scroll_to_end_immediately_requested_ = false;
    focused_tool_index_ = -1;
    tool_view_mode_ = false;
}

void ChatView::set_on_scroll_to_bottom(std::function<void()> cb)
{
    on_scroll_to_bottom_ = std::move(cb);
}

std::string ChatView::role_label(MessageRole role) const
{
    switch (role) {
        case MessageRole::System: return "system";
        case MessageRole::User: return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool: return "tool";
    }
    return "unknown";
}

Color ChatView::role_color(MessageRole role) const
{
    switch (role) {
        case MessageRole::System: return Color::GrayDark;
        case MessageRole::User: return Color::White;
        case MessageRole::Assistant: return Color::White;
        case MessageRole::Tool: return Color::GrayLight;
    }
    return Color::Default;
}

Color ChatView::role_bg(MessageRole role) const
{
    switch (role) {
        case MessageRole::System: return Color::GrayDark;
        case MessageRole::User: return Color::Default;
        case MessageRole::Assistant: return Color::Default;
        case MessageRole::Tool: return Color::Default;
    }
    return Color::Default;
}

Element ChatView::render_message(const DisplayMessage& msg) const
{
    if (msg.role == MessageRole::User) {
        auto user_bar = hbox({
            text(" ❯ ") | bold | color(Color::White),
            paragraph(msg.content) | color(Color::White) | flex,
        }) | bgcolor(Color::RGB(38, 38, 38));
        
        return vbox({
            user_bar,
            text(""),
        });
    }

    if (msg.role == MessageRole::Assistant) {
        MarkdownRenderer md;
        auto content = md.render(msg.content);
        return vbox({
            content,
            text(""),
        });
    }

    if (msg.role == MessageRole::System) {
        Elements elements;
        std::stringstream ss(msg.content);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) {
                elements.push_back(text(""));
            } else {
                elements.push_back(paragraph(line) | flex);
            }
        }
        return vbox({
            vbox(std::move(elements)) | color(Color::GrayDark) | dim,
            text(""),
        });
    }

    if (msg.role == MessageRole::Tool && msg.tool_display == ToolDisplay::Activity) {
        const auto accent = tool_color(msg.tool_name);
        return hbox({
            text("-> ") | color(accent) | bold,
            paragraph(msg.content) | color(accent) | flex,
        });
#if 0
        return vbox({
            text("→ " + msg.content) | color(Color::GrayDark) | dim,
            text(""),
        });
    }

#endif
    }
    if (msg.role == MessageRole::Tool && msg.tool_display == ToolDisplay::Diff) {
        return render_tool_diff(msg);
    }

    if (msg.role == MessageRole::Tool && msg.tool_display == ToolDisplay::Bash) {
        return render_bash(msg);
    }

    if (msg.role == MessageRole::Tool && msg.tool_display == ToolDisplay::SubAgent) {
        return render_subagent(msg);
    }

    if (msg.role == MessageRole::Tool) {
        const auto accent = tool_color(msg.name);
        std::string tool_header = " [Tool: " + (msg.name.empty() ? "unknown" : msg.name) + "] ";
        Elements elements;
        std::stringstream ss(msg.content);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) {
                elements.push_back(text(""));
            } else {
                elements.push_back(paragraph(" " + line) | flex);
            }
        }
        return vbox({
            vbox({
                text(tool_header) | bold | color(accent),
                separator() | color(Color::GrayDark),
                vbox(std::move(elements)) | color(Color::GrayLight) | dim,
            }) | borderRounded | color(accent),
        });
    }

    auto label = text(" " + role_label(msg.role) + " ") | color(role_color(msg.role)) | bold;
    MarkdownRenderer md;
    auto content = md.render(msg.content);

    return vbox({
        label,
        content,
        text(""),
    });
}

Element ChatView::render_tool_diff(const DisplayMessage& msg) const
{
    const auto accent = tool_color(msg.tool_name);
    const auto before = split_lines(msg.before);
    const auto after = split_lines(msg.after);

    size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) {
        ++prefix;
    }

    size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) {
        ++suffix;
    }

    const size_t context_start = prefix > 3 ? prefix - 3 : 0;
    const size_t context_end_before = std::min(before.size(), before.size() - suffix + 3);
    const size_t context_end_after = std::min(after.size(), after.size() - suffix + 3);

    const auto format_number = [](size_t number) {
        std::ostringstream out;
        if (number != 0) {
            out << number;
        }
        return out.str();
    };

    const auto diff_line = [&format_number](size_t old_number, size_t new_number, char marker,
                                              const std::string& content, Color foreground, Color background) {
        return hbox({
            text(format_number(old_number)) | color(Color::GrayDark) | size(WIDTH, EQUAL, 5),
            text(format_number(new_number)) | color(Color::GrayDark) | size(WIDTH, EQUAL, 5),
            text(std::string(1, marker) + " ") | color(foreground) | bold,
            paragraph(content.empty() ? " " : content) | color(foreground) | flex,
        }) | bgcolor(background);
    };

    Elements lines;
    for (size_t index = context_start; index < prefix; ++index) {
        lines.push_back(diff_line(index + 1, index + 1, ' ', before[index], Color::GrayLight, Color::Default));
    }
    for (size_t index = prefix; index < before.size() - suffix; ++index) {
        lines.push_back(diff_line(index + 1, 0, '-', before[index], Color::RGB(255, 150, 150), Color::RGB(60, 25, 25)));
    }
    for (size_t index = prefix; index < after.size() - suffix; ++index) {
        lines.push_back(diff_line(0, index + 1, '+', after[index], Color::RGB(150, 235, 170), Color::RGB(20, 55, 30)));
    }
    for (size_t offset = 0; offset < std::max(context_end_before - (before.size() - suffix), context_end_after - (after.size() - suffix)); ++offset) {
        const size_t before_index = before.size() - suffix + offset;
        const size_t after_index = after.size() - suffix + offset;
        if (before_index < before.size() && after_index < after.size()) {
            lines.push_back(diff_line(before_index + 1, after_index + 1, ' ', before[before_index], Color::GrayLight, Color::Default));
        }
    }

    if (lines.empty()) {
        lines.push_back(text(" No content changes") | color(Color::GrayDark) | dim);
    }

    return vbox({
        hbox({
            text(" " + msg.tool_name + " : " + msg.tool_parameter + " : " + msg.tool_extra + " ") |
                bold | color(accent),
            filler(),
            text(" diff ") | color(Color::GrayDark) | dim,
        }),
        separator() | color(Color::GrayDark),
        vbox(std::move(lines)),
    }) | borderRounded | color(accent);
}

Element ChatView::render_bash(const DisplayMessage& msg) const
{
    const auto accent = tool_color(msg.tool_name);
    Elements output_lines;
    const auto lines = split_lines(msg.content);
    const size_t visible_lines = 5;
    const bool clipped = !msg.bash_expanded && lines.size() > visible_lines;
    const size_t first_line = clipped ? lines.size() - visible_lines : 0;

    if (clipped) {
        output_lines.push_back(
            text("... " + std::to_string(lines.size() - visible_lines) +
                 " earlier lines hidden - Ctrl+T to expand") | color(Color::GrayDark) | dim);
    }
    for (size_t index = first_line; index < lines.size(); ++index) {
        output_lines.push_back(paragraph(lines[index]) | color(Color::GrayLight));
    }
    if (output_lines.empty()) {
        output_lines.push_back(
            text(msg.bash_running ? "waiting for output..." : "(no output)") |
                color(Color::GrayDark) | dim);
    }

    const std::string state = msg.bash_running ? " - running" :
        (msg.bash_success ? " - done" : " - failed");
    auto header = hbox({
        text(msg.tool_focused ? " > " : " " ) |
            bold | color(msg.tool_focused ? Color::CyanLight : Color::White),
        text(msg.tool_name + " : ") | bold | color(accent),
        paragraph(msg.tool_parameter) | color(Color::White) | flex,
        text(" : " + msg.tool_extra + state + " ") | color(Color::GrayDark),
    });

    auto panel = vbox({
        header,
        separator() | color(Color::GrayDark),
        vbox(std::move(output_lines)) | yframe,
    });
    if (!msg.bash_expanded) {
        panel = panel | size(HEIGHT, EQUAL, 8);
    }

    return panel | borderRounded |
        color(msg.tool_focused ? Color::CyanLight : accent);
}

Element ChatView::render_subagent(const DisplayMessage& msg) const
{
    const auto accent = tool_color(msg.tool_name);
    const std::string state = msg.subagent_running ? "running" :
        (msg.subagent_success ? "done" : "failed");
    const std::string latest = msg.subagent_latest_tool.empty()
        ? "thinking"
        : msg.subagent_latest_tool;

    auto header = hbox({
        text(msg.tool_focused ? " > " : " ") |
            bold | color(msg.tool_focused ? Color::CyanLight : Color::White),
        text("sub_agent : " + msg.subagent_mode + " : ") |
            bold | color(accent),
        paragraph(msg.subagent_task) | color(Color::White) | flex,
        text(" : " + std::to_string(msg.subagent_tools_used) +
             " tools · " + latest + " · " + state + " ") |
            color(Color::GrayDark),
    });

    Elements body;
    if (msg.subagent_expanded) {
        MarkdownRenderer markdown;
        const std::string transcript = msg.subagent_transcript.empty()
            ? (msg.content.empty() ? "(no report)" : msg.content)
            : msg.subagent_transcript;
        body.push_back(markdown.render(transcript) |
                       yframe);
    } else if (!msg.subagent_running && !msg.content.empty()) {
        MarkdownRenderer markdown;
        body.push_back(markdown.render(msg.content) |
                       size(HEIGHT, LESS_THAN, 2) | yframe);
    } else {
        const std::string summary = msg.subagent_running
            ? "working · latest tool: " + latest
            : "report ready · Ctrl+T to expand";
        body.push_back(text(" " + summary) | color(Color::GrayDark) | dim);
    }

    auto panel = vbox({
        header,
        separator() | color(Color::GrayDark),
        vbox(std::move(body)) | yframe,
    });
    if (!msg.subagent_expanded) {
        panel = panel | size(HEIGHT, EQUAL, msg.subagent_running ? 4 : 5);
    }
    return panel | borderRounded |
        color(msg.tool_focused ? Color::CyanLight : accent);
}

namespace {

Element render_welcome_screen(const std::string& model)
{
    const auto accent = Color::RGB(115, 170, 255);
    const auto muted_accent = Color::RGB(90, 120, 160);

    auto command_row = [](const std::string& command, const std::string& description) {
        return hbox({
            text(command) | bold | color(Color::White) | size(WIDTH, EQUAL, 12),
            text(description) | color(Color::GrayDark),
        });
    };

    auto task_card = vbox({
        hbox({
            text("TASK") | bold | color(accent),
            text("   describe what you want to build") | color(Color::GrayDark),
        }),
        separator() | color(Color::GrayDark),
        hbox({
            text("MODEL") | bold | color(Color::GrayLight),
            text("   " + model) | color(Color::White),
        }),
        hbox({
            text("TIP  ") | bold | color(Color::GrayLight),
            text("   start with a goal, a bug, or a file to inspect") | color(Color::GrayDark),
        }),
    }) | borderRounded | color(Color::GrayDark) | size(WIDTH, LESS_THAN, 72);

    auto shortcuts = vbox({
        text("QUICK START") | bold | color(Color::GrayLight),
        command_row("/model", "switch the active model"),
        command_row("/connect", "save your API key"),
        command_row("/connect-exa", "save your Exa API key"),
        command_row("/help", "see all commands"),
    }) | size(WIDTH, LESS_THAN, 72);

    return vbox({
        render_welcome_visual(),
        text(""),
        text("TERMINAL AGENT  /  READY") | bold | color(accent) | hcenter,
        text(""),
        hbox({
            text("<") | color(muted_accent),
            text(" KARNA ") | bold | color(Color::White),
            text(">") | color(muted_accent),
        }) | hcenter,
        paragraph("A focused workspace for turning plain language into working software.") |
            color(Color::GrayLight) | hcenter,
        text(""),
        task_card,
        text(""),
        shortcuts,
    }) | center;
}

} // namespace

Element ChatView::render()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (tool_view_mode_ && focused_tool_index_ >= 0 &&
        focused_tool_index_ < static_cast<int>(messages_.size())) {
        auto selected = messages_[focused_tool_index_];
        selected.tool_focused = true;
        if (selected.tool_display == ToolDisplay::Bash) {
            selected.bash_expanded = true;
        } else if (selected.tool_display == ToolDisplay::SubAgent) {
            selected.subagent_expanded = true;
        }
        return vbox({
            text(" TOOL VIEW  /  " + selected.tool_name + " ") |
                bold | color(tool_color(selected.tool_name)),
            separator() | color(Color::GrayDark),
            render_message(selected) | flex,
            text(""),
            text("Esc  return to chat    Ctrl+T  switch tool") |
                color(Color::GrayDark) | dim,
        }) | yframe;
    }

    Elements children;
    if (!model_.empty()) {
        children.push_back(render_message({
            MessageRole::System,
            "Karna v0.1.0 | Model: " + model_ + " | Type /help for commands",
            ""
        }));
    }
    for (size_t index = 0; index < messages_.size();) {
        if (messages_[index].role == MessageRole::Tool) {
            Elements tool_messages;
            while (index < messages_.size() && messages_[index].role == MessageRole::Tool) {
                auto tool_message = messages_[index];
                tool_message.tool_focused = static_cast<int>(index) == focused_tool_index_;
                tool_messages.push_back(render_message(tool_message));
                ++index;
            }
            children.push_back(vbox(std::move(tool_messages)));
        } else {
            auto message = messages_[index];
            message.tool_focused = static_cast<int>(index) == focused_tool_index_;
            children.push_back(render_message(message));
            ++index;
        }
    }

    if (messages_.empty()) {
        children.clear();
        children.push_back(render_welcome_screen(model_));
    }

    auto content = vbox(std::move(children));
    content->ComputeRequirement();
    max_scroll_line_ = std::max(0, content->requirement().min_y - 1);
    if (scroll_to_end_requested_) {
        scroll_target_ = static_cast<float>(max_scroll_line_);
        if (scroll_to_end_immediately_requested_) {
            scroll_position_ = scroll_target_;
            scroll_to_end_immediately_requested_ = false;
        }
        scroll_to_end_requested_ = false;
    }
    scroll_target_ = std::clamp(
        scroll_target_, 0.0f, static_cast<float>(max_scroll_line_));
    scroll_position_ = std::clamp(
        scroll_position_, 0.0f, static_cast<float>(max_scroll_line_));

    return std::move(content) |
        focusPosition(0, static_cast<int>(std::lround(scroll_position_))) |
        vscroll_indicator | yframe;
}

void ChatView::load_history(const std::vector<Message>& messages)
{
    clear();
    for (const auto& message : messages) {
        add_message(message);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scroll_to_end_immediately_requested_ = true;
    }
    scroll_to_end();
}

void ChatView::set_scroll_to_bottom(bool scroll)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (scroll) {
        scroll_to_end_requested_ = true;
    } else {
        scroll_to_end_requested_ = false;
        scroll_position_ = scroll_target_ = 0.0f;
    }
}

void ChatView::scroll_by(int lines)
{
    std::lock_guard<std::mutex> lock(mutex_);
    scroll_to_end_requested_ = false;
    scroll_target_ = std::clamp(
        scroll_target_ + static_cast<float>(lines),
        0.0f,
        static_cast<float>(max_scroll_line_));
}

void ChatView::scroll_to_start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    scroll_to_end_requested_ = false;
    scroll_target_ = 0.0f;
}

void ChatView::scroll_to_end()
{
    std::lock_guard<std::mutex> lock(mutex_);
    scroll_to_end_requested_ = true;
}

bool ChatView::advance_scroll_animation()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (scroll_position_ == scroll_target_) return false;
    scroll_position_ = scroll_target_;
    return true;
}
