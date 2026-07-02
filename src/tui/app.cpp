#include "tui/app.h"

using namespace ftxui;

TuiApp::TuiApp()
    : screen_(ScreenInteractive::Fullscreen())
{}

TuiApp::~TuiApp() = default;

void TuiApp::run()
{
    auto chat_component = chat_view_.build();
    auto input_component = input_bar_.build();
    auto status_component = status_bar_.build();

    auto container = Container::Vertical({
        chat_component,
        input_component,
    });

    auto renderer = Renderer(container, [&, chat_component, input_component, status_component]() {
        auto chat_elem = chat_component->Render() | flex;
        auto input_elem = input_component->Render() | size(HEIGHT, EQUAL, 3);
        auto status_elem = status_component->Render();

        return vbox({
            chat_elem,
            separator(),
            input_elem,
            separator() | bold,
            status_elem,
        });
    });

    main_component_ = renderer;
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

void TuiApp::request_refresh()
{
    screen_.PostEvent(Event::Custom);
}
