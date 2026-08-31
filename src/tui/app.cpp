#include "tui/app.h"
#include <iostream>

using namespace ftxui;

TuiApp::TuiApp()
    : screen_(ScreenInteractive::Fullscreen())
{
    screen_.TrackMouse(true);
}

TuiApp::~TuiApp()
{
    run_refresh_thread_ = false;
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
}

void TuiApp::run()
{
    run_refresh_thread_ = true;
    refresh_thread_ = std::thread([this]() noexcept {
        try {
            int typing_refresh_ticks = 0;
            while (run_refresh_thread_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                const bool scrolling = chat_view_.advance_scroll_animation();
                const bool typing = status_bar_.is_typing();
                const bool typing_refresh = typing && (++typing_refresh_ticks >= 5);
                if (typing_refresh) {
                    typing_refresh_ticks = 0;
                }
                if (typing_refresh || scrolling) {
                    request_refresh();
                }
                if (!typing) {
                    typing_refresh_ticks = 0;
                }
            }
        } catch (...) {
            run_refresh_thread_ = false;
        }
    });

    chat_component_ = chat_view_.build();
    input_component_ = input_bar_.build();
    access_buttons_ = Container::Horizontal({
        Button(" Allow ", [this] { if (on_access_allow_) on_access_allow_(); }),
        Button(" Deny ", [this] { if (on_access_deny_) on_access_deny_(); })
    });
    access_component_ = Renderer(access_buttons_, [this] {
        if (!access_prompt_active_) return text("");
        return vbox({
            paragraph("Approval required: " + access_prompt_detail_),
            access_buttons_->Render()
        }) | borderRounded | color(Color::Yellow);
    });
    auto status_component = status_bar_.build();
    auto sidebar_component = sidebar_.build();

    auto container = Container::Vertical({
        chat_component_,
        access_component_,
        input_component_,
        sidebar_component,
    });

    auto renderer = Renderer(container, [this, status_component, sidebar_component]() {
        auto chat_elem = chat_component_->Render() | flex;
        auto input_elem = input_component_->Render();
        auto status_elem = status_component->Render();
        auto sidebar_elem = sidebar_component->Render();
        auto separator_color = Color::GrayDark;

        auto left_side = vbox({
            chat_elem,
            separator() | color(separator_color),
            access_component_->Render(),
            input_elem,
        }) | flex;

        auto main_content = hbox({
            left_side,
            separator() | color(separator_color),
            sidebar_elem,
        }) | flex;

        return vbox({
            main_content,
            separator() | color(separator_color),
            status_elem,
        });
    });

    input_bar_.focus();

    main_component_ = CatchEvent(renderer, [this](Event event) {
        if (event == Event::Escape && chat_view_.in_tool_view()) {
            chat_view_.exit_tool_view();
            request_refresh();
            return true;
        }
        if (event == Event::F5 || event == Event::Escape) {
            if (on_escape_) {
                on_escape_();
            }
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse) {
            return true; // Consume Tab to prevent focus loss
        }
        if (event == Event::Character('\x14') && input_bar_.get_text().empty()) {
            chat_view_.focus_next_tool();
            request_refresh();
            return true;
        }
        if (event == Event::Return && input_bar_.get_text().empty() &&
            chat_view_.has_focused_tool()) {
            chat_view_.enter_focused_tool_view();
            request_refresh();
            return true;
        }
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                chat_view_.scroll_by(-8);
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                chat_view_.scroll_by(8);
                return true;
            }
        }
        if (event == Event::ArrowUp && input_bar_.get_text().empty()) {
            chat_view_.scroll_by(-3);
            return true;
        }
        if (event == Event::ArrowDown && input_bar_.get_text().empty()) {
            chat_view_.scroll_by(3);
            return true;
        }
        if (event == Event::Home) {
            chat_view_.scroll_to_start();
            return true;
        }
        if (event == Event::End) {
            chat_view_.scroll_to_end();
            return true;
        }
        if (event == Event::PageUp) {
            chat_view_.scroll_by(-24);
            return true;
        }
        if (event == Event::PageDown) {
            chat_view_.scroll_by(24);
            return true;
        }
        if (event == Event::Custom) {
            refresh_pending_.store(false);
            std::queue<std::function<void()>> callbacks;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                callbacks.swap(callbacks_);
            }
            while (!callbacks.empty()) {
                auto callback = std::move(callbacks.front());
                callbacks.pop();
                try {
                    callback();
                } catch (const std::exception& e) {
                    try {
                        if (on_callback_error_) {
                            on_callback_error_(e.what());
                        }
                    } catch (...) {
                    }
                } catch (...) {
                    try {
                        if (on_callback_error_) {
                            on_callback_error_("Unknown asynchronous callback failure");
                        }
                    } catch (...) {
                    }
                }
            }
            return false;
        }
        return false;
    });

    screen_.Loop(main_component_);
}

void TuiApp::set_typing_state(bool typing)
{
    status_bar_.set_typing(typing);
    request_refresh();
}

void TuiApp::stop()
{
    screen_.ExitLoopClosure()();
}

ChatView& TuiApp::chat_view()
{
    return chat_view_;
}

InputBar& TuiApp::input_bar()
{
    return input_bar_;
}

StatusBar& TuiApp::status_bar()
{
    return status_bar_;
}

Sidebar& TuiApp::sidebar()
{
    return sidebar_;
}

void TuiApp::set_on_escape(std::function<void()> callback)
{
    on_escape_ = std::move(callback);
}

void TuiApp::set_on_callback_error(std::function<void(std::string)> callback)
{
    on_callback_error_ = std::move(callback);
}

void TuiApp::request_refresh()
{
    bool expected = false;
    if (refresh_pending_.compare_exchange_strong(expected, true)) {
        screen_.PostEvent(Event::Custom);
    }
}

void TuiApp::post(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.push(std::move(callback));
    }
    request_refresh();
}

void TuiApp::show_access_prompt(const std::string& detail,
                                std::function<void()> on_allow,
                                std::function<void()> on_deny)
{
    access_prompt_detail_ = detail;
    on_access_allow_ = std::move(on_allow);
    on_access_deny_ = std::move(on_deny);
    access_prompt_active_ = true;
    request_refresh();
}

void TuiApp::clear_access_prompt()
{
    access_prompt_active_ = false;
    on_access_allow_ = {};
    on_access_deny_ = {};
    request_refresh();
}
