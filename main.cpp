#include <signal.h>

#include <assert.h>

#include <iostream>
#include <optional>

#include <notcurses/notcurses.h>

#include "File.h"
#include "KeyBinds.h"
#include "view.h"

// Program state
struct ProgramState {

    File file;
    KeyBinds keybinds_table;

    // The main states we maintain
    TextBuffer text_buffer;
    std::vector<std::string> clipboard;

    // the cmd buff should be here
    std::string cmd_buf;
    std::string prompt_buf;

    View view;

    ProgramState(std::optional<std::string_view> maybe_filename)
        : file(), text_buffer(), cmd_buf(""), prompt_buf(""),
          view(
              std::move(View::init_view(&text_buffer, &cmd_buf, &prompt_buf))) {
        // deal with the file nonsense now that everything's set up
        // so we can have error messaging etc
        if (!maybe_filename) {
            return;
        }

        assert(maybe_filename);
        file = File(*maybe_filename);
        if (file.has_errmsg()) {
            // show the error message in the status
            // for now we print:
            std::cerr << file.get_errmsg() << std::endl;
        } else {
            assert(file.is_open());
            text_buffer.load_contents(file.get_file_contents());
        }
    }

    TextBuffer const &get_buffer() const {
        return text_buffer;
    }

    // Cmd Mode >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    std::optional<std::string> prompt(std::string_view prompt_str) {
        // setup the view changes
        cmd_buf.clear();
        view.focus_cmd();

        // prep the prompt buffer
        prompt_buf = prompt_str;
        if (!cmd_event_loop()) {
            view.render_status();
            view.focus_text();
            return std::nullopt;
        }

