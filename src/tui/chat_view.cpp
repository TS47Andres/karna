#include "tui/chat_view.h"
#include <algorithm>
#include <sstream>

using namespace ftxui;

ChatView::ChatView()
{}

Component ChatView::build()
{
    auto renderer = Renderer([this] { return render(); });
    component_ = CatchEvent(renderer, [this](Event event) {
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                scroll_by(-0.08f);
            } else if (event.mouse().button == Mouse::WheelDown) {
                scroll_by(0.08f);
            }
            return true;
        }
        if (event == Event::PageUp || event == Event::Home) {
            scroll_by(event == Event::Home ? -1.0f : -0.25f);
            return true;
        }
        if (event == Event::PageDown || event == Event::End) {
            scroll_by(event == Event::End ? 1.0f : 0.25f);
            return true;
        }
        return false;
    });
    return component_;
}

void ChatView::focus()
{
    return;
}

void ChatView::add_message(const Message& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string content = msg.content;
    for (const auto& tc : msg.tool_calls) {
        content += "\n[Tool call: " + tc.function_name + "]";
    }
    std::string name = msg.name ? *msg.name : "";
    messages_.push_back({msg.role, content, name});
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

void ChatView::append_tool_call(const std::string& text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (messages_.empty() || messages_.back().role != MessageRole::Assistant) {
        messages_.push_back({MessageRole::Assistant, text});
    } else {
        messages_.back().content += text;
    }
}

void ChatView::show_system_message(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back({MessageRole::System, msg});
}

void ChatView::show_tool_activity(const std::string& label)
{
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back({MessageRole::Tool, label, "", ToolDisplay::Activity});
}

void ChatView::show_tool_diff(
    const std::string& path,
    const std::string& before,
    const std::string& after)
{
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back({MessageRole::Tool, path, "", ToolDisplay::Diff, before, after});
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
        return vbox({
            text("→ " + msg.content) | color(Color::GrayDark) | dim,
            text(""),
        });
    }

    if (msg.role == MessageRole::Tool && msg.tool_display == ToolDisplay::Diff) {
        return render_tool_diff(msg);
    }

    if (msg.role == MessageRole::Tool) {
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
                text(tool_header) | bold | color(Color::White),
                separator() | color(Color::GrayDark),
                vbox(std::move(elements)) | color(Color::GrayLight) | dim,
            }) | borderRounded | color(Color::GrayDark),
            text(""),
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
    const auto split_lines = [](const std::string& content) {
        std::vector<std::string> lines;
        std::stringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        if (!content.empty() && content.back() == '\n') {
            lines.push_back("");
        }
        return lines;
    };

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
            text(" " + msg.content + " ") | bold | color(Color::White),
            filler(),
            text(" diff ") | color(Color::GrayDark) | dim,
        }),
        separator() | color(Color::GrayDark),
        vbox(std::move(lines)),
        text(""),
    }) | borderRounded | color(Color::GrayDark);
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
        command_row("/setup", "save your API key"),
        command_row("/help", "see all commands"),
    }) | size(WIDTH, LESS_THAN, 72);

    return vbox({
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

    Elements children;
    if (!model_.empty()) {
        children.push_back(render_message({
            MessageRole::System,
            "Karna v0.1.0 | Model: " + model_ + " | Type /help for commands",
            ""
        }));
    }
    for (const auto& msg : messages_) {
        children.push_back(render_message(msg));
    }

    if (messages_.empty()) {
        children.clear();
        children.push_back(render_welcome_screen(model_));
    }

    if (false) {
        auto title = text("   █  █  █▀▀█  █▀▀█  █▀▀█  █▀▀█   ") | bold | color(Color::White) | hcenter;
        auto title2 = text("  █▀▀█  █▄▄█  █▄▄▀  █  █  █▄▄█   ") | bold | color(Color::White) | hcenter;
        auto subtitle = text("Karna - Term-based AI coding harness") | hcenter | color(Color::GrayLight);
        
        auto section_tips = vbox({
            text("Tips:") | bold | color(Color::White),
            text(" • Type a message or ask a question to start coding.") | color(Color::GrayLight),
            text(" • Use slash commands like /help, /model, /clear.") | color(Color::GrayLight),
            text(" • Press Up/Down arrow keys to navigate input history.") | color(Color::GrayLight),
            text(" • Press F5 or Escape to cancel active request.") | color(Color::GrayLight),
        });

        auto section_info = vbox({
            text("System Info:") | bold | color(Color::White),
            text(" • Theme: Monochrome Grayscale") | color(Color::GrayLight),
            text(" • Status: Ready and listening") | color(Color::GrayLight),
        });

        auto card_content = vbox({
            title,
            title2,
            text(""),
            subtitle,
            text(""),
            separator() | color(Color::GrayDark),
            text(""),
            section_tips,
            text(""),
            separator() | color(Color::GrayDark),
            text(""),
            section_info,
        }) | size(WIDTH, LESS_THAN, 60) | borderRounded | color(Color::GrayDark) | center;

        children.push_back(card_content);
    }

    return vbox(std::move(children)) |
        focusPositionRelative(0.0f, scroll_position_) |
        vscroll_indicator | yframe;
}

void ChatView::set_scroll_to_bottom(bool scroll)
{
    std::lock_guard<std::mutex> lock(mutex_);
    scroll_position_ = scroll ? 1.0f : 0.0f;
}

void ChatView::scroll_by(float amount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    scroll_position_ = std::clamp(scroll_position_ + amount, 0.0f, 1.0f);
}
