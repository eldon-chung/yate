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

    ProgramState() : view(std::move(View::init_view(&text_buffer))) {}
    TextBuffer const &get_buffer() const { return text_buffer; }

    // the main logic for handling keypresses is done here
    void handle_keypress(ncinput nc_input) {
        // eventually this needs to work potentially
        // move than text_buffer
        // so it has to apply a function

        // we're going to manually handled some cases to save on lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {
            text_buffer.insert_char((char)nc_input.id);
            return;
        }

        // TODO: handle unicode insertions

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
        view.render();

        while (true) {
            struct ncinput input = view.get_keypress();
            handle_keypress(input);
            view.render();
        }
    }

    void register_initial_keybinds() {
        ncinput input = {.id = NCKEY_LEFT, .modifiers = 0};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::LEFT_ARROW_HANDLER, this)));

        input = {.id = NCKEY_RIGHT, .modifiers = 0};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::RIGHT_ARROW_HANDLER, this)));

        input = {.id = NCKEY_DOWN, .modifiers = 0};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::DOWN_ARROW_HANDLER, this)));

        input = {.id = NCKEY_UP, .modifiers = 0};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::UP_ARROW_HANDLER, this)));
    }

  private:
    // list of handlers?
    void LEFT_ARROW_HANDLER() {
        // modify text buffer stuff
        if (text_buffer.cursor.col > 0) {
            --text_buffer.cursor.col;
        } else if (text_buffer.cursor.line > 0) {
            text_buffer.cursor.col =
                text_buffer.buffer.at(--text_buffer.cursor.line).size();
        }

        // modify view stuff
    }

    void RIGHT_ARROW_HANDLER() {
        assert(!text_buffer.buffer.empty());
        if (text_buffer.cursor.col ==
                text_buffer.buffer.at(text_buffer.cursor.line).size() &&
            text_buffer.cursor.line + 1 < text_buffer.buffer.size()) {
            // move down one line
            text_buffer.cursor.col = 0;
            ++text_buffer.cursor.line;
        } else if (text_buffer.cursor.col <
                   text_buffer.buffer.at(text_buffer.cursor.line).size()) {
            ++text_buffer.cursor.col;
        }
    }

    void UP_ARROW_HANDLER() {
        if (text_buffer.cursor.line == 0) {
            text_buffer.cursor.col = 0;
        } else {
            text_buffer.cursor.col = std::min(
                text_buffer.cursor.col,
                text_buffer.buffer.at(--text_buffer.cursor.line).size());
        }
    }

    void DOWN_ARROW_HANDLER() {
        if (text_buffer.cursor.line + 1 < text_buffer.buffer.size()) {
            ++text_buffer.cursor.line;
            text_buffer.cursor.col =
                std::min(text_buffer.cursor.col,
                         text_buffer.buffer.at(text_buffer.cursor.line).size());
        } else if (text_buffer.cursor.line + 1 == text_buffer.buffer.size()) {
            text_buffer.cursor.col =
                text_buffer.buffer.at(text_buffer.cursor.line).size();
        }
    }
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    // set up initial state
    ProgramState program_state;

    program_state.register_initial_keybinds();
    program_state.run_event_loop();
}
