#pragma once

#include <assert.h>
#include <signal.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "text_buffer.h"
#include "util.h"

#define BG_INITIALIZER(br, bg, bb) NCCHANNELS_INITIALIZER(0, 0, 0, br, bg, bb)

enum class WrapStatus {
    WRAP,
    NOWRAP,
};

class TextPlaneModel {
    TextBuffer const *text_buffer_ptr;
    Point const *cursor_ptr;
    std::optional<Point> const *anchor_cursor_ptr;

  public:
    TextPlaneModel(TextBuffer const *tb_ptr, Point const *c_ptr,
                   std::optional<Point> const *ap_ptr)
        : text_buffer_ptr(tb_ptr),
          cursor_ptr(c_ptr),
          anchor_cursor_ptr(ap_ptr) {
    }

    TextPlaneModel(TextPlaneModel const &) = default;
    TextPlaneModel(TextPlaneModel &&) = default;
    TextPlaneModel &operator=(TextPlaneModel const &) = default;
    TextPlaneModel &operator=(TextPlaneModel &&) = default;

    std::vector<std::string_view> get_lines(size_t pos,
                                            size_t num_lines) const {
        return text_buffer_ptr->get_n_lines_at(pos, num_lines);
    }

    bool has_anchor() const {
        return anchor_cursor_ptr->has_value();
    }

    Point get_cursor() const {
        return *cursor_ptr;
    }

    Point get_anchor() const {
        return **anchor_cursor_ptr;
    }

    std::string_view at(size_t idx) const {
        return text_buffer_ptr->buffer.at(idx);
    }

    size_t num_lines() const {
        return text_buffer_ptr->num_lines();
    }
};

class TextPlane {
    friend class View;

    TextPlaneModel model;

    ncplane *line_number_plane_ptr;
    ncplane *plane_ptr;
    ncplane *cursor_plane_ptr;
    Point tl_corner;
    Point br_corner; // exclusive range that we also maintain

  public:
    TextPlane(notcurses *nc_ptr, TextPlaneModel tbm, unsigned int num_rows,
              unsigned int num_cols)
        : model(tbm),
          tl_corner({0, 0}),
          br_corner({std::string::npos, std::string::npos}) {

        static const int num_digits = 4;

        // create the text_plane
        ncplane_options text_plane_opts = {.y = 0,
                                           .x = num_digits,
                                           .rows = num_rows,
                                           .cols = num_cols - num_digits,
                                           .name = "textplane"};
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
            .y = 0, .x = -num_digits, .rows = num_rows, .cols = num_digits};

        line_number_plane_ptr =
            ncplane_create(plane_ptr, &line_number_plane_opts);

