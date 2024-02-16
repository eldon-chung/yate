#include <assert.h>

#include <iostream>
#include <optional>

#include <notcurses/notcurses.h>

#include "KeyBinds.h"
#include "view.h"

// Program state
struct ProgramState {

    KeyBinds keybinds_table;

    // The main states we maintain
    TextBuffer text_buffer;
    View view;

    ProgramState() : view(std::move(View::init_view())) {}
    TextBuffer const &get_buffer() const { return text_buffer; }

    // the main logic for handling keypresses is done here
    void handle_keypress(ncinput nc_input) {
        // eventually this needs to work potentially
        // move than text_buffer
        // so it has to apply a function

        if (nc_input.id == NCKEY_RESIZE) {
            exit(0);
        }

        std::cerr << "modifier: " << nc_input.modifiers << std::endl;
        std::cerr << "id: " << nc_input.id << std::endl;

        // we're going to manually handled some cases to save on lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {
            text_buffer.insert_char((char)nc_input.id);
            return;
        }

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            text_buffer.insert_backspace();
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            text_buffer.insert_newline();
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            text_buffer.insert_delete();
            return;
        }

        keybinds_table[nc_input]();
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
