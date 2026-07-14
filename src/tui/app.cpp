#include "tui/app.h"
#include <iostream>

using namespace ftxui;

TuiApp::TuiApp()
    : screen_(ScreenInteractive::Fullscreen())
{
    screen_.TrackMouse(false);
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
            while (run_refresh_thread_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                if (status_bar_.is_typing()) {
                    request_refresh();
                }
            }
        } catch (...) {
            run_refresh_thread_ = false;
        }
    });

    std::cout << "\033[?25l";
    chat_component_ = chat_view_.build();
    input_component_ = input_bar_.build();
    auto status_component = status_bar_.build();
    auto sidebar_component = sidebar_.build();

    auto container = Container::Vertical({
        chat_component_,
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
        if (event == Event::F5 || event == Event::Escape) {
            if (on_escape_) {
                on_escape_();
            }
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse) {
            return true; // Consume Tab to prevent focus loss
        }
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                chat_view_.scroll_by(-0.08f);
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                chat_view_.scroll_by(0.08f);
                return true;
            }
        }
        if (event == Event::ArrowUp && input_bar_.get_text().empty()) {
            chat_view_.scroll_by(-0.08f);
            return true;
        }
        if (event == Event::ArrowDown && input_bar_.get_text().empty()) {
            chat_view_.scroll_by(0.08f);
            return true;
        }
        if (event == Event::PageUp || event == Event::Home) {
            chat_view_.scroll_by(event == Event::Home ? -1.0f : -0.25f);
            return true;
        }
        if (event == Event::PageDown || event == Event::End) {
            chat_view_.scroll_by(event == Event::End ? 1.0f : 0.25f);
            return true;
        }
        if (event == Event::Custom) {
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
    std::cout << "\033[?25h";
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
    screen_.PostEvent(Event::Custom);
}

void TuiApp::post(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_.push(std::move(callback));
    }
    request_refresh();
}
