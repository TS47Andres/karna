#include "tui/input_bar.h"
#include "core/command.h"
#include "core/provider.h"
#include <ftxui/component/component_options.hpp>
#include <algorithm>
#include <cctype>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace ftxui;

namespace {

bool starts_with_case_insensitive(const std::string& value, const std::string& prefix)
{
    if (prefix.size() > value.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool equal_case_insensitive(const std::string& left, const std::string& right)
{
    return left.size() == right.size() && starts_with_case_insensitive(left, right);
}

} // namespace

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
    option.cursor_position = &cursor_position_;
    option.transform = [](InputState state) {
        return state.element | bgcolor(Color::Default) | color(Color::White);
    };
    option.on_change = [this]() {
        update_suggestions_for_current_input();
    };

    auto input = Input(input_content_.get(), option);
    input_component_ = input;

    auto input_with_history = CatchEvent(input, [this](Event event) {
        if ((event == Event::ArrowUp || event == Event::ArrowUpCtrl) &&
            !show_suggestions_ && !history_.empty()) {
            if (history_index_ == -1) {
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                --history_index_;
            }
            *input_content_ = history_[history_index_];
            cursor_position_ = static_cast<int>(input_content_->size());
            return true;
        }
        if ((event == Event::ArrowDown || event == Event::ArrowDownCtrl) &&
            !show_suggestions_ && history_index_ >= 0) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                ++history_index_;
                *input_content_ = history_[history_index_];
                cursor_position_ = static_cast<int>(input_content_->size());
            } else {
                history_index_ = -1;
                input_content_->clear();
                cursor_position_ = 0;
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
            if (event == Event::Tab && selected_index_ >= 0) {
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
                bool sensitive = text.rfind("/connect ", 0) == 0 ||
                    text.rfind("/connect-exa ", 0) == 0;
                if (!sensitive) {
                    history_.push_back(text);
                }
                history_index_ = -1;
                if (on_submit_) on_submit_(text);
            }
            input_content_->clear();
            cursor_position_ = 0;
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
    suggestions_.clear();
    selected_index_ = -1;

    std::string lower_query;
    for (auto c : query) lower_query += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (command_registry_) {
        auto all_commands = command_registry_->all();

        for (const auto* cmd : all_commands) {
            std::string cmd_lower;
            for (auto c : cmd->name()) cmd_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (cmd_lower.find(lower_query) == 0) {
                Suggestion suggestion;
                suggestion.name = cmd->name();
                suggestion.description = cmd->description();
                suggestion.display = "/" + cmd->name();
                suggestion.completion = "/" + cmd->name() + " ";
                suggestions_.push_back(std::move(suggestion));
                if (static_cast<int>(suggestions_.size()) >= 5) break;
            }
        }
    }

    show_suggestions_ = !suggestions_.empty();
    if (!suggestions_.empty()) {
        selected_index_ = 0;
    }
}

void InputBar::update_suggestions_for_current_input()
{
    if (!input_content_ || input_content_->empty() || (*input_content_)[0] != '/') {
        show_suggestions_ = false;
        selected_index_ = -1;
        return;
    }

    const std::string& text = *input_content_;
    const size_t command_end = text.find_first_of(" \t\r\n", 1);
    if (command_end == std::string::npos) {
        update_suggestions(text.substr(1));
        return;
    }

    const std::string command_name = text.substr(1, command_end - 1);
    size_t query_start = command_end;
    while (query_start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[query_start]))) {
        ++query_start;
    }

    if (command_name == "model") {
        update_model_suggestions(text.substr(query_start));
        return;
    }
    if (command_name == "sessions") {
        update_session_suggestions(text.substr(query_start));
        return;
    }

    update_argument_suggestions(command_name, command_end, text);
}

void InputBar::update_argument_suggestions(const std::string& command_name,
                                           size_t command_end,
                                           const std::string& text)
{
    suggestions_.clear();
    selected_index_ = -1;

    const auto* command = command_registry_ ? command_registry_->find(command_name) : nullptr;
    if (!command) {
        show_suggestions_ = false;
        return;
    }

    auto options = command->autocomplete_options();
    if (options.empty()) {
        show_suggestions_ = false;
        return;
    }

    size_t args_start = command_end;
    while (args_start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[args_start]))) {
        ++args_start;
    }

    size_t token_start = text.size();
    while (token_start > args_start &&
           !std::isspace(static_cast<unsigned char>(text[token_start - 1]))) {
        --token_start;
    }

    std::vector<std::string> completed_tokens;
    size_t token = args_start;
    while (token < token_start) {
        while (token < token_start &&
               std::isspace(static_cast<unsigned char>(text[token]))) {
            ++token;
        }
        if (token >= token_start) break;

        size_t token_end = token;
        while (token_end < token_start &&
               !std::isspace(static_cast<unsigned char>(text[token_end]))) {
            ++token_end;
        }
        completed_tokens.push_back(text.substr(token, token_end - token));
        token = token_end;
    }

    const std::vector<CommandAutocompleteOption>* current_options = &options;
    for (size_t i = 0; i < completed_tokens.size(); ++i) {
        const auto& completed = completed_tokens[i];
        const auto it = std::find_if(current_options->begin(), current_options->end(),
            [&completed](const auto& option) {
                return equal_case_insensitive(option.value, completed);
            });
        if (it == current_options->end()) {
            show_suggestions_ = false;
            return;
        }
        current_options = &it->children;
        if (current_options->empty() && i + 1 < completed_tokens.size()) {
            show_suggestions_ = false;
            return;
        }
    }

    const std::string query = text.substr(token_start);
    const std::string prefix = text.substr(0, token_start);
    for (const auto& option : *current_options) {
        if (!starts_with_case_insensitive(option.value, query)) continue;

        Suggestion suggestion;
        suggestion.name = option.value;
        suggestion.description = option.description;
        suggestion.is_argument = true;
        suggestion.has_children = !option.children.empty();
        suggestion.display = prefix + option.value;
        suggestion.completion = suggestion.display + (suggestion.has_children ? " " : "");
        suggestions_.push_back(std::move(suggestion));
        if (static_cast<int>(suggestions_.size()) >= 8) break;
    }

    show_suggestions_ = !suggestions_.empty();
    if (show_suggestions_) selected_index_ = 0;
}