        nccell line_number_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 66, 135, 245)};

        ncplane_set_base_cell(line_number_plane_ptr,
                              &line_number_plane_base_cell);
    }

    ~TextPlane() {

        if (!line_number_plane_ptr) {
            ncplane_destroy(line_number_plane_ptr); // let's try one
        }

        if (!cursor_plane_ptr) {
            ncplane_destroy(cursor_plane_ptr); // let's try one
        }

        if (!plane_ptr) {
            ncplane_destroy(plane_ptr); // let's try one
        }
    }

    TextPlane(TextPlane const &) = delete;
    TextPlane(TextPlane &&other)
        : model(other.model),
          line_number_plane_ptr(
              std::exchange(other.line_number_plane_ptr, nullptr)),
          plane_ptr(std::exchange(other.plane_ptr, nullptr)),
          cursor_plane_ptr(std::exchange(other.cursor_plane_ptr, nullptr)),
          tl_corner(other.tl_corner),
          br_corner(other.br_corner) {
    }
    TextPlane &operator=(TextPlane const &) = delete;
    TextPlane &operator=(TextPlane &&other) {
        TextPlane temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }

    friend void swap(TextPlane &a, TextPlane &b) {
        using std::swap;
        swap(a.model, b.model);
        swap(a.line_number_plane_ptr, b.line_number_plane_ptr);
        swap(a.plane_ptr, b.plane_ptr);
        swap(a.cursor_plane_ptr, b.cursor_plane_ptr);
        swap(a.tl_corner, b.tl_corner);
        swap(a.br_corner, b.br_corner);
    }

    void render(WrapStatus wrap_status) {
        // make this vector a fixed array to avoid allocations?
        auto row_idxs = render_text();
        render_cursor(row_idxs, wrap_status);
        render_selection(row_idxs);
        render_line_numbers(row_idxs);
    }

    ssize_t num_visual_lines_from_tl(Point const &p, WrapStatus wrap_status) {
        auto [row_count, col_count] = get_plane_yx_dim();

        if (wrap_status == WrapStatus::NOWRAP) {
            return (ssize_t)p.row - (ssize_t)tl_corner.row;
        }

        Point aligned_point = p;
        aligned_point.col = aligned_point.col / col_count *
                            col_count; // round to the nearest chunk

        if (aligned_point == tl_corner) {
            return 0;
        }

        if (aligned_point.row == tl_corner.row) {
            return ((ssize_t)aligned_point.col - (ssize_t)tl_corner.col) /
                   (ssize_t)col_count;
        }

        ssize_t num_visual_lines = 0;
        auto [start, end] = std::minmax(p.row, tl_corner.row);
        for (size_t idx = start + 1; idx < end; ++idx) {
            num_visual_lines +=
                std::max(model.at(idx).size() / col_count, (size_t)1);
        }

        if (p > tl_corner) {
            size_t tl_row_len = model.at(tl_corner.row).size();
            num_visual_lines += (tl_row_len - tl_corner.col) / col_count + 1;
            num_visual_lines += (aligned_point.col) / col_count;
        } else {
            assert(p < tl_corner);
            size_t ap_row_len = model.at(aligned_point.row).size();
            num_visual_lines +=
                (ap_row_len - aligned_point.col) / col_count + 1;
            num_visual_lines += (tl_corner.col) / col_count;

            num_visual_lines *= -1;
        }

        return num_visual_lines;
    }

  private:
    void render_selection(std::vector<size_t> const &row_idxs) {
        if (!model.has_anchor()) {
            return;
        }

        auto [row_count, col_count] = get_plane_yx_dim();

        size_t num_lines_output = 0;
        size_t curr_logical_row = tl_corner.row;
        size_t curr_logical_col = tl_corner.col;
        size_t row_indxs_idx = 1; // sigh naming

        auto [lp, rp] = std::minmax(model.get_cursor(), model.get_anchor());

        while (num_lines_output < row_count &&
               curr_logical_row < model.num_lines()) {

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
                        NCCHANNELS_INITIALIZER(0, 0, 0, 128, 128, 128),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 128, 128, 128),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 128, 128, 128),
                        NCCHANNELS_INITIALIZER(0, 0, 0, 128, 128, 128));
                    ncplane_format(plane_ptr, (int)num_lines_output,
                                   (int)visual_col_l, 1,
                                   (unsigned int)(visual_col_r - visual_col_l),
                                   NCSTYLE_UNDERLINE);
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

    void render_cursor(std::vector<size_t> const &row_idxs,
                       WrapStatus wrap_status) {
        // get the text_plane size
        auto [row_count, col_count] = get_plane_yx_dim();
        assert(model.get_cursor().row >= tl_corner.row);
        assert(model.get_cursor().row < tl_corner.row + row_count);

        if (wrap_status == WrapStatus::WRAP) {
            if (model.at(model.get_cursor().row).empty()) {
                ncplane_move_yx(
                    cursor_plane_ptr,
                    (int)row_idxs.at(model.get_cursor().row - tl_corner.row),
                    0);
            } else {
                // TODO: not sure this is entirely correct
                size_t starting_row =
                    row_idxs.at(model.get_cursor().row - tl_corner.row);
                starting_row +=
                    (model.get_cursor().col - tl_corner.col) / col_count;

                int target_col =
                    (model.at(model.get_cursor().row).empty())
                        ? 0
                        : (int)(model.get_cursor().col % col_count);
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
               curr_logical_row < model.num_lines()) {

            if (curr_logical_col == 0 && num_lines_output > 0) {
                line_rows.push_back(num_lines_output);
            }

            std::string_view curr_line = model.at(curr_logical_row);
            assert(tl_corner.col <= curr_line.size());

            int num_written = ncplane_putnstr_yx(
                plane_ptr, (int)num_lines_output, 0, col_count,
                curr_line.data() + curr_logical_col);

            if (num_written < 0) {
                // handle error cases?
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
            // by definition the br corner of the screen
            // is now not reachable by the document
            br_corner.row = std::string::npos;
            br_corner.col = std::string::npos;
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

    void visual_scroll_up(WrapStatus wrap_status) {
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        if (wrap_status == WrapStatus::WRAP) {
            if (tl_corner.col == 0) {
                assert(tl_corner.row > 0);
                // move tl_corner up one row and get the last line
                --tl_corner.row;
                auto prev_line = model.at(tl_corner.row);

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

    void visual_scroll_down(WrapStatus wrap_status) {
        auto [num_rows, num_cols] = get_yx_dim(plane_ptr);
        if (wrap_status == WrapStatus::WRAP) {
            if (tl_corner.col + num_cols > model.at(tl_corner.row).size()) {
                assert(tl_corner.row + 1 < model.num_lines());
                tl_corner.col = 0;
                ++tl_corner.row;
            } else {
                tl_corner.col += num_cols;
            }
        }

        // TODO: what happens if nowrap
    }

    void chase_point(Point point, WrapStatus wrap_status) {
        // moves the top left corner to cover that row
        // later we can add scrolling perhaps

        // TODO: can we tell if the point is already on the screen?
        // will prevent awkward jumps

        assert(tl_corner < br_corner);

        auto [num_rows, num_cols] = get_plane_yx_dim();
        ssize_t visual_row_offset =
            num_visual_lines_from_tl(point, wrap_status);

        if (visual_row_offset >= 0 && visual_row_offset <= num_rows - 1) {
            // still within the screen
            return;
        }

        while (visual_row_offset >= num_rows) {
            visual_scroll_down(wrap_status);
            --visual_row_offset;
        }

        while (visual_row_offset < 0) {
            visual_scroll_up(wrap_status);
            ++visual_row_offset;
        }
    }

    void hide_cursor() {
        ncplane_move_below(cursor_plane_ptr, plane_ptr);
    }
    void show_cursor() {
        ncplane_move_above(cursor_plane_ptr, plane_ptr);
    }
};

struct CommandPalettePlane {
    // 1024 has to be enough right!?
    ncplane *plane_ptr;
    ncplane *cursor_ptr;

    CommandPalettePlane(ncplane *base_ptr, int y_, int x_,
                        unsigned int num_cols) {

        ncplane_options ncopts = {
            .y = y_, .x = x_, .rows = 1, .cols = num_cols};
        plane_ptr = ncplane_create(base_ptr, &ncopts);

        nccell cmd_palette_base_cell = {
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 102, 153, 153)};

        ncplane_set_base_cell(plane_ptr, &cmd_palette_base_cell);

        ncopts = {.y = 0, .x = 0, .rows = 1, .cols = 1};
        cursor_ptr = ncplane_create(plane_ptr, &ncopts);

        nccell cursor_base_cell = {
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 255, 255, 255)};

        ncplane_set_base_cell(cursor_ptr, &cursor_base_cell);

        hide_cursor();
    }
    ~CommandPalettePlane() {
        if (!cursor_ptr) {
            ncplane_destroy(cursor_ptr);
        }

        if (!plane_ptr) {
            ncplane_destroy(plane_ptr);
        }
    }

    CommandPalettePlane(CommandPalettePlane const &) = delete;
    CommandPalettePlane &operator=(CommandPalettePlane const &) = delete;
    CommandPalettePlane(CommandPalettePlane &&other)
        : plane_ptr(std::exchange(other.plane_ptr, nullptr)),
          cursor_ptr(std::exchange(other.cursor_ptr, nullptr)) {
    }

    CommandPalettePlane &operator=(CommandPalettePlane &&other) {
        CommandPalettePlane temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }

    friend void swap(CommandPalettePlane &a, CommandPalettePlane &b) {
        using std::swap;
        swap(a.plane_ptr, b.plane_ptr);
        swap(a.cursor_ptr, b.cursor_ptr);
    }

    void render(std::string_view prompt_str, std::string_view cmd_str,
                size_t cursor) {
        render_cmd(prompt_str, cmd_str);
        render_cursor(prompt_str, cursor);
    }

    void clear() {
        hide_cursor();
        ncplane_erase(plane_ptr);
        // ncplane_putnstr(plane_ptr, std::string::npos, " ");
    }

    void notify(std::string_view notif) {
        ncplane_erase(plane_ptr);
        if (!notif.empty()) {
            ncplane_putstr_yx(plane_ptr, 0, 0, notif.data());
            // and style it
            ncplane_stain(
                plane_ptr, 0, 0, 1, (unsigned int)notif.size(),
                BG_INITIALIZER(102, 102, 153), BG_INITIALIZER(102, 102, 153),
                BG_INITIALIZER(102, 102, 153), BG_INITIALIZER(102, 102, 153));
        }
    }

    void render_cmd(std::string_view prompt_str, std::string_view cmd_str) {
        ncplane_erase(plane_ptr);
        // first we putstr the prompt
        if (!prompt_str.empty()) {
            ncplane_putstr_yx(plane_ptr, 0, 0, prompt_str.data());
            // and style it
            ncplane_stain(
                plane_ptr, 0, 0, 1, (unsigned int)prompt_str.size(),
                BG_INITIALIZER(255, 255, 255), BG_INITIALIZER(255, 255, 255),
                BG_INITIALIZER(255, 255, 255), BG_INITIALIZER(255, 255, 255));
        }
        if (!cmd_str.empty()) {
            ncplane_putstr(plane_ptr, cmd_str.data());
        }
    }

    void render_cursor(std::string_view prompt_str, size_t cursor) {
        ncplane_move_yx(cursor_ptr, 0, (int)(cursor + prompt_str.size()));
    }
    void show_cursor() {
        ncplane_move_above(cursor_ptr, plane_ptr);
    }
    void hide_cursor() {
        ncplane_move_below(cursor_ptr, plane_ptr);
    }

    std::pair<unsigned, unsigned> get_plane_yx_dim() const {
        unsigned y, x;
        ncplane_dim_yx(plane_ptr, &y, &x);
        return {y, x};
    }
};

class View {

  private:
    notcurses *nc_ptr;
    TextPlane text_plane;
    CommandPalettePlane cmd_plane;

    // eventually move this out into
    // its own UI element
    size_t starting_row;

    WrapStatus wrap_status;

    View(notcurses *nc, TextPlane t_plane, CommandPalettePlane c_plane)
        : nc_ptr(nc),
          text_plane(std::move(t_plane)),
          cmd_plane(std::move(c_plane)),
          starting_row(0),
          wrap_status(WrapStatus::WRAP) {
    }

  public:
    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&other)
        : nc_ptr(std::exchange(other.nc_ptr, nullptr)),
          text_plane(std::move(other.text_plane)),
          cmd_plane(std::move(other.cmd_plane)),
          wrap_status(other.wrap_status) {
    }

    View &operator=(View &&other) {
        View temp{std::move(other)};
        swap(*this, temp);
        return *this;
    }
    ~View() {
        notcurses_stop(nc_ptr);
    }

    friend void swap(View &a, View &b) {
        using std::swap;
        swap(a.nc_ptr, b.nc_ptr);
        swap(a.text_plane, b.text_plane);
        swap(a.cmd_plane, b.cmd_plane);
        swap(a.cmd_plane, b.cmd_plane);
        swap(a.wrap_status, b.wrap_status);
    }

    size_t get_starting_row() const {
        return starting_row;
    }

    WrapStatus get_wrap_status() const {
        return wrap_status;
    }

    static View &init_view(TextPlaneModel tpm) {
        // notcurses init
        static struct notcurses_options nc_options = {
            // .loglevel = NCLOGLEVEL_INFO,
            .flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR,
        };
        notcurses *nc_ptr = notcurses_init(&nc_options, nullptr);
        // disable the conversion into the signal
        notcurses_linesigs_disable(nc_ptr);

        // get the base ptr
        ncplane *std_plane_ptr = notcurses_stdplane(nc_ptr);

        unsigned int num_rows, num_cols;
        ncplane_dim_yx(std_plane_ptr, &num_rows, &num_cols);
        // now create the UI elements
        static View view(
            nc_ptr, TextPlane(nc_ptr, tpm, num_rows - 1, num_cols),
            CommandPalettePlane(std_plane_ptr, (int)num_rows - 1, 0, num_cols));
        return view;
    }

    void render_text() {
        text_plane.render(wrap_status);
        notcurses_render(nc_ptr);
    }

    void render_cmd(std::string_view prompt_str, std::string_view cmd_str,
                    size_t cursor) {
        cmd_plane.render(prompt_str, cmd_str, cursor);
        notcurses_render(nc_ptr);
    }

    void render_status() {
        clear_cmd();
        notcurses_render(nc_ptr);
    }

    void notify(std::string_view notif) {
        cmd_plane.hide_cursor();
        cmd_plane.notify(notif);
        notcurses_render(nc_ptr);
    }

    notcurses *get_nc_ptr() const {
        return nc_ptr;
    }

    ncinput get_keypress() {
        struct ncinput nc_input;
        notcurses_get(nc_ptr, nullptr, &nc_input);
        return nc_input;
    }

    void chase_point(Point p) {
        text_plane.chase_point(p, wrap_status);
    }

    std::pair<unsigned int, unsigned int> get_text_plane_dim() const {
        unsigned y, x;
        ncplane_dim_yx(text_plane.plane_ptr, &y, &x);
        return {y, x};
    }

    // Helper methods

    void clear_cmd() {
        cmd_plane.clear();
    }

    void focus_cmd() {
        text_plane.hide_cursor();
        cmd_plane.show_cursor();
    }

    void focus_text() {
        cmd_plane.hide_cursor();
        text_plane.show_cursor();
    }
};
