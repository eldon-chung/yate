#pragma once

#include <assert.h>

#include <string>
#include <vector>

#include "util.h"

struct TextBuffer {
    std::vector<std::string> buffer;

  public:
    TextBuffer()
        : buffer({""}) {
    }

    void load_contents(std::string_view contents) {
        buffer.clear();
        while (true) {
            size_t newl_pos = contents.find('\n');
            buffer.push_back(std::string{contents.substr(0, newl_pos)});
            if (newl_pos == std::string::npos) {
                break;
            }
            contents = contents.substr(newl_pos + 1);
        }
    }

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

    std::string_view at(size_t starting_row) const {
        return buffer.at(starting_row);
    }

    size_t num_lines() const {
        return buffer.size();
    }

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

    Point replace_text_at(Point lp, Point rp, std::vector<std::string> lines) {
        remove_text_at(lp, rp);
        return insert_text_at(lp, std::move(lines));
    }

    void remove_text_at(Point lp, Point rp) {

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

    void insert_text_at(Point point, char ch) {
        insert_text_at(point, {{ch}});
    }

    std::vector<std::string> get_nth_line(size_t idx) const {
        return {buffer.at(idx)};
    }

    std::vector<std::string_view> get_view() const {
        std::vector<std::string_view> to_ret;
        to_ret.reserve(buffer.size());
        for (auto const &line : buffer) {
            to_ret.push_back(line);
        }

        return to_ret;
    }
};

inline const char *read_text_buffer(void *payload,
                                    [[maybe_unused]] uint32_t byte_offset,
                                    TSPoint position, uint32_t *bytes_read) {
    // fprintf(stderr, "requested position: %u, %u\n", position.row,
    //         position.column);

    TextBuffer *text_buffer_ptr = (TextBuffer *)payload;
    // I'm guessing I only need to use either of the 2?

    if (position.row >= text_buffer_ptr->num_lines() ||
        (position.row == text_buffer_ptr->num_lines() - 1 &&
         position.column >=
             text_buffer_ptr->get_nth_line(position.row).size())) {
        // fprintf(stderr, "returning EOF\n");
        *bytes_read = 0;
        return "\0";
    }

    if (position.column == text_buffer_ptr->at(position.row).size()) {
        // this __should__ be safe because it should point to somewhere in
        // globals, rather than point to stack
        // fprintf(stderr, "returning newline\n");
        *bytes_read = 1;
        return "\n";
    }

    assert(position.column < text_buffer_ptr->at(position.row).size());
    const char *corresponding_byte = text_buffer_ptr->at(position.row).data();
    corresponding_byte += position.column;
    *bytes_read =
        (uint32_t)text_buffer_ptr->at(position.row).size() - position.column;
    // fprintf(stderr, "giving str: %s\n", corresponding_byte);
    // fprintf(stderr, "bytes read: %u\n", *bytes_read);

    return corresponding_byte;
}
