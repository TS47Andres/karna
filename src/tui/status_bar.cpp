#include "tui/status_bar.h"

using namespace ftxui;

StatusBar::StatusBar()
{}

Component StatusBar::build()
{
    return Renderer([this] { return render(); });
}

void StatusBar::set_model(const std::string& model)
{
    model_ = model;
}

void StatusBar::set_token_count(int prompt, int completion)
{
    prompt_tokens_ = prompt;
    completion_tokens_ = completion;
}

void StatusBar::set_status(const std::string& status)
{
    status_ = status;
}

void StatusBar::set_typing(bool typing)
{
    typing_ = typing;
}

Element StatusBar::render()
{
    auto model_elem = text(" " + model_) | color(Color::White) | bold;
    auto status_elem = text(" " + status_) | color(typing_ ? Color::White : Color::GrayDark) | flex;

    return hbox({
               model_elem,
               separator() | color(Color::GrayDark) | size(WIDTH, EQUAL, 1),
               status_elem,
               text(" F5/Esc Abort/Exit ") | color(Color::GrayDark),
           }) |
           size(HEIGHT, EQUAL, 1);
}
