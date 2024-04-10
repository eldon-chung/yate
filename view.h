#pragma once

#include <assert.h>
#include <bits/types/wint_t.h>
#include <cstdio>
#include <deque>
#include <signal.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "text_buffer.h"
#include "util.h"

#define BG_INITIALIZER(br, bg, bb) NCCHANNELS_INITIALIZER(0, 0, 0, br, bg, bb)
#define ncstain_args(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b)                       \
    NCCHANNELS_INITIALIZER(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b),                \
        NCCHANNELS_INITIALIZER(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b),            \
        NCCHANNELS_INITIALIZER(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b),            \
        NCCHANNELS_INITIALIZER(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b)

// simple mapper from a "syntax type index" into a an RGB that we style the
// text with For now we're hardcoding for the cpp syntax
struct Highlighter {
    struct Colour {
        uint8_t r;
        uint8_t g;
        uint8_t b;

        Colour()
            : r(0),
              g(0),
              b(0) {
        }

        Colour(uint8_t r_, uint8_t g_, uint8_t b_)
            : r(r_),
              g(g_),
              b(b_) {
        }
    };

    enum class Style { UNDERLINE, BOLD, ITALICIZE };

    struct Highlight {
        std::optional<Colour> fg_colour;
        std::optional<Colour> bg_colour;
        uint16_t nc_style;

        Highlight()
            : fg_colour(std::nullopt),
              bg_colour(std::nullopt),
              nc_style(NCSTYLE_NONE) {
        }

        Highlight(Colour fgc)
            : fg_colour(fgc),
              bg_colour(std::nullopt),
              nc_style(NCSTYLE_NONE) {
        }

        Highlight(Colour fgc, Colour bgc, uint16_t ncs)
            : fg_colour(fgc),
              bg_colour(bgc),
              nc_style(ncs) {
        }

        bool has_fg_colour() const {
            return fg_colour.has_value();
        }

        bool has_bg_colour() const {
            return bg_colour.has_value();
        }

        bool has_style() const {
            return nc_style != NCSTYLE_NONE;
        }
    };

    std::unordered_map<std::string_view, Highlight> capturing_name_to_colour;

    Highlight operator[]([[maybe_unused]] std::string_view capturing_name) {
        if (auto it = capturing_name_to_colour.find(capturing_name);
            it != capturing_name_to_colour.end()) {
            return it->second;
        }
        return Highlight();
    }

