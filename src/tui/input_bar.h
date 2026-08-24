#pragma once

#include <ftxui/component/component.hpp>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "core/provider.h"

class CommandRegistry;

class InputBar {
public:
    struct SessionChoice {
        std::string id;
        std::string title;
        bool active{false};
        bool running{false};
    };

    InputBar();
    ftxui::Component build();

    std::string get_text() const;
    void clear();
    void focus();

    void set_on_submit(std::function<void(std::string)> callback);
    void add_to_history(const std::string& entry);
    void set_command_registry(CommandRegistry* registry);
    void set_models(const std::vector<ModelInfo>& models);
    void set_sessions(const std::vector<SessionChoice>& sessions);

private:
    struct Suggestion {
        std::string name;
        std::string description;
        bool is_model{false};
        bool is_session{false};
        bool session_active{false};
        bool session_running{false};
    };

    void update_suggestions(const std::string& query);
    void update_model_suggestions(const std::string& query);
    void update_session_suggestions(const std::string& query);
    void apply_suggestion();
    ftxui::Element render_suggestion_list();

    ftxui::Component container_;
    std::shared_ptr<std::string> input_content_;
    std::function<void(std::string)> on_submit_;
    std::vector<std::string> history_;
    int history_index_{-1};
    CommandRegistry* command_registry_{nullptr};

    std::vector<Suggestion> suggestions_;
    int selected_index_{-1};
    bool show_suggestions_{false};
    ftxui::Component input_component_;
    std::vector<ModelInfo> models_;
    std::vector<SessionChoice> sessions_;
};
