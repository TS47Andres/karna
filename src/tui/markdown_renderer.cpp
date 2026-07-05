#include "tui/markdown_renderer.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>
#include <string>
#include <memory>

using namespace ftxui;

MarkdownRenderer::MarkdownRenderer() {}

void MarkdownRenderer::push_block()
{
    state_.block_stack.emplace_back();
}

void MarkdownRenderer::pop_block(Element elem)
{
    if (state_.block_stack.empty())
        return;
    auto children = std::move(state_.block_stack.back());
    state_.block_stack.pop_back();
    if (state_.block_stack.empty())
        return;
    if (!children.empty()) {
        auto content = elem ? elem : hflow(std::move(children));
        state_.block_stack.back().push_back(std::move(content));
    } else if (elem) {
        state_.block_stack.back().push_back(std::move(elem));
    }
}

void MarkdownRenderer::push_inline()
{
    state_.inline_stack.emplace_back();
}

Elements MarkdownRenderer::pop_inline(Decorator decorator)
{
    if (state_.inline_stack.empty())
        return {};
    auto elems = std::move(state_.inline_stack.back());
    state_.inline_stack.pop_back();
    if (decorator) {
        for (auto& e : elems)
            e = e | decorator;
    }
    return elems;
}

static std::string decode_entity(const std::string& text)
{
    if (text == "&amp;") return "&";
    if (text == "&lt;") return "<";
    if (text == "&gt;") return ">";
    if (text == "&quot;") return "\"";
    if (text == "&#39;") return "'";
    if (text == "&nbsp;") return " ";
    if (text.size() > 3 && text[1] == '#') {
        int code = 0;
        std::istringstream iss(text.substr(2, text.size() - 3));
        iss >> code;
        if (code > 0 && code < 128)
            return std::string(1, static_cast<char>(code));
    }
    return text;
}

static void add_text_words(Elements& target, const std::string& str)
{
    std::string word;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == ' ') {
            if (!word.empty()) {
                target.push_back(text(word + " ") | color(Color::White));
                word.clear();
            }
        } else if (c == '\n') {
            if (!word.empty()) {
                target.push_back(text(word) | color(Color::White));
                word.clear();
            }
            target.push_back(separator());
        } else {
            word += c;
        }
    }
    if (!word.empty())
        target.push_back(text(word) | color(Color::White));
}

static Element render_code_block(const std::string& code)
{
    if (code.empty())
        return text("") | border | bgcolor(Color::GrayDark);
    Elements lines;
    std::string line;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '\n') {
            lines.push_back(text(line));
            line.clear();
        } else {
            line += code[i];
        }
    }
    if (!line.empty() || code.back() == '\n')
        lines.push_back(text(line));
    auto content = vbox(std::move(lines));
    return content | border | bgcolor(Color::GrayDark) | color(Color::YellowLight);
}

static Element render_table(const std::vector<unsigned>& col_widths,
                            const std::vector<std::vector<std::string>>& cell_texts,
                            bool has_header)
{
    Elements output;
    for (size_t r = 0; r < cell_texts.size(); ++r) {
        Elements row_cells;
        row_cells.push_back(text("│ ") | color(Color::White) | dim);
        for (size_t c = 0; c < col_widths.size(); ++c) {
            std::string cell_str;
            if (c < cell_texts[r].size())
                cell_str = cell_texts[r][c];
            unsigned w = (c < col_widths.size()) ? col_widths[c] : 0;
            if (cell_str.size() < w)
                cell_str.append(w - cell_str.size(), ' ');
            auto cell = text(cell_str) | color(Color::White);
            if (r == 0 && has_header)
                cell = cell | bold;
            row_cells.push_back(cell);
            if (c < col_widths.size() - 1)
                row_cells.push_back(text("│ ") | color(Color::White) | dim);
            else
                row_cells.push_back(text("│") | color(Color::White) | dim);
        }
        output.push_back(hbox(std::move(row_cells)));
        if (r == 0 && has_header) {
            Elements sep_cells;
            for (size_t c = 0; c < col_widths.size(); ++c) {
                std::string dashes;
                for (unsigned w = 0; w < col_widths[c]; ++w)
                    dashes += "─";
                sep_cells.push_back(text("─┼─") | color(Color::White) | dim);
                sep_cells.push_back(text(dashes) | color(Color::White) | dim);
            }
            sep_cells.push_back(text("─") | color(Color::White) | dim);
            output.push_back(hbox(std::move(sep_cells)));
        }
    }
    return vbox(std::move(output));
}

