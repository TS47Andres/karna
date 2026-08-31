#pragma once

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>

#include "tui/chat_view.h"
#include "tui/input_bar.h"
#include "tui/status_bar.h"
#include "tui/sidebar.h"
#include "core/command.h"
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>

class TuiApp {
public:
    TuiApp();
    ~TuiApp();

    void run();
    void stop();

    ChatView& chat_view();
    InputBar& input_bar();
    StatusBar& status_bar();
    Sidebar& sidebar();

    void set_on_escape(std::function<void()> callback);
    void set_on_callback_error(std::function<void(std::string)> callback);

    void request_refresh();
    void set_typing_state(bool typing);
    void post(std::function<void()> callback);
    void show_access_prompt(const std::string& detail,
                            std::function<void()> on_allow,
                            std::function<void()> on_deny);
    void clear_access_prompt();

private:
    ftxui::ScreenInteractive screen_;
    ftxui::Component main_component_;
    ftxui::Component chat_component_;
    ftxui::Component input_component_;
    ftxui::Component access_component_;
    ftxui::Component access_buttons_;

    ChatView chat_view_;
    InputBar input_bar_;
    StatusBar status_bar_;
    Sidebar sidebar_;

    std::function<void()> on_escape_;
    std::function<void(std::string)> on_callback_error_;
    std::thread refresh_thread_;
    std::atomic<bool> run_refresh_thread_{true};
    std::atomic<bool> refresh_pending_{false};
    bool last_typing_state_{false};
    bool access_prompt_active_{false};
    std::string access_prompt_detail_;
    std::function<void()> on_access_allow_;
    std::function<void()> on_access_deny_;
    std::mutex callbacks_mutex_;
    std::queue<std::function<void()>> callbacks_;
};
