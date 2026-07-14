#include "tui/status_bar.h"
#include <chrono>
#include <vector>

using namespace ftxui;

StatusBar::StatusBar()
{}

Component StatusBar::build()
{
    return Renderer([this] { return render(); });
}

void StatusBar::set_model(const std::string& model)
{
    std::lock_guard<std::mutex> lock(mutex_);
    model_ = model;
}

void StatusBar::set_token_count(int prompt, int completion)
{
    std::lock_guard<std::mutex> lock(mutex_);
    prompt_tokens_ = prompt;
    completion_tokens_ = completion;
}

void StatusBar::set_status(const std::string& status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
}

void StatusBar::set_typing(bool typing)
{
    std::lock_guard<std::mutex> lock(mutex_);
    typing_ = typing;
}

bool StatusBar::is_typing() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return typing_;
}

Element StatusBar::render()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto model_elem = text(" " + model_) | color(Color::White) | bold;
    
    Element status_elem;
    Color status_color = Color::GrayDark;
    
    if (status_ == "Thinking...") {
        status_color = Color::White;
    } else if (status_ == "Running tools...") {
        status_color = Color::GrayLight;
    } else if (status_ == "Ready") {
        status_color = Color::GrayDark;
    } else {
        // e.g. Error or Aborted
        status_color = Color::GrayLight;
    }

    if (typing_) {
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        const std::vector<std::string> spinner_chars = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        int idx = (ms / 80) % spinner_chars.size();
        status_elem = text(" " + status_ + " " + spinner_chars[idx]) | color(status_color) | flex;
    } else {
        status_elem = text(" " + status_) | color(status_color) | flex;
    }

    return hbox({
               model_elem,
               separator() | color(Color::GrayDark) | size(WIDTH, EQUAL, 1),
               status_elem,
               text(" Esc twice Abort ") | color(Color::GrayDark),
           }) |
           size(HEIGHT, EQUAL, 1);
}
