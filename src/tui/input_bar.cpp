#include "tui/input_bar.h"
#include "core/command.h"
#include <ftxui/component/component_options.hpp>
#include <algorithm>
#include <cctype>

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
    input_content_ = std::make_shared<std::string>();

    InputOption option;
    option.placeholder = "Type a message... (/help for commands)";
    option.transform = [](InputState state) {
        return state.element | bgcolor(Color::Default) | color(Color::White);
    };
    option.on_change = [this]() {
        const auto& text = *input_content_;
        if (text.empty() || text[0] != '/') {
            show_suggestions_ = false;
            return;
        }
        update_suggestions(text.substr(1));
    };

    auto input = Input(input_content_.get(), option);
    input_component_ = input;

    auto input_with_history = CatchEvent(input, [this](Event event) {
        if (event == Event::ArrowUp && !show_suggestions_ && !history_.empty()) {
            if (history_index_ == -1) {
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                --history_index_;
            }
            *input_content_ = history_[history_index_];
            return true;
        }
        if (event == Event::ArrowDown && !show_suggestions_ && history_index_ >= 0) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                ++history_index_;
                *input_content_ = history_[history_index_];
            } else {
                history_index_ = -1;
                input_content_->clear();
            }
            return true;
        }
        return false;
    });

    auto interceptor = std::make_shared<PasteInterceptor>(input_with_history);

    auto input_renderer = Renderer(interceptor, [this, interceptor]() {
        auto input_elem = interceptor->Render();
        bool is_focused = interceptor->Focused();
        auto border_color = is_focused ? Color::White : Color::GrayDark;
        
        return vbox({
            hbox({
                text(" karna ") | (is_focused ? color(Color::White) | bold : color(Color::GrayDark)),
            }),
            input_elem
        }) | borderRounded | color(border_color);
    });

    auto suggestion_renderer = Renderer([this]() -> Element {
        return render_suggestion_list();
    });

    auto maybe_suggestions = Maybe(suggestion_renderer, [this]() { return show_suggestions_; });

    auto container = Container::Vertical({
        maybe_suggestions,
        input_renderer,
    });

    container = CatchEvent(container, [this](Event event) {
        if (show_suggestions_ && !suggestions_.empty()) {
            if (event == Event::ArrowUp) {
                if (selected_index_ <= 0) {
                    selected_index_ = static_cast<int>(suggestions_.size()) - 1;
                } else {
                    --selected_index_;
                }
                return true;
            }
            if (event == Event::ArrowDown) {
                selected_index_ = (selected_index_ + 1) % static_cast<int>(suggestions_.size());
                return true;
            }
            if (event == Event::Return && selected_index_ >= 0) {
                apply_suggestion();
                return true;
            }
            if (event == Event::Escape) {
                show_suggestions_ = false;
                selected_index_ = -1;
                return true;
            }
        }

        if (event == Event::Return) {
            std::string text = *input_content_;
            if (!text.empty()) {
                history_.push_back(text);
                history_index_ = -1;
                if (on_submit_) on_submit_(text);
            }
            input_content_->clear();
            show_suggestions_ = false;
            return true;
        }

        return false;
    });

    container_ = container;
    return container_;
}

void InputBar::update_suggestions(const std::string& query)
{
    if (!command_registry_) {
        show_suggestions_ = false;
        return;
    }

    suggestions_.clear();
    selected_index_ = -1;

    auto all_commands = command_registry_->all();
    std::string lower_query;
    for (auto c : query) lower_query += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto* cmd : all_commands) {
        std::string cmd_lower;
        for (auto c : cmd->name()) cmd_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (cmd_lower.find(lower_query) == 0) {
            suggestions_.push_back({cmd->name(), cmd->description()});
            if (static_cast<int>(suggestions_.size()) >= 5) break;
        }
    }

    show_suggestions_ = !suggestions_.empty();
    if (!suggestions_.empty()) {
        selected_index_ = 0;
    }
}

void InputBar::apply_suggestion()
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(suggestions_.size())) return;

    *input_content_ = "/" + suggestions_[selected_index_].name + " ";
    show_suggestions_ = false;
    selected_index_ = -1;
}

Element InputBar::render_suggestion_list()
{
    if (suggestions_.empty()) return text("");

    Elements lines;
    for (int i = 0; i < static_cast<int>(suggestions_.size()); ++i) {
        bool selected = (i == selected_index_);

        auto marker = selected ? text(" > ") : text("   ");
        auto cmd = text("/" + suggestions_[i].name);
        auto desc = text("  " + suggestions_[i].description) | dim;

        Element line;
        if (selected) {
            line = hbox({
                marker,
                cmd | bold,
                desc | flex,
            }) | color(Color::Black) | bgcolor(Color::White);
        } else {
            line = hbox({
                marker,
                cmd | color(Color::White) | bold,
                desc | flex,
            }) | color(Color::GrayLight);
        }

        lines.push_back(line);
    }

    return vbox(std::move(lines)) | borderRounded | color(Color::GrayDark);
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
    return input_content_ ? *input_content_ : "";
}

void InputBar::clear()
{
    if (input_content_) input_content_->clear();
    show_suggestions_ = false;
}

void InputBar::focus()
{
    if (input_component_) {
        input_component_->TakeFocus();
    }
}

void InputBar::set_command_registry(CommandRegistry* registry)
{
    command_registry_ = registry;
}
