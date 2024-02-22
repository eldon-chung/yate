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

    std::vector<std::string> get_lines(Point lp, Point rp) {
        if (rp <= lp) {
            // nothing to return; should we instead return {""}?
            return {};
        }

        // how to handle case the points aren't well formed?
        // do we just crash?

        std::vector<std::string> to_return;
        if (lp.row == rp.row) {
            std::string_view line = buffer.at(lp.row);
            // bound to break if lp.col exceeds the value
            line = line.substr(lp.col, rp.col - lp.col);
            to_return.push_back(std::string(line));
            return to_return;
        }

        for (size_t idx = lp.row; idx <= rp.row; ++idx) {
            std::string_view line = buffer.at(idx);

            if (idx == rp.row) {
                line = line.substr(0, rp.col);
            }

            if (idx == lp.row) {
                line = line.substr(lp.col);
            }

            to_return.push_back(std::string(line));
        }

        return to_return;
    }

    void remove_selection_at(Point lp, Point rp) {

        if (lp.row == rp.row) {
            std::string right_half = buffer.at(lp.row).substr(rp.col);

            buffer.at(lp.row).resize(lp.col);

            // not sure the move does anything big here
            buffer.at(lp.row).append(std::move(right_half));
            return;
        }

        buffer.at(lp.row).resize(lp.col);
        buffer.at(rp.row) = buffer.at(rp.row).substr(rp.col);

        buffer.erase(buffer.begin() + (ssize_t)lp.row + 1,
                     buffer.begin() + (ssize_t)rp.row);

        // then delete one more line?
        insert_delete_at(lp);
    }

    Point insert_text_at(Point point, std::vector<std::string> lines) {
        // break the line at point
        assert(!lines.empty());

        if (lines.size() == 1) {
            std::string right_half = buffer.at(point.row).substr(point.col);
            buffer.at(point.row).resize(point.col);
            buffer.at(point.row).append(lines.front());
            buffer.at(point.row).append(right_half);
            return {point.row, point.col + lines.front().size()};
        }

        Point final_insertion_point = {point.row + lines.size() - 1,
                                       lines.back().size()};

        std::string right_half = buffer.at(point.row).substr(point.col);
        buffer.at(point.row).resize(point.col); // retains the old string

        lines.back().append(right_half);
        buffer.at(point.row).append(lines.front());
        // middle stuff
        buffer.insert(buffer.begin() + (ssize_t)point.row + 1,
                      lines.begin() + 1, lines.end());

        return final_insertion_point;
    }

    void insert_text_at(Point point, char ch) { insert_text_at(point, {{ch}}); }

    std::vector<std::string> at(size_t idx) const { return {buffer.at(idx)}; }
};