void InputBar::update_model_suggestions(const std::string& query)
{
    suggestions_.clear();
    selected_index_ = -1;

    std::string lower_query;
    for (auto c : query) {
        lower_query += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (const auto& model : models_) {
        std::string searchable = model.id + " " + model.name + " " + model.owned_by;
        std::string lower_searchable;
        for (auto c : searchable) {
            lower_searchable += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower_query.empty() || lower_searchable.find(lower_query) != std::string::npos) {
            std::string description = model.name.empty() ? model.owned_by : model.name;
            if (model.context_length > 0) {
                description += " (" + std::to_string(model.context_length) + " context)";
            }
            suggestions_.push_back({model.id, description, true});
            suggestions_.back().display = "/model " + model.id;
            suggestions_.back().completion = "/model " + model.id + " ";
            if (static_cast<int>(suggestions_.size()) >= 8) break;
        }
    }

    show_suggestions_ = !suggestions_.empty();
    if (show_suggestions_) {
        selected_index_ = 0;
    }
}

void InputBar::update_session_suggestions(const std::string& query)
{
    suggestions_.clear();
    selected_index_ = -1;

    std::string lower_query;
    for (const auto c : query) {
        lower_query += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (command_registry_) {
        if (const auto* command = command_registry_->find("sessions")) {
            for (const auto& option : command->autocomplete_options()) {
                if (!starts_with_case_insensitive(option.value, query)) continue;

                Suggestion suggestion;
                suggestion.name = option.value;
                suggestion.description = option.description;
                suggestion.is_argument = true;
                suggestion.has_children = !option.children.empty();
                suggestion.display = "/sessions " + option.value;
                suggestion.completion = suggestion.display + (suggestion.has_children ? " " : "");
                suggestions_.push_back(std::move(suggestion));
                if (static_cast<int>(suggestions_.size()) >= 8) break;
            }
        }
    }

    for (const auto& session : sessions_) {
        std::string searchable = session.id + " " + session.title;
        std::string lower_searchable;
        for (const auto c : searchable) {
            lower_searchable += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!lower_query.empty() && lower_searchable.find(lower_query) == std::string::npos) {
            continue;
        }

        std::string description = session.title.empty() ? "Untitled session" : session.title;
        suggestions_.push_back({
            session.id,
            description,
            false,
            true,
            session.active,
            session.running
        });
        suggestions_.back().display = "/sessions " + session.id;
        suggestions_.back().completion = "/sessions " + session.id + " ";
        if (static_cast<int>(suggestions_.size()) >= 8) break;
    }

    show_suggestions_ = !suggestions_.empty();
    if (show_suggestions_) selected_index_ = 0;
}

void InputBar::apply_suggestion()
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(suggestions_.size())) return;

    const Suggestion suggestion = suggestions_[selected_index_];
    *input_content_ = suggestion.completion;
    cursor_position_ = static_cast<int>(input_content_->size());
    selected_index_ = -1;

    if (suggestion.is_argument && suggestion.has_children) {
        update_suggestions_for_current_input();
    } else if (!suggestion.is_model && !suggestion.is_session && !suggestion.is_argument) {
        // Completing a command creates its next argument context immediately.
        update_suggestions_for_current_input();
    } else {
        show_suggestions_ = false;
    }
}

