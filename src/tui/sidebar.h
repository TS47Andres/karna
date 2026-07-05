#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include "project/context.h"

class Sidebar {
public:
    Sidebar();
    ftxui::Component build();

    void set_project_context(const ProjectContext& ctx);
    void set_model(const std::string& model);
    void set_token_count(int prompt, int completion);

private:
    ProjectContext project_ctx_;
    std::string model_{"unknown"};
    int prompt_tokens_{0};
    int completion_tokens_{0};

    ftxui::Element render();
};
