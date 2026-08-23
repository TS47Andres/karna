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
    void show_tool_activity(const std::string& tool_name, const std::string& label);
    void show_tool_diff(const std::string& tool_name, const std::string& path,
                        const std::string& before, const std::string& after);
    void show_bash_started(const std::string& key, const std::string& command,
                           const std::string& timeout_label,
                           const std::string& tool_name = "bash");
    void append_bash_output(const std::string& key, const std::string& output);
    void finish_bash(const std::string& key, const std::string& output, bool success);
    void toggle_bash_view();
    void show_subagent_started(const std::string& key, const std::string& task,
                               const std::string& mode);
    void update_subagent(const std::string& key, const std::string& event);
    void finish_subagent(const std::string& key, const std::string& report, bool success);
    void toggle_last_tool_view();
    bool focus_next_tool();
    bool has_focused_tool() const;
    bool enter_focused_tool_view();
    bool exit_tool_view();
    bool in_tool_view() const;
    void set_model(const std::string& model);
    void clear();
    void set_on_scroll_to_bottom(std::function<void()> cb);
    void set_scroll_to_bottom(bool scroll);
    void scroll_by(int lines);
    void scroll_to_start();
    void scroll_to_end();
    bool advance_scroll_animation();
    void focus();

private:
    ftxui::Component component_;
    enum class ToolDisplay {
        Default,
        Activity,
        Diff,
        Bash,
        SubAgent,
    };
    struct DisplayMessage {
        MessageRole role;
        std::string content;
        std::string name;
        std::string tool_name;
        std::string tool_parameter;
        std::string tool_extra;
        std::string display_key;
        ToolDisplay tool_display{ToolDisplay::Default};
        std::string before;
        std::string after;
        int added_lines{0};
        int deleted_lines{0};
        bool bash_expanded{false};
        bool bash_running{false};
        bool bash_success{false};
        std::string subagent_mode;
        std::string subagent_task;
        std::string subagent_latest_tool;
        int subagent_tools_used{0};
        std::string subagent_transcript;
        bool subagent_tool_output_started{false};
        bool subagent_expanded{false};
        bool subagent_running{false};
        bool subagent_success{false};
        bool tool_focused{false};
    };

    std::vector<DisplayMessage> messages_;
    std::string model_;
    mutable std::mutex mutex_;
    float scroll_position_{0.0f};
    float scroll_target_{0.0f};
    int max_scroll_line_{0};
    bool scroll_to_end_requested_{false};
    int focused_tool_index_{-1};
    bool tool_view_mode_{false};
    std::function<void()> on_scroll_to_bottom_;
    ftxui::Element render();
    ftxui::Element render_message(const DisplayMessage& msg) const;
    ftxui::Element render_tool_diff(const DisplayMessage& msg) const;
    ftxui::Element render_bash(const DisplayMessage& msg) const;
    ftxui::Element render_subagent(const DisplayMessage& msg) const;
    std::string role_label(MessageRole role) const;
    ftxui::Color role_color(MessageRole role) const;
    ftxui::Color role_bg(MessageRole role) const;
};
