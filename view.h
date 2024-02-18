#pragma once

#include <assert.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include <utility>

#include "text_buffer.h"

class TextPlane {
    // should it just get a ptr to text buffer?
    TextBuffer const *buffer_ptr;
    ncplane *plane_ptr;
    ncplane *cursor_plane_ptr;
    size_t starting_row;

  public:
    TextPlane(TextBuffer const *text_buffer_ptr, ncplane *text_plane_ptr)
        : buffer_ptr(text_buffer_ptr), plane_ptr(text_plane_ptr),
          starting_row(0) {
        ncplane_options cursor_plane_opts = {
            .y = 0, .x = 0, .rows = 1, .cols = 1};
        cursor_plane_ptr = ncplane_create(plane_ptr, &cursor_plane_opts);

        nccell cursor_plane_base_cell{
            .channels = NCCHANNELS_INITIALIZER(0, 0, 0, 0xff, 0xff, 0xff)};
        ncplane_set_base_cell(cursor_plane_ptr, &cursor_plane_base_cell);
    }
    ~TextPlane() { ncplane_destroy(plane_ptr); }

    void render() {
        render_text();
        render_cursor();
    }

  private:
    void render_cursor() {
        TextBuffer::Cursor text_cursor = buffer_ptr->get_cursor();

        // get the text_plane size
        auto [row_count, col_count] = get_yx_dim();

        // assert the cursor has to be within the view range
        assert(text_cursor.line >= starting_row &&
               text_cursor.line < buffer_ptr->num_lines());
        assert(text_cursor.col < col_count);

        ncplane_move_yx(cursor_plane_ptr,
                        (int)(text_cursor.line - starting_row),
                        (int)text_cursor.col);
    }

    void render_text() {

        // use this as a line breaker for line wraps
        auto break_into_visual_lines =
            [](std::string_view logical_line,
               std::vector<std::string_view> &visual_lines, size_t size_limit,
               size_t line_size) {
                // you should only call this if we've not hit the size_limit
                assert(size_limit > visual_lines.size());
                size_t remaining_needed = size_limit - visual_lines.size();

                while (!logical_line.empty() && remaining_needed-- > 0) {
                    visual_lines.push_back(logical_line.substr(0, line_size));
                    logical_line = logical_line.substr(
                        std::min(line_size, logical_line.size()));
                }
            };

        ncplane_erase(plane_ptr);

        // get the text_plane size
        auto [row_count, col_count] = get_yx_dim();

        // get that many logical rows from the textbuffer
        std::vector<std::string_view> logical_lines =
            buffer_ptr->get_n_lines(starting_row, row_count);

        // break up the lines into visual lines
        // for now we assume line wrapping is a thing
        std::vector<std::string_view> visual_lines;
        visual_lines.reserve(logical_lines.size());

        for (size_t logical_idx = 0; logical_idx < logical_lines.size() &&
                                     visual_lines.size() < row_count;
             ++logical_idx) {

            break_into_visual_lines(logical_lines[logical_idx], visual_lines,
                                    row_count, col_count);
        }

        // pad the remaining lines
        for (size_t remaining_idx = visual_lines.size();
             remaining_idx < col_count; ++remaining_idx) {
            visual_lines.push_back("");
        }

        // place the text on the screen (i forgot but there's probably auto
        // wrapping, and the more) important case is none wrap
        for (size_t plane_row_idx = 0; plane_row_idx < row_count;
             ++plane_row_idx) {
            ncplane_putnstr_yx(plane_ptr, (int)plane_row_idx, 0,
                               visual_lines[plane_row_idx].size(),
                               visual_lines[plane_row_idx].data());
        }
    }

    std::pair<unsigned int, unsigned int> get_yx_dim() const {
        unsigned int num_rows, num_cols;
        ncplane_dim_yx(plane_ptr, &num_rows, &num_cols);
        return {num_rows, num_cols};
    }
};

class View {
    notcurses *nc_ptr;
    TextPlane text_plane;

    // eventually move this out into
    // its own UI element
    size_t starting_row;

    View(notcurses *nc, ncplane *text_plane_ptr, TextBuffer const *state)
        : nc_ptr(nc), text_plane(state, text_plane_ptr), starting_row(0) {}

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

        // create the text_plane
        ncplane_options text_plane_opts = {.rows = num_rows - 1,
                                           .cols = num_cols};
        ncplane *text_plane_ptr =
            ncplane_create(std_plane_ptr, &text_plane_opts);

        nccell text_plane_base_cell = {.channels = NCCHANNELS_INITIALIZER(
                                           0xff, 0xff, 0xff, 169, 169, 169)};
        ncplane_set_base_cell(text_plane_ptr, &text_plane_base_cell);

        // now create the UI elements
        static View view(nc_ptr, text_plane_ptr, text_buffer_ptr);
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
};
