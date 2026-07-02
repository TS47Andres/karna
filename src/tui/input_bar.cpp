#include "tui/input_bar.h"
#include <ftxui/component/component_options.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace ftxui;

#ifdef _WIN32
static std::string get_clipboard_text()
{
    if (!OpenClipboard(nullptr)) return {};
    std::string result;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* wtext = static_cast<wchar_t*>(GlobalLock(hData));
        if (wtext) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                result.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, wtext, -1, result.data(), len, nullptr, nullptr);
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}
#endif

class PasteInterceptor : public ComponentBase {
public:
    PasteInterceptor(Component child)
    {
        Add(std::move(child));
    }

    bool OnEvent(Event event) override
    {
        if (event.is_character() && event.character() == "\x16") {
            std::string text;
#ifdef _WIN32
            text = get_clipboard_text();
#endif
            if (!text.empty()) {
                auto child = children_[0];
                for (size_t i = 0; i < text.size();) {
                    unsigned char c = static_cast<unsigned char>(text[i]);
                    int len = 1;
                    if ((c & 0x80) == 0) len = 1;
                    else if ((c & 0xE0) == 0xC0) len = 2;
                    else if ((c & 0xF0) == 0xE0) len = 3;
                    else if ((c & 0xF8) == 0xF0) len = 4;
                    child->OnEvent(Event::Character(text.substr(i, len)));
                    i += len;
                }
            }
            return true;
        }
        return ComponentBase::OnEvent(event);
    }
};

InputBar::InputBar()
{
}

Component InputBar::build()
{
    auto input_content = std::make_shared<std::string>();

    InputOption option;
    option.placeholder = "Type a message... (/help for commands)";
    option.transform = [](InputState state) {
        return state.element | bgcolor(Color::Default) | color(Color::White);
    };

    auto input = Input(input_content.get(), option);

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

    auto interceptor = std::make_shared<PasteInterceptor>(input);
    return interceptor;
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
