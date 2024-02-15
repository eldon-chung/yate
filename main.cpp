#include <assert.h>

#include <iostream>
#include <optional>

#include <notcurses/notcurses.h>

#include "view.h"

// Program state
struct ProgramState {
    TextBuffer text_buffer;
    View view;

    ProgramState() : view(std::move(View::init_view())) {}
    TextBuffer const &get_buffer() const { return text_buffer; }

    void handle_keypress(ncinput nc_input) {
        // handle arrow keys
        if (nc_input.id == NCKEY_LEFT || nc_input.id == NCKEY_RIGHT ||
            nc_input.id == NCKEY_DOWN || nc_input.id == NCKEY_UP) {
            text_buffer.move_cursor(nc_input.id - preterunicode(2));
            return;
        }

        // some chars to insert first
        // insert newline
        if (nc_input.id == NCKEY_ENTER) {
            text_buffer.insert_newline();
            return;
        }

        // backspace
        if (nc_input.id == NCKEY_BACKSPACE) {
            text_buffer.insert_backspace();
            return;
        }

        // delete
        if (nc_input.id == NCKEY_DEL) {
            text_buffer.insert_delete();
            return;
        }

        // handle unmodified inputs
        // and need to handle delete separately
        if (nc_input.id == NCKEY_TAB ||
            (nc_input.id <= 255 && nc_input.id >= 32) ||
            nc_input.id != NCKEY_DEL) {
            text_buffer.insert_char((char)nc_input.id);
            return;
        }
    }

    void run_event_loop() {
        view.render(text_buffer);

        while (true) {
            struct ncinput input = view.get_keypress();
            handle_keypress(input);
            view.render(text_buffer);
        }
    }
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    // set up initial state
    ProgramState program_state;
    program_state.run_event_loop();
}
