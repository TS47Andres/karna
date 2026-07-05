#include "tui/chat_view.h"
#include <sstream>

using namespace ftxui;

ChatView::ChatView()
{}

Component ChatView::build()
{
    auto renderer = Renderer([this] { return render(); });
    return CatchEvent(renderer, [this](Event event) {
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                scroll_to_bottom_ = false;
            } else if (event.mouse().button == Mouse::WheelDown) {
                scroll_to_bottom_ = true;
            }
            return false;
        }
        if (event == Event::PageUp || event == Event::Home) {
            scroll_to_bottom_ = false;
        }
        if (event == Event::PageDown || event == Event::End) {
            scroll_to_bottom_ = true;
        }
        return false;
    });
}

void ChatView::add_message(const Message& msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string content = msg.content;
    for (const auto& tc : msg.tool_calls) {
        content += "\n[Tool call: " + tc.function_name + "]";
    }
    messages_.push_back({msg.role, content});
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

    if (msg.role == MessageRole::Tool) {
        Elements elements;
        std::stringstream ss(msg.content);
        std::string line;
        bool is_first = true;
        while (std::getline(ss, line)) {
            std::string prefix_line = is_first ? (" ⚙ " + line) : ("   " + line);
            is_first = false;
            if (line.empty()) {
                elements.push_back(text(""));
            } else {
                elements.push_back(paragraph(prefix_line) | flex);
            }
        }
        return vbox({
            vbox(std::move(elements)) | color(Color::GrayLight) | dim,
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

Element ChatView::render()
{
    std::lock_guard<std::mutex> lock(mutex_);

    Elements children;
    for (const auto& msg : messages_) {
        children.push_back(render_message(msg));
    }

    if (children.empty()) {
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

    if (scroll_to_bottom_ && !children.empty()) {
        children.back() = children.back() | focus;
    }

    return vbox(std::move(children)) | vscroll_indicator | yframe;
}
