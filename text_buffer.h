#pragma once

#include <assert.h>

#include <string>
#include <vector>

#include "util.h"

struct TextBuffer {
    std::vector<std::string> buffer;

  public:
    TextBuffer() : buffer({""}) {}

    void insert_char_at(Point cursor, char c) {
        buffer.at(cursor.row).insert(cursor.col++, 1, c);
    }

    void insert_newline_at(Point cursor) {
        std::string next_line = buffer.at(cursor.row).substr(cursor.col);
        buffer.at(cursor.row).resize(cursor.col);
        buffer.insert(buffer.begin() + (long)cursor.row + 1,
                      std::move(next_line));

        // then set the cursor
        ++cursor.row;
        cursor.col = 0;
    }

    void insert_backspace_at(Point cursor) {
        if (cursor.col > 0) {
            buffer.at(cursor.row).erase(--cursor.col, 1);

        } else if (cursor.row > 0) {
            cursor.col = buffer.at(--cursor.row).length();
            buffer.at(cursor.row).append(buffer.at(cursor.row + 1));
            buffer.erase(buffer.begin() + (long)cursor.row + 1);
        }
    }

    void insert_delete_at(Point cursor) {
        if (cursor.col < buffer.at(cursor.row).size()) {
            buffer.at(cursor.row).erase(cursor.col, 1);
        } else if (cursor.row + 1 < buffer.size()) {
            assert(cursor.col == buffer.at(cursor.row).size());
            buffer.at(cursor.row) += buffer.at(cursor.row + 1);
            buffer.erase(buffer.begin() + (long)cursor.row + 1);
        }
    }

    std::vector<std::string_view> get_n_lines_at(size_t starting_row,
                                                 size_t row_count) const {
        std::vector<std::string_view> to_ret;
        to_ret.reserve(row_count);

        for (size_t idx = 0;
             idx < row_count && idx + starting_row < buffer.size(); ++idx) {
            to_ret.push_back(buffer.at(starting_row + idx));
        }

        return to_ret;
    }

    std::string_view get_line_at(size_t starting_row) const {
        return buffer.at(starting_row);
    }

    size_t num_lines() const { return buffer.size(); }
};
