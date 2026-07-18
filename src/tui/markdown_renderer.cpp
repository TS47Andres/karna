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

static std::string lower_ascii(std::string value)
{
    for (auto& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

static std::string strip_html_markup(const std::string& value)
{
    std::string result;
    bool in_tag = false;
    for (char character : value) {
        if (character == '<') {
            in_tag = true;
            continue;
        }
        if (character == '>') {
            in_tag = false;
            result.push_back(' ');
            continue;
        }
        if (!in_tag) {
            result.push_back(character);
        }
    }

    std::string decoded;
    for (size_t index = 0; index < result.size();) {
        if (result[index] == '&') {
            const size_t end = result.find(';', index);
            if (end != std::string::npos) {
                decoded += decode_entity(result.substr(index, end - index + 1));
                index = end + 1;
                continue;
            }
        }
        decoded.push_back(result[index++]);
    }

    std::string normalized;
    bool whitespace = false;
    for (char character : decoded) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            whitespace = true;
        } else {
            if (whitespace && !normalized.empty()) normalized.push_back(' ');
            normalized.push_back(character);
            whitespace = false;
        }
    }
    return normalized;
}

static std::string escape_table_cell(std::string value)
{
    size_t position = 0;
    while ((position = value.find('|', position)) != std::string::npos) {
        value.replace(position, 1, "\\|");
        position += 2;
    }
    return value;
}

static std::string html_table_to_markdown(const std::string& table)
{
    const std::string lower = lower_ascii(table);
    std::vector<std::vector<std::string>> rows;
    size_t row_position = 0;

    while (true) {
        const size_t row_start = lower.find("<tr", row_position);
        if (row_start == std::string::npos) break;
        const size_t row_open_end = lower.find('>', row_start);
        const size_t row_end = lower.find("</tr", row_open_end);
        if (row_open_end == std::string::npos || row_end == std::string::npos) break;

        const std::string row_lower = lower.substr(row_open_end + 1, row_end - row_open_end - 1);
        const std::string row_source = table.substr(row_open_end + 1, row_end - row_open_end - 1);
        std::vector<std::string> cells;
        size_t cell_position = 0;
        while (true) {
            const size_t th = row_lower.find("<th", cell_position);
            const size_t td = row_lower.find("<td", cell_position);
            size_t cell_start = std::min(
                th == std::string::npos ? row_lower.size() : th,
                td == std::string::npos ? row_lower.size() : td);
            if (cell_start == row_lower.size()) break;

            const bool is_header = row_lower.compare(cell_start, 3, "<th") == 0;
            const std::string close_tag = is_header ? "</th" : "</td";
            const size_t cell_open_end = row_lower.find('>', cell_start);
            const size_t cell_end = row_lower.find(close_tag, cell_open_end);
            if (cell_open_end == std::string::npos || cell_end == std::string::npos) break;

            cells.push_back(escape_table_cell(strip_html_markup(
                row_source.substr(cell_open_end + 1, cell_end - cell_open_end - 1))));
            cell_position = cell_end + close_tag.size();
        }

        if (!cells.empty()) rows.push_back(std::move(cells));
        row_position = row_end + 4;
    }

    if (rows.empty()) return table;

    const size_t columns = rows.front().size();
    std::ostringstream markdown;
    auto write_row = [&markdown, columns](const std::vector<std::string>& row) {
        markdown << "|";
        for (size_t column = 0; column < columns; ++column) {
            markdown << " " << (column < row.size() ? row[column] : "") << " |";
        }
        markdown << "\n";
    };

    write_row(rows.front());
    markdown << "|";
    for (size_t column = 0; column < columns; ++column) {
        markdown << " --- |";
    }
    markdown << "\n";
    for (size_t row = 1; row < rows.size(); ++row) write_row(rows[row]);
    return markdown.str();
}

static std::string normalize_html_tables(const std::string& markdown)
{
    const std::string lower = lower_ascii(markdown);
    std::string normalized;
    size_t position = 0;
    while (true) {
        const size_t table_start = lower.find("<table", position);
        if (table_start == std::string::npos) {
            normalized += markdown.substr(position);
            break;
        }
        normalized += markdown.substr(position, table_start - position);
        const size_t table_open_end = lower.find('>', table_start);
        const size_t table_end = lower.find("</table", table_open_end);
        if (table_open_end == std::string::npos || table_end == std::string::npos) {
            normalized += markdown.substr(table_start);
            break;
        }
        const size_t table_close_end = lower.find('>', table_end);
        const std::string table = markdown.substr(
            table_open_end + 1, table_end - table_open_end - 1);
        normalized += html_table_to_markdown(table);
        position = table_close_end == std::string::npos ? markdown.size() : table_close_end + 1;
    }
    return normalized;
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
            s.table_has_header = false;
            break;
        }
        case MD_BLOCK_THEAD:
            s.in_table_head = true;
            s.table_has_header = true;
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
            auto table = render_table(
                s.table_col_widths, s.table_cell_texts, s.table_has_header);
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

    if (s.in_table_cell_) {
        if (type == MD_TEXT_NORMAL || type == MD_TEXT_SOFTBR ||
            type == MD_TEXT_CODE || type == MD_TEXT_HTML) {
            s.current_cell_text_ += str;
        } else if (type == MD_TEXT_ENTITY) {
            s.current_cell_text_ += decode_entity(str);
        } else if (type == MD_TEXT_BR) {
            s.current_cell_text_ += ' ';
        }
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
        auto code_elem = text(str + " ") |
            bgcolor(Color::GrayDark) | color(Color::YellowLight);
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
    const std::string normalized_markdown = normalize_html_tables(markdown);

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

    md_parse(normalized_markdown.data(),
             static_cast<MD_SIZE>(normalized_markdown.size()), &parser, this);

    if (!state_.doc_result.empty())
        return vbox(std::move(state_.doc_result));
    if (!state_.block_stack.empty() && !state_.block_stack.back().empty())
        return vbox(std::move(state_.block_stack.back()));
    return text("");
}
