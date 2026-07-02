#include "tui/input_bar.h"
#include <ftxui/component/component_options.hpp>

using namespace ftxui;

InputBar::InputBar()
{
}

Component InputBar::build()
{
    auto input_content = std::make_shared<std::string>();
    auto input = Input(input_content.get(), "Type a message... (/help for commands)");

    input |= CatchEvent([this, input_content](Event event) {
        if (event == Event::Return) {
            std::string text = *input_content;
            input_content->clear();
            if (!text.empty()) {
                history_.push_back(text);
                history_index_ = -1;
                if (on_submit_) on_submit_(text);
            }
            return true;
        }

        if (event == Event::ArrowUp && !history_.empty()) {
            if (history_index_ == -1) {
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                --history_index_;
            }
            *input_content = history_[history_index_];
            return true;
        }

        if (event == Event::ArrowDown && history_index_ >= 0) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                ++history_index_;
                *input_content = history_[history_index_];
            } else {
                history_index_ = -1;
                input_content->clear();
            }
            return true;
        }

        return false;
    });

    return input;
}

void InputBar::set_on_submit(std::function<void(std::string)> callback)
{
    on_submit_ = std::move(callback);
}

void InputBar::add_to_history(const std::string& entry)
{
    history_.push_back(entry);
    history_index_ = -1;
}

std::string InputBar::get_text() const
{
    return {};
}

void InputBar::clear()
{
}

void InputBar::focus()
{
}