int MarkdownRenderer::enter_block_cb(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* self = static_cast<MarkdownRenderer*>(userdata);
    auto& s = self->state_;

    switch (type) {
        case MD_BLOCK_DOC:
            self->push_block();
            break;
        case MD_BLOCK_P:
        case MD_BLOCK_H:
        case MD_BLOCK_LI:
            s.paragraph_lines_.clear();
            s.track_lines_ = true;
            self->push_block();
            break;
        case MD_BLOCK_QUOTE:
            self->push_block();
            break;
        case MD_BLOCK_CODE:
            s.code_text_.clear();
            s.in_code_block = true;
            break;
        case MD_BLOCK_UL:
            s.list_counter = 0;
            self->push_block();
            break;
        case MD_BLOCK_OL: {
            auto* od = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            s.list_counter = static_cast<int>(od->start);
            self->push_block();
            break;
        }
        case MD_BLOCK_HR:
            s.block_stack.back().push_back(separator());
            break;
        case MD_BLOCK_TABLE: {
            auto* td = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
            s.table_col_count = td->col_count;
            s.table_col_widths.assign(s.table_col_count, 0);
            s.table_cell_texts.clear();
            s.table_row_cells.clear();
            s.in_table_head = false;
            break;
        }
        case MD_BLOCK_THEAD:
            s.in_table_head = true;
            break;
        case MD_BLOCK_TBODY:
            s.in_table_head = false;
            break;
        case MD_BLOCK_TR:
            s.table_row_cells.clear();
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            s.current_cell_text_.clear();
            s.in_table_cell_ = true;
            break;
        default:
            break;
    }
    return 0;
}

int MarkdownRenderer::leave_block_cb(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* self = static_cast<MarkdownRenderer*>(userdata);
    auto& s = self->state_;

    switch (type) {
        case MD_BLOCK_DOC: {
            s.doc_result = std::move(s.block_stack.back());
            s.block_stack.pop_back();
            break;
        }
        case MD_BLOCK_P: {
            s.track_lines_ = false;
            if (!s.block_stack.back().empty())
                s.paragraph_lines_.push_back(hflow(std::move(s.block_stack.back())));
            s.block_stack.pop_back();
            Elements lines;
            if (!s.paragraph_lines_.empty()) {
                for (auto& l : s.paragraph_lines_)
                    lines.push_back(std::move(l));
            }
            s.paragraph_lines_.clear();
            if (!lines.empty())
                s.block_stack.back().push_back(vbox(std::move(lines)));
            break;
        }
        case MD_BLOCK_H: {
            s.track_lines_ = false;
            if (!s.block_stack.back().empty())
                s.paragraph_lines_.push_back(hflow(std::move(s.block_stack.back())));
            auto* hd = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            int level = hd->level;
            s.block_stack.pop_back();
            static const Color heading_colors[] = {
                Color::CyanLight, Color::BlueLight, Color::MagentaLight,
                Color::White, Color::GrayLight, Color::GrayDark
            };
            int idx = std::max(0, std::min(level - 1, 5));
            Elements lines;
            for (auto& l : s.paragraph_lines_)
                lines.push_back(std::move(l));
            s.paragraph_lines_.clear();
            auto content = vbox(std::move(lines)) | bold | color(heading_colors[idx]);
            s.block_stack.back().push_back(std::move(content));
            break;
        }
        case MD_BLOCK_CODE: {
            s.in_code_block = false;
            auto code_elem = render_code_block(s.code_text_);
            s.code_text_.clear();
            s.block_stack.back().push_back(std::move(code_elem));
            break;
        }
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: {
            auto children = std::move(s.block_stack.back());
            s.block_stack.pop_back();
            s.block_stack.back().push_back(vbox(std::move(children)));
            s.list_counter = 0;
            break;
        }
        case MD_BLOCK_LI: {
            s.track_lines_ = false;
            if (!s.block_stack.back().empty())
                s.paragraph_lines_.push_back(hflow(std::move(s.block_stack.back())));
            s.block_stack.pop_back();
            std::string marker;
            if (s.list_counter > 0) {
                marker = std::to_string(s.list_counter) + ".";
                s.list_counter++;
            } else {
                marker = " •";
            }
            Elements lines;
            for (auto& l : s.paragraph_lines_)
                lines.push_back(std::move(l));
            s.paragraph_lines_.clear();
            Elements row;
            row.push_back(text(marker) | bold | color(Color::White));
            row.push_back(text(" "));
            if (!lines.empty())
                row.push_back(vbox(std::move(lines)));
            s.block_stack.back().push_back(hbox(std::move(row)));
            break;
        }
        case MD_BLOCK_QUOTE: {
            auto children = std::move(s.block_stack.back());
            s.block_stack.pop_back();
            auto content = vbox(std::move(children)) | border | dim;
            s.block_stack.back().push_back(std::move(content));
            break;
        }
        case MD_BLOCK_HR:
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            s.in_table_cell_ = false;
            if (s.table_row_cells.size() < s.table_col_widths.size()) {
                unsigned w = static_cast<unsigned>(s.current_cell_text_.size());
                if (w > s.table_col_widths[s.table_row_cells.size()])
                    s.table_col_widths[s.table_row_cells.size()] = w;
            }
            s.table_row_cells.push_back(s.current_cell_text_);
            s.current_cell_text_.clear();
            break;
        }
        case MD_BLOCK_TR: {
            while (s.table_row_cells.size() < s.table_col_count)
                s.table_row_cells.emplace_back();
            s.table_cell_texts.push_back(s.table_row_cells);
            s.table_row_cells.clear();
            break;
        }
        case MD_BLOCK_TABLE: {
            auto table = render_table(s.table_col_widths, s.table_cell_texts, false);
            s.block_stack.back().push_back(std::move(table));
            break;
        }
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        default:
            break;
    }
    return 0;
}

