#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/message.h"
#include "tui/markdown_renderer.h"
#include <vector>
#include <string>
#include <mutex>
#include <functional>

class ChatView {
public:
    ChatView();
    ftxui::Component build();

    void add_message(const Message& msg);
    void append_to_last(const std::string& content);
    void add_delta(const Delta& delta);
    void append_tool_call(const std::string& text);
    void show_system_message(const std::string& msg);
    void clear();
    void set_on_scroll_to_bottom(std::function<void()> cb);
    void set_scroll_to_bottom(bool scroll);

private:
    struct DisplayMessage {
        MessageRole role;
        std::string content;
    };

    std::vector<DisplayMessage> messages_;
    std::mutex mutex_;
    bool scroll_to_bottom_ = true;
    std::function<void()> on_scroll_to_bottom_;
    ftxui::Element render();
    ftxui::Element render_message(const DisplayMessage& msg) const;
    std::string role_label(MessageRole role) const;
    ftxui::Color role_color(MessageRole role) const;
    ftxui::Color role_bg(MessageRole role) const;
};
