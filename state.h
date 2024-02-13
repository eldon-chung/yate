#pragma once

#include <notcurses/notcurses.h>

#include "text_buffer.h"

struct State {
    TextBuffer text_buffer;

    TextBuffer const &get_buffer() const { return text_buffer; }

    void handle_keypress(ncinput nc_input) {

        // some chars to insert first
        // insert newline
        if (nc_input.id == 127) {
            text_buffer.insert_newline();
        }

        // backspace
        if (nc_input.id == 8) {
            text_buffer.insert_backspace();
        }

        // backspace
        if (nc_input.id == 127) {
            text_buffer.insert_backspace();
        }

        // handle unmodified inputs
        // and need to handle delete separately
        if (nc_input.id == 9 || (nc_input.id <= 255 && nc_input.id >= 32) ||
            nc_input.id != 127) {
            text_buffer.insert_char((char)nc_input.id);
        }
    }
};