Element InputBar::render_suggestion_list()
{
    if (suggestions_.empty()) return text("");

    Elements lines;
    for (int i = 0; i < static_cast<int>(suggestions_.size()); ++i) {
        bool selected = (i == selected_index_);

        auto marker = selected ? text(" > ") : text("   ");
        const auto& suggestion = suggestions_[i];
        std::string completion_label;
        if (suggestion.is_argument) {
            completion_label = suggestion.display;
        } else if (suggestion.is_session) {
            completion_label = "/sessions " + suggestion.name;
        } else if (suggestion.is_model) {
            completion_label = "/model " + suggestion.name;
        } else {
            completion_label = "/" + suggestion.name;
        }
        auto cmd = text(completion_label);
        auto desc = text("  " + suggestions_[i].description) | dim;

        Element line;
        if (selected) {
            Elements selected_parts = {
                marker,
                cmd | bold,
                desc | flex,
            };
            if (suggestion.is_session) {
                const std::string state = suggestion.session_active && suggestion.session_running
                    ? "  CURRENT · RUNNING"
                    : (suggestion.session_active
                        ? "  CURRENT"
                        : (suggestion.session_running ? "  RUNNING" : ""));
                if (!state.empty()) {
                    selected_parts.push_back(text(state) | bold);
                }
            }
            line = hbox(std::move(selected_parts)) | color(Color::Black) | bgcolor(Color::White);
        } else if (suggestion.is_session) {
            Color state_color = suggestion.session_active
                ? Color::Green
                : (suggestion.session_running ? Color::Cyan : Color::GrayLight);
            std::string state;
            if (suggestion.session_active && suggestion.session_running) state = "  CURRENT · RUNNING";
            else if (suggestion.session_active) state = "  CURRENT";
            else if (suggestion.session_running) state = "  RUNNING";
            line = hbox({
                marker,
                cmd | color(Color::White) | bold,
                desc | flex,
                text(state) | color(state_color) | bold,
            }) | color(Color::GrayLight);
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
    cursor_position_ = 0;
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
    update_suggestions_for_current_input();
}

void InputBar::set_models(const std::vector<ModelInfo>& models)
{
    models_ = models;
    update_suggestions_for_current_input();
}

void InputBar::set_sessions(const std::vector<SessionChoice>& sessions)
{
    sessions_ = sessions;
    update_suggestions_for_current_input();
}
