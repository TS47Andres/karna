#pragma once

#include <ftxui/dom/elements.hpp>
#include <md4c.h>
#include <string>
#include <vector>
#include <memory>

class MarkdownRenderer {
public:
    MarkdownRenderer();
    ftxui::Element render(const std::string& markdown);

private:
    struct RenderState {
        std::vector<ftxui::Elements> block_stack;
        std::vector<ftxui::Elements> inline_stack;
        ftxui::Elements text_buffer;
        int heading_level{0};
        int list_counter{0};
        bool in_code_block{false};
        bool in_table_head{false};
        unsigned table_col_count{0};
        unsigned table_row_index{0};
        std::vector<unsigned> table_col_widths;
        std::vector<std::vector<std::string>> table_cell_texts;
        std::vector<std::string> table_row_cells;
        std::string current_cell_text_;
        std::string code_text_;
        bool in_table_cell_{false};
        ftxui::Elements doc_result;
        ftxui::Elements paragraph_lines_;
        bool track_lines_{false};
    };

    RenderState state_;

    void push_block();
    void pop_block(ftxui::Element elem);
    void push_inline();
    ftxui::Elements pop_inline(ftxui::Decorator decorator);
    void commit_inline_to_block();

    static int enter_block_cb(MD_BLOCKTYPE type, void* detail, void* userdata);
    static int leave_block_cb(MD_BLOCKTYPE type, void* detail, void* userdata);
    static int enter_span_cb(MD_SPANTYPE type, void* detail, void* userdata);
    static int leave_span_cb(MD_SPANTYPE type, void* detail, void* userdata);
    static int text_cb(MD_TEXTTYPE type, const MD_CHAR* str, MD_SIZE size, void* userdata);
};
