#pragma once

#include <assert.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include <utility>
#include <vector>

#include "text_buffer.h"
#include "util.h"

class TextPlane {
    friend class View;

    // should it just get a ptr to text buffer?
    enum class WrapStatus {
        WRAP,
        NOWRAP,
    };

    TextBuffer const *buffer_ptr;
    ncplane *line_number_plane_ptr;
    ncplane *plane_ptr;
    ncplane *cursor_plane_ptr;
    WrapStatus wrap_status;
    Point tl_corner;
    Point cursor;

  public:
    TextPlane(notcurses *nc_ptr, TextBuffer const *text_buffer_ptr,
              unsigned int num_rows, unsigned int num_cols)
        : buffer_ptr(text_buffer_ptr), wrap_status(WrapStatus::WRAP),
          tl_corner({0, 0}), cursor({0, 0}) {

        // create the text_plane
        ncplane_options text_plane_opts = {
            .x = 2, .rows = num_rows - 1, .cols = num_cols - 3};
        plane_ptr =
            ncplane_create(notcurses_stdplane(nc_ptr), &text_plane_opts);

        nccell text_plane_base_cell = {.channels = NCCHANNELS_INITIALIZER(
                                           0xff, 0xff, 0xff, 169, 169, 169)};
        ncplane_set_base_cell(plane_ptr, &text_plane_base_cell);

        // cursor plane opts
        ncplane_options cursor_plane_opts = {
            .y = 0, .x = 0, .rows = 1, .cols = 1};
        cursor_plane_ptr = ncplane_create(plane_ptr, &cursor_plane_opts);

        nccell cursor_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff)};

        ncplane_set_base_cell(cursor_plane_ptr, &cursor_plane_base_cell);

        // line number plane opts
        ncplane_options line_number_plane_opts = {
            .y = 0, .x = -2, .rows = num_rows - 1, .cols = 2};

        line_number_plane_ptr =
            ncplane_create(plane_ptr, &line_number_plane_opts);

        nccell line_number_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 66, 135, 245)};

        ncplane_set_base_cell(line_number_plane_ptr,
                              &line_number_plane_base_cell);
    }

    ~TextPlane() {
        ncplane_destroy(plane_ptr);
        ncplane_destroy(cursor_plane_ptr);
        ncplane_destroy(line_number_plane_ptr);
    }

    void render() {
        // make this vector a fixed array to avoid allocations?
        auto row_idxs = render_text();
        render_cursor(row_idxs);
        render_line_numbers(row_idxs);
    }

    TextPlane(TextPlane const &) = delete;
    TextPlane(TextPlane &&other)
        : buffer_ptr(std::exchange(other.buffer_ptr, nullptr)),
          line_number_plane_ptr(
              std::exchange(other.line_number_plane_ptr, nullptr)),
          plane_ptr(std::exchange(other.plane_ptr, nullptr)),
          cursor_plane_ptr(std::exchange(other.cursor_plane_ptr, nullptr)),
          wrap_status(other.wrap_status), tl_corner(other.tl_corner),
          cursor(other.cursor) {}
    TextPlane &operator=(TextPlane const &) = delete;
    TextPlane &operator=(TextPlane &&other) {
        TextPlane temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }

    friend void swap(TextPlane &a, TextPlane &b) {
        using std::swap;
        swap(a.buffer_ptr, b.buffer_ptr);
        swap(a.line_number_plane_ptr, b.line_number_plane_ptr);
        swap(a.plane_ptr, b.plane_ptr);
        swap(a.cursor_plane_ptr, b.cursor_plane_ptr);
        swap(a.wrap_status, b.wrap_status);
        swap(a.tl_corner, b.tl_corner);
        swap(a.cursor, b.cursor);
    }

  private:
    void render_line_numbers(std::vector<size_t> const &row_idxs) {
        ncplane_erase(line_number_plane_ptr);
        char out_str[5];
        for (size_t idx = 0; idx < row_idxs.size(); ++idx) {
            size_t local_offset = row_idxs[idx];
            snprintf(out_str, 3, "%zu", tl_corner.row + idx + 1);
            ncplane_putnstr_yx(line_number_plane_ptr, (int)(local_offset), 0, 3,
                               out_str);
        }
    }

    void render_cursor(std::vector<size_t> const &row_idxs) {
        // get the text_plane size
        auto [row_count, col_count] = get_plane_yx_dim();
        assert(cursor.row >= tl_corner.row);
        assert(cursor.row < tl_corner.row + row_count);

        if (wrap_status == WrapStatus::WRAP) {
            if (buffer_ptr->buffer.at(cursor.row).empty()) {
                ncplane_move_yx(cursor_plane_ptr,
                                (int)row_idxs.at(cursor.row - tl_corner.row),
                                0);
            } else {
                // TODO: not sure this is entirely correct
                size_t starting_row = row_idxs.at(cursor.row - tl_corner.row);
                starting_row += (cursor.col - tl_corner.col) / col_count;

                int target_col = (buffer_ptr->buffer.at(cursor.row).empty())
                                     ? 0
                                     : (int)(cursor.col % col_count);
                ncplane_move_yx(cursor_plane_ptr, (int)starting_row,
                                target_col);
            }
        } else {
            // TODO: nowrap case
        }
    }

    std::vector<size_t> render_text() {
        ncplane_erase(plane_ptr);

        // get the text_plane size
        auto [row_count, col_count] = get_plane_yx_dim();
        std::vector<size_t> line_rows;
        line_rows.reserve(row_count);

        size_t num_lines_output = 0;

        size_t curr_logical_row = tl_corner.row;
        size_t curr_logical_col = tl_corner.col;

        while (num_lines_output < row_count &&
               curr_logical_row < buffer_ptr->num_lines()) {
            // string_view is nice for tracking sizes
            if (curr_logical_col == 0) {
                line_rows.push_back(num_lines_output);
            }

            std::string_view curr_line =
                buffer_ptr->get_line_at(curr_logical_row);
            assert(tl_corner.col <= curr_line.size());
            curr_line = curr_line.substr(curr_logical_col);

            ncplane_putnstr_yx(plane_ptr, (int)num_lines_output, 0, col_count,
                               curr_line.data());
            ++num_lines_output;
            if (curr_logical_col + col_count < curr_line.size()) {
                curr_logical_col += col_count;
            } else {
                curr_logical_col = 0;
                ++curr_logical_row;
            }
        };

        return line_rows;
    }

    std::pair<unsigned int, unsigned int> get_yx_dim(ncplane *ptr) const {
        unsigned int num_rows, num_cols;
        ncplane_dim_yx(ptr, &num_rows, &num_cols);
        return {num_rows, num_cols};
    }

    std::pair<unsigned int, unsigned int> get_plane_yx_dim() const {
        return get_yx_dim(plane_ptr);
    }

    void move_cursor_left() {
        // update logical cursor
        if (cursor.col > 0) {
            --cursor.col;
        } else if (cursor.row > 0) {
            cursor.col = buffer_ptr->buffer.at(--cursor.row).size();
        }

        // now update tl_corner to chase it if needed
        if (cursor < tl_corner) {
            visual_scroll_up();
        }
    }

    void move_cursor_right() {
        assert(!buffer_ptr->buffer.empty());
        if (cursor.col == buffer_ptr->buffer.at(cursor.row).size() &&
            cursor.row + 1 < buffer_ptr->buffer.size()) {
            // move down one line
            cursor.col = 0;
            ++cursor.row;
        } else if (cursor.col < buffer_ptr->buffer.at(cursor.row).size()) {
            ++cursor.col;
        }

        // now update tl_corner to chase it if needed
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        Point br_corner = tl_corner + Point{num_rows - 1, num_cols};
        if (cursor >= br_corner) {
            visual_scroll_down();
        }
    }

    void move_cursor_down() {
        if (cursor.row + 1 == buffer_ptr->buffer.size()) {
            // special logic for the final row
            cursor.col = buffer_ptr->buffer.at(cursor.row).size();
        } else if (wrap_status == WrapStatus::WRAP) {
            // TODO: add jump to end logic
            assert(cursor.row + 1 < buffer_ptr->buffer.size());
            auto [num_rows, num_cols] = get_plane_yx_dim();
            size_t curr_line_size = buffer_ptr->buffer.at(cursor.row).size();

            if (curr_line_size == 0) {
                cursor.col = 0;
                ++cursor.row;
            } else if ((cursor.col / curr_line_size * curr_line_size) +
                           num_cols <=
                       curr_line_size) {
                cursor.col = std::min(cursor.col + num_cols, curr_line_size);
            } else {
                assert(cursor.row + 1 < buffer_ptr->buffer.size());
                ++cursor.row;
                cursor.col = std::min(cursor.col,
                                      buffer_ptr->buffer.at(cursor.row).size());
            }
        } else {
            // non-wrapping movement
            if (cursor.row + 1 < buffer_ptr->buffer.size()) {
                ++cursor.row;
                cursor.col = std::min(cursor.col,
                                      buffer_ptr->buffer.at(cursor.row).size());
            }
        }

        // now update tl_corner to chase it if needed
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        Point br_corner = tl_corner + Point{num_rows - 1, num_cols};
        if (cursor >= br_corner) {
            visual_scroll_down();
        }
    }

    void move_cursor_up() {
        if (cursor.row == 0) {
            // special case of jump to front
            cursor.col = 0;
        } else if (wrap_status == WrapStatus::WRAP) {
            auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
            if (cursor.col >= num_cols) {
                cursor.col -= num_cols;
            } else {
                assert(cursor.row > 0);
                --cursor.row;
                size_t line_size = buffer_ptr->buffer.at(cursor.row).size();
                size_t last_chunk_col = line_size / num_cols * line_size;
                assert(cursor.col < num_cols);
                cursor.col = std::min(cursor.col + last_chunk_col, line_size);
            }
        } else {
            // non-wrapping movement
            cursor.col = std::min(cursor.col,
                                  buffer_ptr->buffer.at(--cursor.row).size());
        }

        // now update tl_corner to chase it if needed
        if (cursor < tl_corner) {
            visual_scroll_up();
        }
    }

    void move_cursor_to(Point new_curs) {
        // need to chase the new cursor?
        cursor = new_curs;
    }

    void visual_scroll_up() {
        auto [num_rows, num_cols] = get_plane_yx_dim();
        if (wrap_status == WrapStatus::WRAP) {
            if (tl_corner.col == 0) {
                assert(tl_corner.row > 0);
                // move tl_corner up one row and get the last line
                --tl_corner.row;
                auto prev_line = buffer_ptr->get_line_at(tl_corner.row);

                tl_corner.col = (prev_line.empty()) ? 0
                                                    : (prev_line.size() - 1) /
                                                          num_cols * num_cols;

            } else {
                assert(tl_corner.col > 0);
                // move tl_corner back by one chunk
                assert(tl_corner.col % num_cols == 0);
                tl_corner.col -= num_cols;
            }
        }
        // TODO: what happens if nowrap
    }

    void visual_scroll_down() {
        auto [num_rows, num_cols] = get_plane_yx_dim();
        // TODO: fix this
        if (wrap_status == WrapStatus::WRAP) {
            if (cursor.col + num_cols >
                buffer_ptr->buffer.at(cursor.row).size()) {
                assert(tl_corner.row + 1 < buffer_ptr->buffer.size());
                tl_corner.col = 0;
                ++tl_corner.row;
            } else {
                tl_corner.col += num_cols;
            }
        }
        // TODO: what happens if nowrap
    }
};

