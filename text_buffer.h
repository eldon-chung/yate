#pragma once

#include <assert.h>

#include <string>
#include <vector>

class TextBuffer {
    std::vector<std::string> buffer;

  public:
    struct Cursor {
        size_t line;
        size_t col;

        enum Direction {
            UP = 0,
            RIGHT,
            DOWN,
            LEFT,
        };
    };

    Cursor cursor;

    TextBuffer() : buffer({""}), cursor({0, 0}) {}

    void insert_char(char c) {
        buffer.at(cursor.line).insert(cursor.col++, 1, c);
    }

    void insert_newline() {
        std::string next_line = buffer.at(cursor.line).substr(cursor.col);
        buffer.at(cursor.line).resize(cursor.col);
        buffer.insert(buffer.begin() + (long)cursor.line + 1,
                      std::move(next_line));

        // then set the cursor
        ++cursor.line;
        cursor.col = 0;
    }

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

    void move_cursor(uint32_t direction_idx) {
        switch (direction_idx) {
        case Cursor::LEFT: {
            if (cursor.col > 0) {
                --cursor.col;
            } else if (cursor.line > 0) {
                cursor.col = buffer.at(--cursor.line).size();
            }
        } break;
        case Cursor::UP: {
            if (cursor.line == 0) {
                cursor.col = 0;
            } else {
                cursor.col =
                    std::min(cursor.col, buffer.at(--cursor.line).size());
            }
        } break;
        case Cursor::RIGHT:
            assert(!buffer.empty());
            if (cursor.col == buffer.at(cursor.line).size() &&
                cursor.line + 1 < buffer.size()) {
                // move down one line
                cursor.col = 0;
                ++cursor.line;
            } else if (cursor.col < buffer.at(cursor.line).size()) {
                ++cursor.col;
            }
            break;
        case Cursor::DOWN:
            if (cursor.line + 1 < buffer.size()) {
                ++cursor.line;
                cursor.col =
                    std::min(cursor.col, buffer.at(cursor.line).size());
            } else if (cursor.line + 1 == buffer.size()) {
                cursor.col = buffer.at(cursor.line).size();
            }
            break;
        default:
            break;
        }
    }
};
