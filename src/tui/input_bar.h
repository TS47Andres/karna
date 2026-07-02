#pragma once

#include <ftxui/component/component.hpp>
#include <string>
#include <functional>

class InputBar {
public:
    InputBar();
    ftxui::Component build();

    std::string get_text() const;
    void clear();
    void focus();

    void set_on_submit(std::function<void(std::string)> callback);
    void add_to_history(const std::string& entry);

private:
    ftxui::Component input_;
    std::function<void(std::string)> on_submit_;
    std::vector<std::string> history_;
    int history_index_{-1};
};
