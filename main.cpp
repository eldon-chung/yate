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
            text_buffer.insert_char_at(view.get_text_cursor(),
                                       (char)nc_input.id);
            view.move_cursor_right();
            return;
        }

        // TODO: handle unicode insertions

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            Point old_pos = view.get_cursor();
            view.move_cursor_left();
            text_buffer.insert_backspace_at(old_pos);
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            text_buffer.insert_newline_at(view.get_text_cursor());
            view.move_cursor_right();
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            text_buffer.insert_delete_at(view.get_text_cursor());
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
        // Arrow key handlers
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

        // Shift arrow key handlers
        input = {.id = NCKEY_UP, .modifiers = NCKEY_MOD_SHIFT};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::SHIFT_UP_ARROW_HANDLER, this)));

        input = {.id = NCKEY_DOWN, .modifiers = NCKEY_MOD_SHIFT};
        keybinds_table.register_handler(
            input, std::function<void()>(std::bind(
                       &ProgramState::SHIFT_DOWN_ARROW_HANDLER, this)));

        input = {.id = NCKEY_RIGHT, .modifiers = NCKEY_MOD_SHIFT};
        keybinds_table.register_handler(
            input, std::function<void()>(std::bind(
                       &ProgramState::SHIFT_RIGHT_ARROW_HANDLER, this)));

        input = {.id = NCKEY_LEFT, .modifiers = NCKEY_MOD_SHIFT};
        keybinds_table.register_handler(
            input, std::function<void()>(std::bind(
                       &ProgramState::SHIFT_LEFT_ARROW_HANDLER, this)));
    }

  private:
    //   Arrow handlers
    void LEFT_ARROW_HANDLER() { view.move_cursor_left(); }
    void RIGHT_ARROW_HANDLER() { view.move_cursor_right(); }
    void UP_ARROW_HANDLER() { view.move_cursor_up(); }
    void DOWN_ARROW_HANDLER() { view.move_cursor_down(); }

    void SHIFT_LEFT_ARROW_HANDLER() { view.move_selector_left(); }
    void SHIFT_RIGHT_ARROW_HANDLER() { view.move_selector_right(); }
    void SHIFT_UP_ARROW_HANDLER() { view.move_selector_up(); }
    void SHIFT_DOWN_ARROW_HANDLER() { view.move_selector_down(); }
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    // set up initial state
    ProgramState program_state;

    program_state.register_initial_keybinds();
    program_state.run_event_loop();
}
