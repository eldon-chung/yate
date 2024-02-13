#pragma once

#include <string>
#include <vector>

class TextBuffer {
    std::vector<std::string> buffer;

  public:
    struct Cursor {
        size_t line;
        size_t col;
    };

    Cursor cursor;

    TextBuffer() : buffer({""}), cursor({0, 0}) {}

    void insert_char(char c) {
        buffer.at(cursor.line).insert(cursor.col, 1, c);
    }

    void insert_newline() {}

    void insert_backspace() {}

    void insert_delete() {}

    std::vector<std::string_view> get_n_lines(size_t starting_row,
                                              size_t row_count) const {
        std::vector<std::string_view> to_ret;
        to_ret.reserve(row_count);

        for (size_t idx = 0;
             idx < row_count && idx + starting_row < buffer.size(); ++idx) {
            to_ret.push_back(buffer.at(starting_row + idx));
        }

        return to_ret;
    }

    size_t num_lines() const { return buffer.size(); }

    Cursor const &get_cursor() const { return cursor; }
};
