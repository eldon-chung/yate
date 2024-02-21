#pragma once

#include <assert.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include <algorithm>
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
    Point br_corner; // exclusive range that we also maintain
    Point cursor;
    std::optional<Point> anchor_cursor;

  public:
    TextPlane(notcurses *nc_ptr, TextBuffer const *text_buffer_ptr,
              unsigned int num_rows, unsigned int num_cols)
        : buffer_ptr(text_buffer_ptr), wrap_status(WrapStatus::WRAP),
          tl_corner({0, 0}), br_corner({0, 1}), cursor({0, 0}),
          anchor_cursor(std::nullopt) {

        static const int num_digits = 4;

        // create the text_plane
        ncplane_options text_plane_opts = {.x = num_digits,
                                           .rows = num_rows - 1,
                                           .cols = num_cols - num_digits - 1};
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
            .y = 0, .x = -num_digits, .rows = num_rows - 1, .cols = num_digits};

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

    TextPlane(TextPlane const &) = delete;
    TextPlane(TextPlane &&other)
        : buffer_ptr(std::exchange(other.buffer_ptr, nullptr)),
          line_number_plane_ptr(
              std::exchange(other.line_number_plane_ptr, nullptr)),
          plane_ptr(std::exchange(other.plane_ptr, nullptr)),
          cursor_plane_ptr(std::exchange(other.cursor_plane_ptr, nullptr)),
          wrap_status(other.wrap_status), tl_corner(other.tl_corner),
          br_corner(other.br_corner), cursor(other.cursor),
          anchor_cursor(other.anchor_cursor) {}
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
        swap(a.br_corner, b.br_corner);
        swap(a.cursor, b.cursor);
        swap(a.anchor_cursor, b.anchor_cursor);
    }

    void render() {
        // make this vector a fixed array to avoid allocations?
        auto row_idxs = render_text();
        render_cursor(row_idxs);
        render_selection(row_idxs);
        render_line_numbers(row_idxs);
    }

  private:
    void render_selection(std::vector<size_t> const &row_idxs) {
        if (!anchor_cursor) {
            return;
        }

        auto [row_count, col_count] = get_plane_yx_dim();

        size_t num_lines_output = 0;
        size_t curr_logical_row = tl_corner.row;
        size_t curr_logical_col = tl_corner.col;
        size_t row_indxs_idx = 1; // sigh naming

        auto [lp, rp] = std::minmax(cursor, *anchor_cursor);

        while (num_lines_output < row_count &&
               curr_logical_row < buffer_ptr->num_lines()) {

            if (curr_logical_row > rp.row ||
                (curr_logical_row == rp.row && curr_logical_col >= rp.col)) {
                return;
            }

            if (curr_logical_row >= lp.row && curr_logical_row <= rp.row) {
                size_t visual_col_l = 0;
                size_t visual_col_r = col_count;

                // if on same row and visual chunk
                if (curr_logical_row == lp.row &&
                    (curr_logical_col <= lp.col)) {
                    visual_col_l =
                        std::max(lp.col, curr_logical_col) - curr_logical_col;
                }

                if (curr_logical_row == rp.row && curr_logical_col <= rp.col) {
                    visual_col_r =
                        std::min(rp.col - curr_logical_col, (size_t)col_count);
                }

                if (visual_col_l < visual_col_r) {
                    ncplane_stain(
                        plane_ptr, (int)num_lines_output, (int)visual_col_l, 1,
                        (unsigned int)(visual_col_r - visual_col_l),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff));
                }
            }

            ++num_lines_output;

            if (row_indxs_idx < row_idxs.size() &&
                num_lines_output >= row_idxs[row_indxs_idx]) {
                ++curr_logical_row;
                curr_logical_col = 0;
                ++row_indxs_idx;
            } else {
                curr_logical_col += col_count;
            }
        }
    }

    void render_line_numbers(std::vector<size_t> const &row_idxs) {
        ncplane_erase(line_number_plane_ptr);
        char out_str[5];
        for (size_t idx = 0; idx < row_idxs.size(); ++idx) {
            size_t local_offset = row_idxs[idx];
            snprintf(out_str, 5, "%zu", tl_corner.row + idx + 1);
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

        line_rows.push_back(0);

        while (num_lines_output < row_count &&
               curr_logical_row < buffer_ptr->num_lines()) {

            if (curr_logical_col == 0 && num_lines_output > 0) {
                line_rows.push_back(num_lines_output);
            }

            std::string_view curr_line =
                buffer_ptr->get_line_at(curr_logical_row);
            assert(tl_corner.col <= curr_line.size());

            int num_written = ncplane_putnstr_yx(
                plane_ptr, (int)num_lines_output, 0, col_count,
                curr_line.data() + curr_logical_col);

            if (num_written < 0) {
                // handle error cases?
                std::cerr << "this should not have happened" << std::endl;
                exit(1);
            }

            ++num_lines_output;
            br_corner.row = curr_logical_row;
            br_corner.col = curr_logical_col + col_count;

            // is there some row shenanigan we need to do

            if (curr_logical_col + col_count < curr_line.size()) {
                curr_logical_col += col_count;
            } else {
                curr_logical_col = 0;
                ++curr_logical_row;
            }
        }

        if (line_rows.empty()) {
            line_rows.push_back(0);
        }

        // set br_corner only when screen is filled
        if (num_lines_output < row_count) {
            ++br_corner.row;
            br_corner.col = col_count;
        }

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
        cursor = move_point_left(cursor);
        // now update tl_corner to chase it if needed
        if (cursor < tl_corner) {
            visual_scroll_up();
        }
    }

    void move_cursor_right() {
        cursor = move_point_right(cursor);
        // now update tl_corner to chase it if needed
        if (cursor >= br_corner) {
            visual_scroll_down();
        }
    }

    void move_cursor_down() {
        cursor = move_point_down(cursor);
        if (cursor >= br_corner) {
            visual_scroll_down();
        }
    }

    void move_cursor_up() {
        cursor = move_point_up(cursor);
        if (cursor < tl_corner) {
            visual_scroll_up();
        }
    }

    void move_cursor_to(Point new_curs) {
        // need to chase the new cursor?
        cursor = new_curs;
    }

    void visual_scroll_up() {
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
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
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        if (wrap_status == WrapStatus::WRAP) {
            if (tl_corner.col + num_cols >
                buffer_ptr->buffer.at(tl_corner.row).size()) {
                assert(tl_corner.row + 1 < buffer_ptr->buffer.size());
                tl_corner.col = 0;
                ++tl_corner.row;
            } else {
                tl_corner.col += num_cols;
            }
        }
        // TODO: what happens if nowrap
    }

    void move_to_point(Point const &point) {
        // moves the top left corner to cover that row
        // later we can add scrolling perhaps

        // TODO: can we tell if the point is already on the screen?
        // will prevent awkward jumps

        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        tl_corner.row = point.row;
        tl_corner.col = point.col / num_cols * num_cols;
    }

    void set_anchor() { anchor_cursor = cursor; }
    void unset_anchor() { anchor_cursor = std::nullopt; }

    Point move_point_down(Point const &p) {
        Point to_return = p;
        if (wrap_status == WrapStatus::WRAP) {
            // TODO: add jump to end logic
            auto [num_rows, num_cols] = get_plane_yx_dim();
            size_t curr_line_size = buffer_ptr->buffer.at(to_return.row).size();

            if (curr_line_size == 0) {
                to_return.col = 0;
                to_return.row =
                    std::min(buffer_ptr->buffer.size() - 1, to_return.row + 1);
            } else if ((to_return.col / curr_line_size * curr_line_size) +
                           num_cols <=
                       curr_line_size) {
                to_return.col =
                    std::min(to_return.col + num_cols, curr_line_size);
            } else if (to_return.row + 1 == buffer_ptr->buffer.size()) {
                // special logic for the final row
                to_return.col = buffer_ptr->buffer.at(to_return.row).size();
            } else {
                assert(to_return.row + 1 < buffer_ptr->buffer.size());
                ++to_return.row;
                to_return.col = std::min(
                    to_return.col, buffer_ptr->buffer.at(to_return.row).size());
            }
        } else {
            // non-wrapping movement
            if (to_return.row + 1 < buffer_ptr->buffer.size()) {
                ++to_return.row;
                to_return.col = std::min(
                    to_return.col, buffer_ptr->buffer.at(to_return.row).size());
            }
        }

        return to_return;
    }

    Point move_point_up(Point const &p) {
        Point to_return = p;
        if (wrap_status == WrapStatus::WRAP) {
            auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
            if (to_return.col >= num_cols) {
                to_return.col -= num_cols;
            } else if (to_return.row == 0) {
                // special case of jump to front
                to_return.col = 0;
            } else {
                assert(to_return.row > 0);
                --to_return.row;
                size_t line_size = buffer_ptr->buffer.at(to_return.row).size();
                size_t last_chunk_col = line_size / num_cols * line_size;
                assert(to_return.col < num_cols);
                to_return.col =
                    std::min(to_return.col + last_chunk_col, line_size);
            }
        } else {
            // non-wrapping movement
            to_return.col = std::min(
                to_return.col, buffer_ptr->buffer.at(--to_return.row).size());
        }

        return to_return;
    }

    Point move_point_left(Point const &p) {
        Point to_return = p;
        // update logical cursor
        if (to_return.col > 0) {
            --to_return.col;
        } else if (to_return.row > 0) {
            to_return.col = buffer_ptr->buffer.at(--to_return.row).size();
        }
        return to_return;
    }

    Point move_point_right(Point const &p) {
        Point to_return = p;
        if (to_return.col == buffer_ptr->buffer.at(to_return.row).size() &&
            to_return.row + 1 < buffer_ptr->buffer.size()) {
            // move down one line
            to_return.col = 0;
            ++to_return.row;
        } else if (to_return.col <
                   buffer_ptr->buffer.at(to_return.row).size()) {
            ++to_return.col;
        }
        return to_return;
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

    void move_cursor_left() {
        if (text_plane.anchor_cursor) {
            text_plane.cursor =
                std::min(text_plane.cursor, *text_plane.anchor_cursor);
            text_plane.move_to_point(text_plane.cursor);
            text_plane.unset_anchor();
        } else {
            text_plane.move_cursor_left();
        }
    }
    void move_cursor_right() {
        if (text_plane.anchor_cursor) {
            text_plane.cursor =
                std::max(text_plane.cursor, *text_plane.anchor_cursor);
            text_plane.move_to_point(text_plane.cursor);
            text_plane.unset_anchor();
        } else {
            text_plane.move_cursor_right();
        }
    }
    void move_cursor_down() {
        if (text_plane.anchor_cursor) {
            text_plane.cursor =
                std::max(text_plane.cursor, *text_plane.anchor_cursor);
            text_plane.cursor = text_plane.move_point_down(text_plane.cursor);
            text_plane.move_to_point(text_plane.cursor);
            text_plane.unset_anchor();
        } else {
            text_plane.move_cursor_down();
        }
    }
    void move_cursor_up() {
        if (text_plane.anchor_cursor) {
            text_plane.cursor =
                std::min(text_plane.cursor, *text_plane.anchor_cursor);
            text_plane.cursor = text_plane.move_point_up(text_plane.cursor);
            text_plane.move_to_point(text_plane.cursor);
            text_plane.unset_anchor();
        } else {
            text_plane.move_cursor_up();
        }
    }
    void move_cursor_to(Point new_curs) { text_plane.move_cursor_to(new_curs); }

    void move_selector_left() {
        if (!text_plane.anchor_cursor) {
            text_plane.set_anchor();
        }

        text_plane.move_cursor_left();

        assert(text_plane.anchor_cursor);
        if (*text_plane.anchor_cursor == text_plane.cursor) {
            text_plane.unset_anchor();
        }
    }
    void move_selector_right() {
        if (!text_plane.anchor_cursor) {
            text_plane.set_anchor();
        }

        text_plane.move_cursor_right();

        assert(text_plane.anchor_cursor);
        if (*text_plane.anchor_cursor == text_plane.cursor) {
            text_plane.unset_anchor();
        }
    }
    void move_selector_down() {
        if (!text_plane.anchor_cursor) {
            text_plane.set_anchor();
        }

        text_plane.move_cursor_down();

        assert(text_plane.anchor_cursor);
        if (*text_plane.anchor_cursor == text_plane.cursor) {
            text_plane.unset_anchor();
        }
    }
    void move_selector_up() {
        if (!text_plane.anchor_cursor) {
            text_plane.set_anchor();
        }

        text_plane.move_cursor_up();

        assert(text_plane.anchor_cursor);
        if (*text_plane.anchor_cursor == text_plane.cursor) {
            text_plane.unset_anchor();
        }
    }
    Point get_cursor() const { return text_plane.cursor; }
};