class View {
    notcurses *nc_ptr;
    TextPlane text_plane;

    // eventually move this out into
    // its own UI element
    size_t starting_row;

    View(notcurses *nc, TextPlane t_plane)
        : nc_ptr(nc), text_plane(std::move(t_plane)), starting_row(0) {}

  public:
    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&other)
        : nc_ptr(std::exchange(other.nc_ptr, nullptr)),
          text_plane(std::move(other.text_plane)) {}

    View &operator=(View &&other) {
        View temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }
    ~View() { notcurses_stop(nc_ptr); }

    friend void swap(View &a, View &b) {
        using std::swap;
        swap(a.nc_ptr, b.nc_ptr);
        swap(a.text_plane, b.text_plane);
    }

    size_t get_starting_row() const { return starting_row; }

    static View &init_view(TextBuffer const *text_buffer_ptr) {
        // notcurses init
        static struct notcurses_options nc_options = {
            .flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR};
        notcurses *nc_ptr = notcurses_init(&nc_options, nullptr);

        // get the base ptr
        ncplane *std_plane_ptr = notcurses_stdplane(nc_ptr);

        unsigned int num_rows, num_cols;
        ncplane_dim_yx(std_plane_ptr, &num_rows, &num_cols);

        // now create the UI elements
        static View view(
            nc_ptr, TextPlane(nc_ptr, text_buffer_ptr, num_rows - 1, num_cols));
        return view;
    }

    void render() {
        // render the text plane
        text_plane.render();
        notcurses_render(nc_ptr);
    }

    notcurses *get_nc_ptr() const { return nc_ptr; }

    ncinput get_keypress() {
        // try getting user input:
        struct ncinput nc_input;
        notcurses_get(nc_ptr, nullptr, &nc_input);
        return nc_input;
    }

    Point get_text_cursor() const { return text_plane.cursor; }

    void move_cursor_left() { text_plane.move_cursor_left(); }
    void move_cursor_right() { text_plane.move_cursor_right(); }
    void move_cursor_down() { text_plane.move_cursor_down(); }
    void move_cursor_up() { text_plane.move_cursor_up(); }
    void move_cursor_to(Point new_curs) { text_plane.move_cursor_to(new_curs); }
    Point get_cursor() const { return text_plane.cursor; }
};
