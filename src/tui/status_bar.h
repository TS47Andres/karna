#pragma once

#include <ftxui/component/component.hpp>
#include <string>

class StatusBar {
public:
    StatusBar();
    ftxui::Component build();

    void set_model(const std::string& model);
    void set_token_count(int prompt, int completion);
    void set_status(const std::string& status);
    void set_typing(bool typing);
    bool is_typing() const;

private:
    std::string model_;
    int prompt_tokens_{0};
    int completion_tokens_{0};
    std::string status_;
    bool typing_{false};

    ftxui::Element render();
};
