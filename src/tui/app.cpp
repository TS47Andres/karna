#include "tui/app.h"
#include <iostream>

using namespace ftxui;

TuiApp::TuiApp()
    : screen_(ScreenInteractive::Fullscreen())
{}

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
    refresh_thread_ = std::thread([this]() {
        while (run_refresh_thread_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (status_bar_.is_typing()) {
                request_refresh();
            }
        }
    });

    std::cout << "\033[?25l";
    auto chat_component = chat_view_.build();
    auto input_component = input_bar_.build();
    auto status_component = status_bar_.build();
    auto sidebar_component = sidebar_.build();

    auto container = Container::Vertical({
        chat_component,
        input_component,
        sidebar_component,
    });

    auto renderer = Renderer(container, [&, chat_component, input_component, status_component, sidebar_component]() {
        auto chat_elem = chat_component->Render() | flex;
        auto input_elem = input_component->Render();
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

    main_component_ = CatchEvent(renderer, [this](Event event) {
        if (event == Event::F5) {
            if (on_escape_) {
                on_escape_();
            }
            return true;
        }
        return false;
    });

    screen_.Loop(main_component_);
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

void TuiApp::request_refresh()
{
    screen_.PostEvent(Event::Custom);
}
