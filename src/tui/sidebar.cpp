#include "tui/sidebar.h"
#include <filesystem>

using namespace ftxui;

Sidebar::Sidebar()
{}

Component Sidebar::build()
{
    return Renderer([this] { return render(); });
}

void Sidebar::set_project_context(const ProjectContext& ctx)
{
    project_ctx_ = ctx;
}

void Sidebar::set_model(const std::string& model)
{
    model_ = model;
}

void Sidebar::set_token_count(int prompt, int completion)
{
    prompt_tokens_ = prompt;
    completion_tokens_ = completion;
}

Element Sidebar::render()
{
    // Extract project name from root path
    std::string project_name = "Karna";
    if (!project_ctx_.root_path.empty()) {
        try {
            project_name = std::filesystem::path(project_ctx_.root_path).filename().string();
            if (project_name.empty()) {
                project_name = std::filesystem::path(project_ctx_.root_path).parent_path().filename().string();
            }
        } catch (...) {
            // fallback
        }
    }

    // Colors
    auto color_header = color(Color::White) | bold;
    auto color_label = color(Color::GrayLight);
    auto color_value = color(Color::White);
    auto color_dim = color(Color::GrayDark);

    // Section 1: Project Metadata
    Elements project_elements;
    project_elements.push_back(text(" PROJECT ") | color_header);
    project_elements.push_back(separator() | color_dim);
    project_elements.push_back(hbox({ text("Name: ") | color_label, text(project_name) | color_value }));
    
    // Truncate path if too long
    std::string path_str = project_ctx_.root_path;
    if (path_str.size() > 24) {
        path_str = "..." + path_str.substr(path_str.size() - 21);
    }
    project_elements.push_back(hbox({ text("Path: ") | color_label, text(path_str) | color_dim }));

    if (project_ctx_.has_git) {
        project_elements.push_back(hbox({ text("Git : ") | color_label, text(project_ctx_.git_branch) | color(Color::Green) | bold }));
    } else {
        project_elements.push_back(hbox({ text("Git : ") | color_label, text("none") | color_dim }));
    }

    // Section 2: Session Info
    Elements session_elements;
    session_elements.push_back(text(" SESSION ") | color_header);
    session_elements.push_back(separator() | color_dim);
    
    // Truncate model if too long
    std::string model_str = model_;
    if (model_str.size() > 24) {
        model_str = model_str.substr(0, 21) + "...";
    }
    session_elements.push_back(hbox({ text("Model: ") | color_label, text(model_str) | color_value }));
    session_elements.push_back(hbox({ text("Prompt: ") | color_label, text(std::to_string(prompt_tokens_)) | color_dim }));
    session_elements.push_back(hbox({ text("Compl : ") | color_label, text(std::to_string(completion_tokens_)) | color_dim }));
    session_elements.push_back(hbox({ text("Total : ") | color_label, text(std::to_string(prompt_tokens_ + completion_tokens_)) | color_value }));

    // Section 3: Keys Guide
    Elements keys_elements;
    keys_elements.push_back(text(" SHORTCUTS ") | color_header);
    keys_elements.push_back(separator() | color_dim);
    
    auto key_line = [&](const std::string& key, const std::string& desc) {
        return hbox({
            text(key) | color_value | bold | size(WIDTH, EQUAL, 10),
            text(desc) | color_dim
        });
    };

    keys_elements.push_back(key_line("Enter", "Send msg"));
    keys_elements.push_back(key_line("Up/Down", "History"));
    keys_elements.push_back(key_line("Esc/F5", "Abort/Exit"));
    keys_elements.push_back(key_line("/clear", "Clear chat"));
    keys_elements.push_back(key_line("/help", "List cmds"));

    return vbox({
        vbox(std::move(project_elements)) | borderRounded | color_dim,
        vbox(std::move(session_elements)) | borderRounded | color_dim,
        vbox(std::move(keys_elements)) | borderRounded | color_dim,
        filler() // Fill remaining vertical space
    }) | size(WIDTH, EQUAL, 35);
}