int MarkdownRenderer::enter_span_cb(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* self = static_cast<MarkdownRenderer*>(userdata);
    auto& s = self->state_;

    if (s.in_code_block) return 0;
    self->push_inline();
    return 0;
}

int MarkdownRenderer::leave_span_cb(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* self = static_cast<MarkdownRenderer*>(userdata);
    auto& s = self->state_;

    if (s.in_code_block) return 0;

    Decorator decorator = nullptr;
    switch (type) {
        case MD_SPAN_EM:
            decorator = dim;
            break;
        case MD_SPAN_STRONG:
            decorator = bold;
            break;
        case MD_SPAN_CODE:
            decorator = [](Element e) {
                return e | bgcolor(Color::GrayDark) | color(Color::YellowLight);
            };
            break;
        case MD_SPAN_DEL:
            decorator = strikethrough;
            break;
        case MD_SPAN_U:
            decorator = underlined;
            break;
        case MD_SPAN_A:
            decorator = [](Element e) {
                return e | underlined | color(Color::CyanLight);
            };
            break;
        case MD_SPAN_IMG:
            decorator = [](Element e) { return e | dim; };
            break;
        default:
            break;
    }

    auto elems = self->pop_inline(decorator);
    if (!s.inline_stack.empty()) {
        for (auto& e : elems)
            s.inline_stack.back().push_back(std::move(e));
    } else if (!s.block_stack.empty()) {
        for (auto& e : elems)
            s.block_stack.back().push_back(std::move(e));
    }
    return 0;
}

int MarkdownRenderer::text_cb(MD_TEXTTYPE type, const MD_CHAR* raw, MD_SIZE size, void* userdata)
{
    auto* self = static_cast<MarkdownRenderer*>(userdata);
    auto& s = self->state_;
    std::string str(raw, size);

    if (s.in_code_block) {
        if (type == MD_TEXT_CODE || type == MD_TEXT_NORMAL)
            s.code_text_ += str;
        return 0;
    }

    if (type == MD_TEXT_NORMAL || type == MD_TEXT_SOFTBR) {
        if (s.in_table_cell_)
            s.current_cell_text_ += str;
        auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
        add_text_words(target, str);
        return 0;
    }

    if (type == MD_TEXT_BR) {
        if (s.track_lines_) {
            auto& target = s.block_stack.back();
            if (!target.empty() || !s.paragraph_lines_.empty()) {
                s.paragraph_lines_.push_back(hflow(std::move(target)));
                target.clear();
            } else {
                s.paragraph_lines_.push_back(hflow(Elements{}));
            }
        } else {
            auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
            target.push_back(text(" "));
        }
        return 0;
    }

    if (type == MD_TEXT_CODE) {
        if (s.in_table_cell_)
            s.current_cell_text_ += str;
        auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
        auto code_elem = text(str) | bgcolor(Color::GrayDark) | color(Color::YellowLight);
        target.push_back(std::move(code_elem));
        return 0;
    }

    if (type == MD_TEXT_ENTITY) {
        auto decoded = decode_entity(str);
        auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
        add_text_words(target, decoded);
        return 0;
    }

    if (type == MD_TEXT_NULLCHAR) {
        auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
        target.push_back(text("\xEF\xBF\xBD") | color(Color::White));
        return 0;
    }

    if (type == MD_TEXT_HTML) {
        auto& target = s.inline_stack.empty() ? s.block_stack.back() : s.inline_stack.back();
        target.push_back(text(str) | dim | color(Color::White));
        return 0;
    }

    return 0;
}

Element MarkdownRenderer::render(const std::string& markdown)
{
    state_ = RenderState{};
    state_.block_stack.emplace_back();

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_HARD_SOFT_BREAKS;
    parser.enter_block = enter_block_cb;
    parser.leave_block = leave_block_cb;
    parser.enter_span = enter_span_cb;
    parser.leave_span = leave_span_cb;
    parser.text = text_cb;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, this);

    if (!state_.doc_result.empty())
        return vbox(std::move(state_.doc_result));
    if (!state_.block_stack.empty() && !state_.block_stack.back().empty())
        return vbox(std::move(state_.block_stack.back()));
    return text("");
}
