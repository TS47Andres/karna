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

    void request_refresh();
    void set_typing_state(bool typing);
    void post(std::function<void()> callback);

private:
    ftxui::ScreenInteractive screen_;
    ftxui::Component main_component_;
    ftxui::Component chat_component_;
    ftxui::Component input_component_;

    ChatView chat_view_;
    InputBar input_bar_;
    StatusBar status_bar_;
    Sidebar sidebar_;

    std::function<void()> on_escape_;
    std::thread refresh_thread_;
    std::atomic<bool> run_refresh_thread_{true};
    bool last_typing_state_{false};
    std::mutex callbacks_mutex_;
    std::queue<std::function<void()>> callbacks_;
};