    // Hardcode the cpp values for now
    Highlighter() {
        capturing_name_to_colour["attribute"] = {
            Colour{0x22, 0x3b, 0x7d},
        };
        capturing_name_to_colour["comment"] = {
            Colour{0x79, 0x79, 0x79},
        };
        capturing_name_to_colour["type.builtin"] = {
            Colour{0x22, 0x3b, 0x7d},
        };
        capturing_name_to_colour["constant.builtin.boolean"] = {
            Colour{0x25, 0x47, 0xa9}};
        capturing_name_to_colour["type"] = {Colour{0x4e, 0xc9, 0xb0}};
        capturing_name_to_colour["type.enum.variant"] = {
            Colour{0x4e, 0xc9, 0xb0}};
        capturing_name_to_colour["string"] = {
            Colour{0xae, 0x66, 0x41},
        };
        capturing_name_to_colour["constant.character"] = {
            Colour{0xae, 0x66, 0x41}};
        capturing_name_to_colour["constant.character.escape"] = {
            Colour{0xc3, 0x8a, 0x3c}};
        capturing_name_to_colour["constant.numeric"] = {
            Colour{0xaf, 0xca, 0x9f}};
        capturing_name_to_colour["function"] = {
            Colour{0xdc, 0xdc, 0xaa},
        };
        capturing_name_to_colour["function.special"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["keyword"] = {
            Colour{0xc5, 0x86, 0xc0},
        };
        capturing_name_to_colour["keyword.control"] = {
            Colour{0xa6, 0x79, 0xaf},
        };
        capturing_name_to_colour["keyword.control.conditional"] = {
            Colour{0xa6, 0x79, 0xaf}};
        capturing_name_to_colour["keyword.control.repeat"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["keyword.control.return"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["keyword.control.exception"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["keyword.directive"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["keyword.storage.modifier"] = {
            Colour{0x22, 0x3b, 0x7d}};
        capturing_name_to_colour["keyword.storage.type"] = {
            Colour{0x22, 0x3b, 0x7d}};
        capturing_name_to_colour["namespace"] = {
            Colour{0x4e, 0xc8, 0xaf},
        };
        capturing_name_to_colour["punctuation.bracket"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["variable"] = {
            Colour{0x8e, 0xd3, 0xf9},
        };
        capturing_name_to_colour["variable.builtin"] = {
            Colour{0xc5, 0x86, 0xc0}};
        capturing_name_to_colour["variable.other.member"] = {
            Colour{0x8e, 0xd3, 0xf9}};
        capturing_name_to_colour["variable.parameter"] = {
            Colour{0x8e, 0xd3, 0xf9}};
    }
};

enum class WrapStatus {
    WRAP,
    NOWRAP,
};

struct NCPlane {
    ncplane *ptr;

    NCPlane(ncplane *base, int y, int x, unsigned y_len, unsigned x_len) {
        assert(base);
        ncplane_options opts{.y = y, .x = x, .rows = y_len, .cols = x_len};
        ptr = ncplane_create(base, &opts);
        assert(ptr);
    }

    NCPlane(NCPlane &base, int y, int x, unsigned y_len, unsigned x_len) {
        assert(base.get());
        ncplane_options opts{.y = y, .x = x, .rows = y_len, .cols = x_len};
        ptr = ncplane_create(base.get(), &opts);
        assert(ptr);
    }

    ~NCPlane() {
        if (ptr) {
            ncplane_destroy(ptr);
        }
    }

    NCPlane(NCPlane const &) = delete;
    NCPlane &operator=(NCPlane const &) = delete;
    NCPlane(NCPlane &&other)
        : ptr(std::exchange(other.ptr, nullptr)) {
    }
    NCPlane &operator=(NCPlane &&other) {
        NCPlane temp(std::move(other));
        swap(*this, other);
        return *this;
    }

    friend void swap(NCPlane &a, NCPlane &b) {
        std::swap(a.ptr, b.ptr);
    }

    ncplane *get() {
        return ptr;
    }

    ncplane const *get() const {
        return ptr;
    }

    std::pair<unsigned, unsigned> dims() const {
        unsigned row, cols;
        ncplane_dim_yx(ptr, &row, &cols);
        return {row, cols};
    }

    unsigned height() const {
        return ncplane_dim_y(ptr);
    }

    unsigned width() const {
        return ncplane_dim_x(ptr);
    }
};

class BottomPlaneModel {
    // these are for cmd stuff
    std::string const *prompt_str;
    size_t const *cursor;
    std::string const *cmd_buf;

    // parsed lang

  public:
    BottomPlaneModel() {
    }

    BottomPlaneModel(std::string const *ps, size_t const *c,
                     std::string const *cb)
        : prompt_str(ps),
          cursor(c),
          cmd_buf(cb) {
    }

    std::string_view get_prompt_str() const {
        return *prompt_str;
    }

    size_t get_cursor() const {
        return *cursor;
    }

    std::string_view get_cmd_buf() const {
        return *cmd_buf;
    }
};

class TextPlaneModel {
    TextBuffer const *text_buffer_ptr;
    Cursor const *cursor_ptr;
    std::optional<Cursor> const *anchor_cursor_ptr;
    std::optional<Parser<TextBuffer>> const *maybe_parser;

  public:
    TextPlaneModel()
        : text_buffer_ptr(nullptr),
          cursor_ptr(nullptr),
          anchor_cursor_ptr(nullptr),
          maybe_parser(nullptr) {
    }

    TextPlaneModel(TextBuffer const *tbp, Cursor const *cp,
                   std::optional<Cursor> const *acp,
                   std::optional<Parser<TextBuffer>> const *mp)
        : text_buffer_ptr(tbp),
          cursor_ptr(cp),
          anchor_cursor_ptr(acp),
          maybe_parser(mp) {
    }

    std::vector<std::string_view> get_lines(size_t pos,
                                            size_t num_lines) const {
        return text_buffer_ptr->get_n_lines_at(pos, num_lines);
    }

    bool has_anchor() const {
        return anchor_cursor_ptr->has_value();
    }

    Cursor get_cursor() const {
        return *cursor_ptr;
    }

    Cursor get_anchor() const {
        return **anchor_cursor_ptr;
    }

    std::string_view at(size_t idx) const {
        return text_buffer_ptr->buffer.at(idx);
    }

    size_t num_lines() const {
        return text_buffer_ptr->num_lines();
    }

    bool has_parser() const {
        return maybe_parser->has_value();
    }

    std::vector<Capture> get_captures_within(Point tl, Point br) const {
        assert(maybe_parser->has_value());
        return maybe_parser->value().get_captures_within(tl, br);
    }
};

class TextPlane {
    // TODO: just stick this here for now
    inline static Highlighter highlighter;

    friend class View;

    TextPlaneModel model;

    WrapStatus wrap_status;

    NCPlane text_plane;
    NCPlane cursor_plane;
    NCPlane line_number_plane;
    Point tl_corner;
    Point br_corner; // exclusive range that we also maintain

    // buffer to hold temporarily rendered text
    std::vector<std::pair<Point, Point>> line_points;

  public:
    TextPlane(NCPlane &parent_plane, TextPlaneModel tpm, unsigned int num_rows,
              unsigned int num_cols)
        : model(tpm),
          wrap_status(WrapStatus::WRAP),
          text_plane(parent_plane, 0, 4, num_rows, num_cols - 4),
          cursor_plane(text_plane, 0, 4, 1, 1),
          line_number_plane(text_plane, 0, -4, num_rows, 4),
          tl_corner(Point{0, 0}),
          br_corner(Point{std::string::npos, std::string::npos}) {

        // initially model is uninitialised

        nccell text_plane_base_cell = {.channels = NCCHANNELS_INITIALIZER(
                                           0xff, 0xff, 0xff, 0x2c, 0x2c, 0x2c)};
        ncplane_set_base_cell(text_plane.get(), &text_plane_base_cell);

        // cursor plane opts

        nccell cursor_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff)};
        ncplane_set_base_cell(cursor_plane.get(), &cursor_plane_base_cell);

        nccell line_number_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 66, 135, 245)};

        ncplane_set_base_cell(line_number_plane.get(),
                              &line_number_plane_base_cell);
    }

    ~TextPlane() {
    }
    TextPlane(TextPlane const &) = delete;
    TextPlane &operator=(TextPlane const &) = delete;
    TextPlane(TextPlane &&) = default;
    TextPlane &operator=(TextPlane &&) = default;

    void render() {
        // make this vector a fixed array to avoid allocations?
        render_text();
        render_cursor();
        if (model.has_parser()) {
            render_highlights();
        }
        render_selection();
        render_line_numbers();
    }

    WrapStatus get_wrap_status() const {
        return wrap_status;
    }

    ssize_t num_visual_lines_from_tl(Point const &p) {
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
    void apply_highlight_on_range(
        Point range_start, Point range_end,
        [[maybe_unused]] Highlighter::Highlight highlight) {

        // gives the index into line_points
        auto find_visual_row_containing_point = [&](Point p) -> size_t {
            size_t row_idx = 0;
            while (row_idx < line_points.size()) {
                if (line_points[row_idx].first <= p &&
                    p <= line_points[row_idx].second) {
                    return row_idx;
                }
                ++row_idx;
            }
            return row_idx;
        };

        // need this for base colour things
        nccell base_cell;
        ncplane_base(text_plane.get(), &base_cell);

        auto apply_style = [=, this](size_t y, size_t x, size_t ylen,
                                     size_t xlen,
                                     Highlighter::Highlight hl) -> void {
            // we first obtain the base fg and bg

            unsigned fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
            ncchannels_fg_rgb8(base_cell.channels, &fg_r, &fg_g, &fg_b);
            ncchannels_bg_rgb8(base_cell.channels, &bg_r, &bg_g, &bg_b);

            bool restain = false;
            if (hl.has_fg_colour()) {
                fg_r = hl.fg_colour->r;
                fg_g = hl.fg_colour->g;
                fg_b = hl.fg_colour->b;
                restain = true;
            }

            if (hl.has_bg_colour()) {
                bg_r = hl.bg_colour->r;
                bg_g = hl.bg_colour->g;
                bg_b = hl.bg_colour->b;
                restain = true;
            }

            if (restain) {
                ncplane_stain(text_plane.get(), (int)y, (int)x,
                              (unsigned int)ylen, (unsigned int)xlen,
                              ncstain_args(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b));
            }

            if (hl.has_style()) {
                ncplane_format(text_plane.get(), (int)y, (int)x,
                               (unsigned int)ylen, (unsigned int)xlen,
                               hl.nc_style);
            }
        };

        auto col_to_width = [this](size_t row, size_t col) -> size_t {
            assert(col <= model.at(row).size());
            size_t width = 0;
            for (size_t c = 0; c < col; ++c) {
                width += StringUtils::symbol_into_width(model.at(row)[c]);
            }
            return width;
        };

        // clamp the points if you must
        range_start = std::max(range_start, line_points.front().first);
        range_end =
            std::min(range_end, line_points.back().second); // this is exclusive

        // need to assert that the range intersects the screen
        assert(range_end >= line_points.front().first &&
               range_start < line_points.back().second);

        size_t starting_visual_row =
            find_visual_row_containing_point(range_start);
        size_t ending_visual_row = find_visual_row_containing_point(range_end);

        assert(starting_visual_row <= ending_visual_row);
        if (starting_visual_row == ending_visual_row) {
            size_t starting_col =
                col_to_width(range_start.row, range_start.col);
            size_t ending_col = col_to_width(range_start.row, range_end.col);
            apply_style(starting_visual_row, starting_col, 1,
                        ending_col - starting_col, highlight);
            return;
        }

        {
            size_t starting_col =
                col_to_width(range_start.row, range_start.col);
            size_t ending_col = StringUtils::var_width_str_into_effective_width(
                model.at(range_start.row));
            apply_style(starting_visual_row, starting_col, 1,
                        ending_col - starting_col, highlight);
        }
        {
            size_t ending_col = col_to_width(range_end.row, range_end.col);
            if (ending_col > 0) {
                apply_style(ending_visual_row, 0, 1, ending_col, highlight);
            }
        }
        {
            for (size_t row = starting_visual_row + 1; row < ending_visual_row;
                 ++row) {
                size_t length = StringUtils::var_width_str_into_effective_width(
                    model.at(row));
                apply_style(row, 0, 1, length, highlight);
            }
        }
    }

    void render_highlights() {
        // get highlight list from the model
        std::vector<Capture> captures = model.get_captures_within(
            line_points.front().first, line_points.back().second);

        // TODO: I just want to verify that no point is going to be highlighted
        // twice

        for (auto capture : captures) {
            apply_highlight_on_range(capture.start, capture.end,
                                     highlighter[capture.capture_name]);
        }
    }

    void render_selection() {
        if (!model.has_anchor()) {
            return;
        }
        auto [lp, rp] = std::minmax(model.get_anchor(), model.get_cursor());
        Highlighter::Highlight selection_highlight = Highlighter::Highlight{
            Highlighter::Colour{0, 0, 0}, Highlighter::Colour{0xff, 0xff, 0xff},
            NCSTYLE_UNDERLINE};
        apply_highlight_on_range(lp, rp, selection_highlight);
    }

    void render_line_numbers() {
        // TODO: on the first number, indicate if there's more to that line
        // being wrapped from the previous visual row

        ncplane_erase(line_number_plane.get());
        size_t curr_logical_row = std::string::npos;

        char out_str[5];
        for (size_t visual_row_idx = 0; visual_row_idx < line_points.size();
             ++visual_row_idx) {
            if (curr_logical_row != line_points.at(visual_row_idx).first.row) {
                curr_logical_row = line_points.at(visual_row_idx).first.row;
                snprintf(out_str, 5, "%zu", curr_logical_row + 1);
                ncplane_putnstr_yx(line_number_plane.get(),
                                   (int)(visual_row_idx), 0, 3, out_str);
            }
        }
    }

    void render_cursor() {

        // if our text plane right now doesn't contain the cursor
        // we just hide the cursor and return;
        if (model.get_cursor() > line_points.back().second ||
            model.get_cursor() < line_points.front().first) {
            ncplane_move_below(cursor_plane.get(), text_plane.get());
            return;
        }

        auto [row_count, col_count] = get_plane_yx_dim();
        // need to find where to put the cursor
        Cursor logical_cursor = model.get_cursor();
        std::string_view curr_line = model.at(logical_cursor.row);

        size_t vis_row = line_points.size() - 1;
        for (size_t idx = 0; idx < line_points.size(); ++idx) {
            if (line_points[idx].first <= logical_cursor &&
                logical_cursor <= line_points[idx].second) {
                vis_row = idx;
                break;
            }

            if (logical_cursor < line_points[idx].first) {
                assert(idx > 1);
                vis_row = idx - 1;
                break;
            }
        }

        // now translate this into a vis_col
        size_t vis_col = 0;

        curr_line = curr_line.substr(line_points[vis_row].first.col,
                                     logical_cursor.col -
                                         line_points[vis_row].first.col);
        for (char c : curr_line) {
            vis_col += StringUtils::symbol_into_width(c);
        }

        if (vis_col == col_count) {
            ncplane_move_yx(cursor_plane.get(), (int)vis_row + 1, 0);
        } else {
            ncplane_move_yx(cursor_plane.get(), (int)vis_row, (int)vis_col);
        }

        // else todo the nowrap case
    }

    std::vector<std::pair<Point, Point>> render_text() {
        ncplane_erase(text_plane.get());
        // get the text_plane size
        auto dims = get_plane_yx_dim();
        size_t row_count = dims.first, col_count = dims.second;

        line_points.clear();
        line_points.reserve(row_count);

        size_t num_lines_output = 0;

        size_t curr_logical_row = tl_corner.row;
        size_t curr_logical_col = tl_corner.col;

        char vis_line_buf[col_count + 1];
        // rendered_string_buf.clear();
        std::string_view curr_logical_line;

        auto into_vis_line_buf = [&, col_count]() {
            Point line_start_point = {curr_logical_row, curr_logical_col};

            size_t buf_idx = 0;
            if (curr_logical_col == 0) {
                curr_logical_line = model.at(curr_logical_row);
            }
            while (buf_idx < col_count &&
                   curr_logical_col < curr_logical_line.size()) {
                if (curr_logical_line[curr_logical_col] != '\t') {
                    vis_line_buf[buf_idx++] =
                        curr_logical_line[curr_logical_col++];
                    // rendered_string_buf.back().push_back(
                    // curr_logical_line[curr_logical_col++]);
                    continue;
                }

                if (buf_idx + 4 <= col_count) {
                    for (size_t i = 0; i < 4; ++i) {
                        // rendered_string_buf.back().push_back(' ');
                        vis_line_buf[buf_idx++] = ' ';
                    }
                    ++curr_logical_col;
                } else {
                    break;
                }
            }

            Point line_end_point = {curr_logical_row, curr_logical_col};
            line_points.push_back({line_start_point, line_end_point});

            if (curr_logical_col == curr_logical_line.size()) {
                ++curr_logical_row;
                curr_logical_col = 0;
            }
            vis_line_buf[buf_idx] = '\0';
            return buf_idx;
        };

        while (num_lines_output < row_count &&
               curr_logical_row < model.num_lines()) {
            size_t num_vis_chars = into_vis_line_buf();

            ncplane_putnstr_yx(text_plane.get(), (int)num_lines_output++, 0,
                               num_vis_chars, vis_line_buf);
            // ncplane_putnstr_yx(plane_ptr, (int)num_lines_output++, 0,
            //                    rendered_string_buf.back().size(),
            //                    rendered_string_buf.back().c_str());
        }

        if (num_lines_output == row_count) {
            br_corner = line_points.back().second;
        } else {
            br_corner = Point(std::string::npos, std::string::npos);
        }

        return line_points;
    }

    std::pair<unsigned int, unsigned int> get_yx_dim(ncplane const *ptr) const {
        unsigned int num_rows, num_cols;
        ncplane_dim_yx(ptr, &num_rows, &num_cols);
        return {num_rows, num_cols};
    }

  public:
    std::pair<unsigned int, unsigned int> get_plane_yx_dim() const {
        return get_yx_dim(text_plane.get());
    }

  private:
    void visual_scroll_up() {
        auto [num_rows, num_cols] = get_yx_dim(text_plane.get());
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

    void visual_scroll_down() {
        auto [num_rows, num_cols] = get_yx_dim(text_plane.get());
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

  public:
    void chase_point(Point point) {
        // moves the top left corner to cover that row
        // later we can add scrolling perhaps

        // TODO: can we tell if the point is already on the screen?
        // will prevent awkward jumps

        assert(tl_corner < br_corner);

        auto [num_rows, num_cols] = get_plane_yx_dim();
        ssize_t visual_row_offset = num_visual_lines_from_tl(point);

        if (visual_row_offset >= 0 && visual_row_offset <= num_rows - 1) {
            // still within the screen
            return;
        }

        while (visual_row_offset >= num_rows) {
            visual_scroll_down();
            --visual_row_offset;
        }

        while (visual_row_offset < 0) {
            visual_scroll_up();
            ++visual_row_offset;
        }
    }

    void hide_cursor() {
        ncplane_move_below(text_plane.get(), cursor_plane.get());
    }

    void show_cursor() {
        ncplane_move_above(cursor_plane.get(), text_plane.get());
    }

  private:
};

struct BottomPane {
    NCPlane cmd_plane; // for both cmds and notifs
    NCPlane status_pane;

    // parent is cmd_plane
    NCPlane cmd_cursor_plane;

    bool has_notif = false;

    BottomPlaneModel bpm;

    BottomPane(NCPlane &base_plane, [[maybe_unused]] int y,
               [[maybe_unused]] int x, [[maybe_unused]] unsigned height,
               unsigned width)
        : cmd_plane(base_plane, y, 0, 1, 3 * width / 4),
          status_pane(base_plane, y, 3 * width / 4, 1,
                      width - cmd_plane.width()),
          cmd_cursor_plane(base_plane, 0, 0, 1, 1) {

        nccell cmd_palette_base_cell = {
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 102, 153, 153)};
        ncplane_set_base_cell(cmd_plane.get(), &cmd_palette_base_cell);

        nccell status_base_cell = {
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 102, 153, 255)};
        ncplane_set_base_cell(status_pane.get(), &status_base_cell);

        nccell cursor_base_cell = {
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 255, 255, 255)};
        ncplane_set_base_cell(cmd_cursor_plane.get(), &cursor_base_cell);
    }

    void set_model(BottomPlaneModel to_set) {
        bpm = to_set;
    }

    void render_status(std::string const &sv) {
        ncplane_erase(status_pane.get());
        ncplane_putstr(status_pane.get(), sv.c_str());
    }

    void render_cmd() {
        ncplane_erase(cmd_plane.get());
        // first we putstr the prompt
        if (!bpm.get_prompt_str().empty()) {
            ncplane_putstr_yx(cmd_plane.get(), 0, 0,
                              bpm.get_prompt_str().data());
            // and style it
            ncplane_stain(
                cmd_plane.get(), 0, 0, 1,
                (unsigned int)bpm.get_prompt_str().size(),
                BG_INITIALIZER(105, 105, 105), BG_INITIALIZER(105, 105, 105),
                BG_INITIALIZER(105, 105, 105), BG_INITIALIZER(105, 105, 105));
        }
        if (!bpm.get_cmd_buf().empty()) {
            ncplane_putstr(cmd_plane.get(), bpm.get_cmd_buf().data());
        }

        // TODO: fix this logic in the case of long cmd strings
        ncplane_move_yx(cmd_cursor_plane.get(), 0,
                        (int)(bpm.get_cursor() + bpm.get_prompt_str().size()));
    }

    // overwrite cmd pane
    void notify(std::string_view notif) {
        has_notif = true;
        ncplane_erase(cmd_plane.get());
        if (!notif.empty()) {
            ncplane_putstr_yx(cmd_plane.get(), 0, 0, notif.data());
            // and style it
            ncplane_stain(
                cmd_plane.get(), 0, 0, 1, (unsigned int)notif.size(),
                BG_INITIALIZER(102, 102, 153), BG_INITIALIZER(102, 102, 153),
                BG_INITIALIZER(102, 102, 153), BG_INITIALIZER(102, 102, 153));
        }
    }

    void show_cursor() {
        ncplane_move_above(cmd_cursor_plane.get(), cmd_plane.get());
    }

    void hide_cursor() {
        ncplane_move_below(cmd_plane.get(), cmd_cursor_plane.get());
    }

    unsigned int width() const {
        return ncplane_dim_x(status_pane.get());
    }
};

struct MainPane {
    NCPlane main_plane;
    // i need them to be address stable so i can't just use std::vector
    std::deque<TextPlane> text_planes;
    size_t active_idx;

    MainPane(NCPlane &base_plane, int y, int x, unsigned height, unsigned width)
        : main_plane(base_plane, y, x, height, width),
          active_idx(0) {
    }

    // adds a new text state to the current pane
    TextPlane *add_text_plane(TextPlaneModel tpm) {
        text_planes.emplace_back(main_plane, tpm, main_plane.height(),
                                 main_plane.width());
        active_idx = text_planes.size() - 1;
        return &text_planes.back();
    }

    void hide_cursor() {
        text_planes.at(active_idx).hide_cursor();
    }

    void show_cursor() {
        text_planes.at(active_idx).show_cursor();
    }
};

class View {

  private:
    notcurses *nc_ptr;
    std::vector<TextPlane> text_plane_list;
    size_t active_text_plane_idx;

    // new stuff
    NCPlane ncplane;
    MainPane main_pane;
    BottomPane bottom_pane;

  public:
    View(notcurses *nc, unsigned height, unsigned width)
        : nc_ptr(nc),
          ncplane(notcurses_stdplane(nc), 0, 0, height, width),
          main_pane(ncplane, 0, 0, height - 1, width),
          bottom_pane(ncplane, (int)height - 1, 0, 1, width) {
    }

    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&other) = delete;
    View &operator=(View &&other) = delete;
    ~View() {
    }

    TextPlane *add_text_plane(TextPlaneModel tpm) {
        return main_pane.add_text_plane(tpm);
    }

    void set_prompt_plane(BottomPlaneModel bpm) {
        bottom_pane.set_model(bpm);
    }

    void render_cmd() {
        bottom_pane.render_cmd();
    }

    void notify(std::string_view notif) {
        bottom_pane.notify(notif);
    }

    notcurses *get_nc_ptr() const {
        return nc_ptr;
    }

    ncinput get_keypress() {
        struct ncinput nc_input;
        notcurses_get(nc_ptr, nullptr, &nc_input);
        return nc_input;
    }

    // Helper methods
    void focus_cmd() {
        main_pane.hide_cursor();
        bottom_pane.show_cursor();
    }

    void focus_text() {
        main_pane.show_cursor();
        bottom_pane.hide_cursor();
    }

    std::vector<std::pair<Point, Point>> const &get_active_line_points() {
        return text_plane_list.at(active_text_plane_idx).line_points;
    }

    void refresh_screen() {
        notcurses_render(nc_ptr);
    }

    BottomPane *get_bottom_pane_ptr() {
        return &bottom_pane;
    }
};