        view.clear_cmd();
        view.focus_text();
        return cmd_buf;
    }

    bool cmd_event_loop() {
        view.render_cmd();
        while (true) {
            struct ncinput input = view.get_keypress();

            // Ctrl C to quit prompt mode
            if (input.id == 'C' && ncinput_ctrl_p(&input)) {
                // clear the prompt and buf, and reset the cursor
                view.reset_cmd_cursor();
                cmd_buf.clear();
                prompt_buf.clear();
                return false; // return empty string
            }

            // returns true when quitting
            if (handle_keypress_cmd_mode(input)) {
                return true;
            }

            // should we just make a prompt_render
            // and a text_render? and make prompt_buf a local?
            // TODO: save that for next refactor
            view.render_cmd();
        }
    }

    bool handle_keypress_cmd_mode(ncinput nc_input) {
        // we're going to manually handle some cases to save on lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {

            cmd_buf.insert(view.get_cmd_cursor(), 1, (char)nc_input.id);
            view.move_cmd_cursor_right();
            // should cursors be part of view or state?
            return false;
        }

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            if (view.get_cmd_cursor() >= 1) {
                cmd_buf.erase(view.get_cmd_cursor() - 1);
                view.move_cmd_cursor_left();
            }
            return false;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            view.move_cmd_cursor_to(0);
            return true;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            if (view.get_cmd_cursor() < cmd_buf.size()) {
                cmd_buf.erase(view.get_cmd_cursor());
            }
            return false;
        }

        // arrow keys here
        switch (nc_input.id) {
        case NCKEY_LEFT:
            view.move_cmd_cursor_left();
            break;
        case NCKEY_RIGHT:
            view.move_cmd_cursor_right();
            break;
        default:
            break;
        }

        return false;
    }

    // Text Mode >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    // the main logic for handling keypresses is done here
    void handle_keypress_text_mode(ncinput nc_input) {
        // eventually this needs to work potentially
        // move than text_buffer
        // so it has to apply a function

        // we're going to manually handle some cases to save on lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {

            // hmm so is ProgramState the driver?
            text_buffer.insert_char_at(view.get_text_cursor(),
                                       (char)nc_input.id);
            view.move_text_cursor_right();
            return;
        }

        // TODO: handle unicode insertions

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            Point old_pos = view.get_text_cursor();
            view.move_text_cursor_left();
            text_buffer.insert_backspace_at(old_pos);
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            text_buffer.insert_newline_at(view.get_text_cursor());
            view.move_text_cursor_right();
            return;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            text_buffer.insert_delete_at(view.get_text_cursor());
            return;
        }
        keybinds_table[nc_input]();
    }

    void run_event_loop() {
        view.render_text();
        view.render_status();

        while (true) {
            struct ncinput input = view.get_keypress();
            // pressing key in text mode clears notifs
            // should it?
            view.render_status();

            // Ctrl W to return for now
            if (input.id == 'W' && ncinput_ctrl_p(&input)) {
                return;
            }

            handle_keypress_text_mode(input);
            view.render_text();
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

        // Clipboard manipulators
        input = {.id = 'C', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_C_HANDLER, this)));

        input = {.id = 'X', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_X_HANDLER, this)));

        input = {.id = 'N', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_V_HANDLER, this)));

        // cmd palette
        input = {.id = 'P', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_P_HANDLER, this)));

        // basic functionality
        input = {.id = 'R', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_R_HANDLER, this)));

        input = {.id = 'O', .modifiers = NCKEY_MOD_CTRL};
        keybinds_table.register_handler(
            input, std::function<void()>(
                       std::bind(&ProgramState::CTRL_O_HANDLER, this)));
    }

  private:
    // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  START HANDLERS
    //   Arrow handlers
    void LEFT_ARROW_HANDLER() {
        view.move_text_cursor_left();
    }
    void RIGHT_ARROW_HANDLER() {

        view.move_text_cursor_right();
    }
    void UP_ARROW_HANDLER() {

        view.move_text_cursor_up();
    }
    void DOWN_ARROW_HANDLER() {

        view.move_text_cursor_down();
    }

    void SHIFT_LEFT_ARROW_HANDLER() {

        view.move_selector_left();
    }
    void SHIFT_RIGHT_ARROW_HANDLER() {

        view.move_selector_right();
    }
    void SHIFT_UP_ARROW_HANDLER() {

        view.move_selector_up();
    }
    void SHIFT_DOWN_ARROW_HANDLER() {
        view.move_selector_down();
    }

    // Clipboard manip
    void CTRL_C_HANDLER() {
        if (view.in_selection_mode()) {
            auto [lp, rp] = view.get_selection_points();
            clipboard = text_buffer.get_lines(lp, rp);
        } else {
            // for now do nothing
        }
    }

    void CTRL_X_HANDLER() {
        if (view.in_selection_mode()) {
            auto [lp, rp] = view.get_selection_points();
            clipboard = text_buffer.get_lines(lp, rp);
            text_buffer.remove_selection_at(lp, rp);
            view.move_cursor_to(lp);
        } else {
            // for now do nothing
        }

        // quit selection mode
    }

    void CTRL_V_HANDLER() {
        if (clipboard.empty()) {
            return;
        }

        if (view.in_selection_mode()) {
            auto [lp, rp] = view.get_selection_points();
            text_buffer.remove_selection_at(lp, rp);
            view.move_cursor_to(lp);
        }
        Point insertion_point = view.get_text_cursor();
        Point final_point =
            text_buffer.insert_text_at(insertion_point, clipboard);
        view.move_cursor_to(final_point);
    }

    // Command Palette
    void CTRL_P_HANDLER() {
        std::optional<std::string> cmd = prompt(">");
        // then other stuff here later
        if (!cmd) {
            std::cerr << "user quit prompting" << std::endl;
        } else {
            std::cerr << "executing: [" << *cmd << "]" << std::endl;
        }
    }

    // Basic functionality

    // Nano's keybind for saving files
    void CTRL_O_HANDLER() {
        if (!file.has_filename()) {
            // at this point we need to prompt the user for a filename:
            // TODO: a prompt UI element? can be merged with cmd UI element

            std::optional<std::string> response =
                prompt("Enter filename to save to:");
            if (!response.has_value()) {
                return;
            }

            bool created;
            std::tie(file, created) = File::create_if_not_exists(*response);
            if (!created) {
                // warn user about overwriting
                response = prompt("Overwrite file contents? [y/N]");

                if (response.value_or("n") != "y") {
                    return; // don't save
                }
            }
        }

        if (!file.is_open()) {
            // tell the user what the error is and clear off the file?
            view.notify(file.get_errmsg());
            file = File(); // clear off the file
            return;
        }

        // attempt to write to file
        if (file.write(text_buffer.get_view())) {
            view.notify("File saved!");
        } else {
            // tell the dude we failed to save the file
            view.notify(file.get_errmsg());
        }
    }

    // Nano's keybind for reading files
    void CTRL_R_HANDLER() {

        // TODO: ask about saving current contents
        // if we havent saved it

        // After that's done:
        std::optional<std::string> response = prompt("Enter filename to open:");
        if (!response) {
            return; // do nothing and return
        }

        assert(response.has_value());
        file = File(*response);
        if (!file.is_open()) {
            view.notify(file.get_errmsg());
        } else {
            text_buffer.load_contents(file.get_file_contents());
            view.render_text();
            if (file.in_readonly_mode()) {
                view.notify("File opened in read-only mode.");
            } else {
                view.notify("File opened.");
            }
        }
    }

    // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  END HANDLERS
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    std::optional<std::string_view> maybe_filename;
    if (argc > 1) {
        // for now we assuming second argv is filename
        maybe_filename = argv[1];
    }

    // set up initial state
    ProgramState program_state(maybe_filename);
    program_state.register_initial_keybinds();
    program_state.run_event_loop();
    return 0;
}
