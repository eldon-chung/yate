#pragma once

#include <algorithm>
#include <cctype>
#include <notcurses/nckeys.h>
#include <signal.h>

#include <assert.h>

#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <notcurses/notcurses.h>
#include <tree_sitter/api.h>

#include "EventQueue.h"
#include "File.h"
#include "Program.h"
#include "text_buffer.h"
#include "util.h"
#include "view.h"

// Program state

class ProgramState;
struct StateReturn {
    bool event_handled;
    enum class Transition {
        ENTER,
        REMAIN,
        EXIT,
    };
    Transition transition_type;
    ProgramState *next_state_ptr;

    using enum Transition;

    StateReturn()
        : event_handled(true),
          transition_type(REMAIN),
          next_state_ptr(nullptr) {
    }

    StateReturn(Transition tt)
        : transition_type(tt) {
        assert(tt != ENTER);
    }

    StateReturn(ProgramState *ns)
        : transition_type(ENTER),
          next_state_ptr(ns) {
    }

    StateReturn(bool eh)
        : event_handled(eh) {
    }
};

inline StateReturn null_func() {
    return StateReturn();
}

#define REGISTER_KEY(key_id, func)                                             \
    keybinds_table.register_handler(                                           \
        ncinput{.id = key_id, .modifiers = 0},                                 \
        std::function<StateReturn()>(std::bind(func, this)));

#define REGISTER_MODDED_KEY(key_id, key_mod, func)                             \
    keybinds_table.register_handler(                                           \
        ncinput{.id = key_id, .modifiers = key_mod},                           \
        std::function<StateReturn()>(std::bind(func, this)));

struct KeyBinds {
    // TODO: For now these are only used to handle ncinput events
    std::array<std::function<StateReturn()>, 1024> handler_list;

    KeyBinds() {
        // set everything as null
        for (size_t idx = 0; idx < 1024; ++idx) {
            handler_list[idx] = null_func;
        }
    }

    static size_t modifier_shift(unsigned int modifiers) {
        return 128 *
               ((modifiers & NCKEY_MOD_SHIFT) + (modifiers & NCKEY_MOD_ALT) +
                (modifiers & NCKEY_MOD_CTRL));
    }

    static size_t get_hash(ncinput input) {
        size_t index = input.id;
        if (index >= preterunicode(1) && index <= preterunicode(12)) {
            // maps 1, 12 to 0, 11
            index -= preterunicode(1);
        } else if (index >= preterunicode(21) && index <= preterunicode(30)) {
            // maps 21, 30 to 12, 21
            index -= preterunicode(21);
            index += 12;
        } else if (index == preterunicode(121)) {
            // maps 121 to 13
            index -= preterunicode(121);
            index += 13;
        }

        // translate based on modifier
        index += modifier_shift(input.modifiers);
        return index;
    }

    std::function<StateReturn()> operator[](ncinput input) {
        return handler_list[get_hash(input)];
    }

    // returns false if handler is not registered, happens if attempting to
    // overwrite a a handler that isnt null_func
    bool register_handler(ncinput input, std::function<StateReturn()> handler) {
        auto ptr = handler_list[get_hash(input)].target<StateReturn (*)()>();
        if (ptr && *ptr != null_func) {
            return false;
        }
        handler_list[get_hash(input)] = std::move(handler);
        return true;
    }
};

struct Program;
class ProgramState;
class TextState;
class PromptState;

class ProgramState {

  protected:
    inline static View *view_ptr;
    inline static EventQueue *event_queue_ptr;
    // View *view_ptr;
    KeyBinds keybinds_table;

    // TODO: add a callback for cleanup after prompts?
    // e.g. refocus view onto text_plane
    // better yet make view figure out based on the updates?

  public:
    //   Note: Do this before anything else
    static void set_view_ptr(View *vp_) {
        view_ptr = vp_;
    }

    static void set_eq_ptr(EventQueue *eq_) {
        event_queue_ptr = eq_;
    }

    ProgramState() {
    }

    virtual ~ProgramState() {
    }

    [[nodiscard]] virtual StateReturn handle_event(Event e) final {
        if (e.is_input()) {
            return handle_input(e.get_input());
        } else {
            return handle_msg(e.get_msg());
        }
    }

    [[nodiscard]] virtual StateReturn handle_msg(std::string_view msg) = 0;
    [[nodiscard]] virtual StateReturn handle_input(ncinput nc_input) = 0;

    virtual void enter() = 0;
    virtual void exit() = 0;
    virtual void register_keybinds() = 0;
    virtual void trigger_render() = 0;

    static std::shared_ptr<TextState>
    get_first_text_state(std::optional<std::string_view> maybe_filename) {
        return std::make_shared<TextState>(maybe_filename);
    }

    friend std::ostream &operator<<(std::ostream &os, ProgramState const &ps);

  protected:
    virtual void print(std::ostream &os) const = 0;
};

inline std::ostream &operator<<(std::ostream &os, ProgramState const &ps) {
    ps.print(os);
    return os;
}

class PromptState : public ProgramState {
    // stuff for prompting

    // place the response when we quit here
    bool has_response;
    std::string target_str;
    std::string prompt_str;
    size_t cursor;
    std::string cmd_buf;

  public:
    PromptState()
        : ProgramState() {
    }

    StateReturn handle_msg([[maybe_unused]] std::string_view msg) {
        // nothing to do rn
        return StateReturn();
    }

    void print(std::ostream &os) const {
        os << "{PromptState}";
    }

    void enter() {
        has_response = false;
        cmd_buf.clear();
        cursor = 0;
    }

    void exit() {
        prompt_str.clear();
        // remove the ptr
        std::string to_send = target_str;
        to_send += ":";
        if (has_response) {
            to_send += "str=";
            to_send += cmd_buf;
        } else {
            to_send += "null";
        }

        cmd_buf.clear();
        prompt_str.clear();
        cursor = 0;
        has_response = false;
        view_ptr->render_cmd();
        event_queue_ptr->post_message(to_send);
    }

    void register_keybinds() {
        // nothing here right now
    }

    void setup(std::string_view ps, std::string_view target) {
        prompt_str = ps;
        target_str = target;
    }

    BottomPlaneModel get_prompt_plane_model() {
        return BottomPlaneModel{&prompt_str, &cursor, &cmd_buf};
    }

    StateReturn handle_input(ncinput nc_input) {
        auto move_cursor_left = [&]() {
            if (cursor > 0) {
                --cursor;
            }
        };

        auto move_cursor_right = [&]() {
            if (cursor < cmd_buf.size()) {
                ++cursor;
            }
        };

        view_ptr->focus_cmd();

        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {

            cmd_buf.insert(cursor, 1, (char)nc_input.id);
            move_cursor_right();
            // should cursors be part of view or state?
            return StateReturn();
        }

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            if (cursor >= 1) {
                cmd_buf.erase(cursor - 1);
                move_cursor_left();
            }
            return StateReturn();
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            if (cursor < cmd_buf.size()) {
                cmd_buf.erase(cursor);
            }
            return StateReturn();
        }

        // arrow keys here
        switch (nc_input.id) {
        case NCKEY_LEFT:
            move_cursor_left();
            return StateReturn();
        case NCKEY_RIGHT:
            move_cursor_right();
            return StateReturn();
        default:
            break;
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            // TODO: Caller needs to refocus at this point?
            // return the response to the prompt
            has_response = true;
            return StateReturn(StateReturn::Transition::EXIT);
        }

        if (nc_input.modifiers == NCKEY_MOD_CTRL && nc_input.id == 'Q') {
            // we'll have to reset the response
            has_response = false;
            return StateReturn(StateReturn::Transition::EXIT);
        }

        return StateReturn();
    }

    void trigger_render() {
        view_ptr->focus_cmd();
        view_ptr->render_cmd();
    }
};

static inline PromptState prompt_state;

class FileSaverState : public ProgramState {

    File *file_ptr;                // corresponds to the textstate
    TextBuffer const *text_buffer; // corresponds to the textstate

    enum class SubState {
        EXISTING_FILE,
        CLOSED_FILE,
        HAS_FILENAME,
        ASK_OVERWRITE,
        QUIT,
        FAIL,
        SUCCESS
    };
    SubState substate;

    // for potential prompts
    std::optional<std::string> maybe_target_for_response;

  public:
    FileSaverState(File *fp, TextBuffer const *tbp)
        : file_ptr(fp),
          text_buffer(tbp),
          substate(SubState::CLOSED_FILE),
          maybe_target_for_response(std::nullopt) {
    }

    FileSaverState(File *fp, TextBuffer const *tbp, std::string_view target)
        : file_ptr(fp),
          text_buffer(tbp),
          maybe_target_for_response(std::string(target)) {
    }

    void print(std::ostream &os) const {
        os << "{FileSaverState }";
    }

    StateReturn handle_msg(std::string_view msg) {

        // we resume our computations here?
        if (!msg.starts_with("FileSaverState:")) {
            return StateReturn(false);
        }
        msg = msg.substr(15);

        switch (substate) {
            using enum SubState;
        case EXISTING_FILE: {
            // just write to the file and call it a day
            return write_to_file();
        };
        case CLOSED_FILE:
            if (!file_ptr->has_filename()) {
                // ask for filename
                substate = HAS_FILENAME;
                prompt_state.setup("Enter filename to save to:",
                                   "FileSaverState");
                return StateReturn(&prompt_state);
            }
        case HAS_FILENAME:
            if (!file_ptr->has_filename() && msg.starts_with("str=") &&
                msg.size() > 4) {
                // TODO: filename validation
                file_ptr->set_filename(msg.substr(4));
            } else if (msg == "null" || msg.size() == 4) {
                // then we should just quit
                substate = QUIT;
                return StateReturn(StateReturn::Transition::EXIT);
            }
            assert(file_ptr->has_filename());
            // returns true if a file was created in the process
            substate = ASK_OVERWRITE;
            if (!file_ptr->try_open_or_create()) {
                // file was not just created
                prompt_state.setup("File exists, overwrite? [Y/n]:",
                                   "FileSaverState");
                return StateReturn(&prompt_state);
            } else {
                // file was just created, in which case
                // just set the answer to yes to save
                msg = "str=Y";
            }
        case ASK_OVERWRITE:
            if (msg == "str=Y" || msg == "str=y" || msg == "str=") {
                return write_to_file();
            } else {
                substate = QUIT;
            }
            return StateReturn(StateReturn::Transition::EXIT);
        default:
            assert(false);
        };
        // this should now be unreachable
        return StateReturn(false);
    }

    StateReturn handle_input([[maybe_unused]] ncinput nc_input) {
        // never handle inputs; always bubble them up
        return StateReturn(false);
    }

    void setup(std::string_view tfr) {
        maybe_target_for_response = tfr;
    }

    void enter() {
        using enum SubState;
        if (file_ptr->is_open()) {
            substate = EXISTING_FILE;
        } else {
            substate = CLOSED_FILE;
        }
        // just a message to start things off
        event_queue_ptr->post_message("FileSaverState", "");
    }

    void exit() {
        using enum SubState;
        assert(substate == QUIT || substate == FAIL || substate == SUCCESS);
        if (maybe_target_for_response) {
            // switch on states here
            switch (substate) {
            case QUIT:
                event_queue_ptr->post_message(maybe_target_for_response.value(),
                                              "QUIT");
                break;
            case FAIL:
                event_queue_ptr->post_message(maybe_target_for_response.value(),
                                              "FAIL");
                break;
            case SUCCESS:
                event_queue_ptr->post_message(maybe_target_for_response.value(),
                                              "SUCCESS");
                break;
            default:
                assert(false);
            }
        }
        maybe_target_for_response = std::nullopt;
    }

    void register_keybinds() {
        // nothing to register
    }

    void trigger_render() {
        // nothing to render?
    }

  private:
    // Tries to open the file
    // if the file already open we move on
    StateReturn write_to_file() {
        using enum SubState;

        assert(file_ptr->is_open());
        if (file_ptr->get_mode() != File::Mode::READWRITE) {
            substate = FAIL;
        } else if (!file_ptr->write(text_buffer->get_view())) {
            substate = FAIL;
        } else {
            substate = SUCCESS;
        }

        return StateReturn(StateReturn::Transition::EXIT);
    }
};

class FileOpenerState : public ProgramState {

    File *file_ptr;
    TextBuffer *text_buffer_ptr;
    Cursor *text_buffer_cursor_ptr;
    std::optional<std::string> maybe_filename_to_open;

    enum class SubState {
        NO_FILENAME,
        HAS_FILENAME,
        HAS_UNSAVED,
        ASK_TO_SAVE,
        OPENING,
        QUIT,
        FAIL,
        SUCCESS
    };
    SubState substate;

  public:
    FileOpenerState(File *fp, TextBuffer *tbp, Cursor *tbcp)
        : ProgramState(),
          file_ptr(fp),
          text_buffer_ptr(tbp),
          text_buffer_cursor_ptr(tbcp),
          maybe_filename_to_open(std::nullopt) {
    }

    ~FileOpenerState() {
    }

    void print(std::ostream &os) const {
        os << "{FileOpenerState }";
    }

    StateReturn handle_msg(std::string_view msg) {

        // we resume our computations here?
        if (!msg.starts_with("FileOpenerState:")) {
            return StateReturn(false);
        }

        msg = msg.substr(16);
        switch (substate) {
            using enum SubState;
        case NO_FILENAME:
            // prompt the user for a name
            prompt_state.setup("Enter file name to open:", "FileOpenerState");
            substate = HAS_FILENAME;
            return StateReturn(&prompt_state);
        case HAS_FILENAME:
            if (!(msg.starts_with("str=") && msg.size() > 4)) {
                return StateReturn(StateReturn::Transition::EXIT);
            }
            substate = ASK_TO_SAVE;
            maybe_filename_to_open = std::string(msg.substr(4));
            // TODO: when undo stack is empty, we
            // skip the next state
        case ASK_TO_SAVE:
            prompt_state.setup("Do you want to save current contents? [Y/n]:",
                               "FileOpenerState");

            substate = HAS_UNSAVED;
            return StateReturn(&prompt_state);
        case HAS_UNSAVED:
            if (msg == "null") {
                // then the user cancelled, in which case
                // we bail
                return StateReturn(StateReturn::Transition::EXIT);
            }
            substate = OPENING;
            if (msg != "str=N" && msg != "str=n") {
                return StateReturn(new FileSaverState(file_ptr, text_buffer_ptr,
                                                      "FileOpenerState"));
            }
        case OPENING: {
            assert(maybe_filename_to_open.has_value());
            File attempt = File(maybe_filename_to_open.value());
            if (attempt.get_mode() == File::Mode::SCRATCH) {
                view_ptr->notify("File doesn't exist");
                return StateReturn(StateReturn::Transition::EXIT);
            }

            if (attempt.get_mode() == File::Mode::UNREADABLE) {
                view_ptr->notify("File can't be read");
                return StateReturn(StateReturn::Transition::EXIT);
            }

            auto maybe_file_contents = attempt.get_file_contents();
            if (!maybe_file_contents) {
                view_ptr->notify("Could not load file contents.");
            } else {
                text_buffer_ptr->load_contents(maybe_file_contents.value());
                // for now we just reset this at {0, 0}
                *text_buffer_cursor_ptr = Cursor();
            }
            return StateReturn(StateReturn::Transition::EXIT);
        }
        default:
            assert(false);
        }
        return StateReturn(false);
    }

    StateReturn handle_input([[maybe_unused]] ncinput nc_input) {
        return StateReturn(false);
    }

    void enter() {
        substate = SubState::NO_FILENAME;
        // send a message to kick things off
        event_queue_ptr->post_message("FileOpenerState", "");
    }

    void exit() {
        // nothing to clean up
    }
    void register_keybinds() {
        // nothing to register
    }
    void trigger_render() {
        // nothing to render
    }
};

class TextState : public ProgramState {
    File file;
    TextBuffer text_buffer;
    Cursor text_cursor;
    std::optional<Cursor> maybe_anchor_point;
    std::vector<std::string> clipboard;
    TextPlane
        *text_plane_ptr; // how do i retrigger a reparse without giving an fd?
    BottomPane *bottom_pane_ptr;

    // objects used for parsing
    std::optional<Parser<TextBuffer>> maybe_parser;

  public:
    TextState(std::optional<std::string_view> maybe_filename)
        : ProgramState(),
          file() {
        register_keybinds();
        // TODO: for now, we keep the keybinds per context separate
        if (maybe_filename.has_value()) {
            file = File(maybe_filename.value());
        }

        if (auto fc = file.get_file_contents(); fc.has_value()) {
            text_buffer.load_contents(*fc);
        }

        if (file.has_errmsg()) {
            view_ptr->notify(file.get_errmsg());
        }

        text_plane_ptr = view_ptr->add_text_plane(this->get_text_plane_model());
        bottom_pane_ptr = view_ptr->get_bottom_pane_ptr();
    }
    ~TextState() {
    }

    void enter() {
    }

    void exit() {
        // eventually we can prompt user to save here perhaps
    }

    void print(std::ostream &os) const {
        os << "{TextState }";
    }

    void set_parse_lang(Parser<TextBuffer>::LANG parser_language) {
        if (!maybe_parser) {
            maybe_parser = Parser<TextBuffer>{&text_buffer, read_text_buffer};
        }

        maybe_parser->set_language(parser_language);
        // need to trigger first time parse
        maybe_parser->parse_buffer();
    }

    TextPlaneModel get_text_plane_model() {
        return TextPlaneModel{&text_buffer, &text_cursor, &maybe_anchor_point,
                              &maybe_parser};
    }

    StateReturn handle_msg([[maybe_unused]] std::string_view msg) {
        // for now ignore it
        return StateReturn();
    }

    void trigger_render() {
        view_ptr->focus_text();
        text_plane_ptr->render();

        std::string status_str =
            "Line " + std::to_string(text_cursor.row) + ", Column " +
            std::to_string(text_cursor.effective_col) + " ";

        // TODO what happens if it's not wide enough?
        // can we at least assume 80 cols? that should be enough
        std::string_view lang_name = "Text Mode";
        if (maybe_parser) {
            lang_name = maybe_parser->get_parser_lang_name();
        }

        size_t remaining_pad_length =
            bottom_pane_ptr->width() - status_str.size() - lang_name.size();
        status_str.resize(status_str.size() + remaining_pad_length, ' ');
        status_str += lang_name;

        bottom_pane_ptr->render_status(status_str);
    }

    StateReturn handle_input(ncinput nc_input) {
        // we're going to manually handle some cases to save on
        // lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {

            Cursor update_start_point = text_cursor;
            size_t start_byte =
                text_buffer.get_offset_from_point(update_start_point);

            Cursor update_old_end_point = text_cursor;
            size_t old_end_byte =
                text_buffer.get_offset_from_point(update_old_end_point);

            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);

                update_start_point = lp;
                start_byte =
                    text_buffer.get_offset_from_point(update_start_point);

                update_old_end_point = rp;
                old_end_byte =
                    text_buffer.get_offset_from_point(update_old_end_point);

                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            }
            text_buffer.insert_char_at(text_cursor, (char)nc_input.id);
            RIGHT_ARROW_HANDLER();

            Cursor update_new_end_point = text_cursor;
            size_t new_end_byte =
                text_buffer.get_offset_from_point(update_new_end_point);

            // TODO: only do this if it's valid
            reparse_text(update_start_point, update_old_end_point,
                         update_new_end_point, start_byte, old_end_byte,
                         new_end_byte);

            text_plane_ptr->chase_point(text_cursor);
            return StateReturn();
        }

        // TODO: handle unicode insertions

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            return BACKSPACE_HANDLER();
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {

            Cursor update_start_point = text_cursor;
            Cursor update_old_end_point = text_cursor;

            size_t start_byte =
                text_buffer.get_offset_from_point(update_start_point);
            size_t old_end_byte =
                text_buffer.get_offset_from_point(update_old_end_point);

            Cursor update_new_end_point;
            size_t new_end_byte;

            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);

                update_start_point = lp;
                update_old_end_point = rp;

                start_byte =
                    text_buffer.get_offset_from_point(update_start_point);
                old_end_byte =
                    text_buffer.get_offset_from_point(update_old_end_point);

                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            }
            text_buffer.insert_newline_at(text_cursor);
            RIGHT_ARROW_HANDLER();
            update_new_end_point = text_cursor;
            new_end_byte =
                text_buffer.get_offset_from_point(update_new_end_point);

            reparse_text(update_start_point, update_old_end_point,
                         update_new_end_point, start_byte, old_end_byte,
                         new_end_byte);

            text_plane_ptr->chase_point(text_cursor);
            return StateReturn();
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            return DELETE_HANDLER();
        }

        // need to change the keybinds_table type
        return keybinds_table[nc_input]();
    }

    void register_keybinds() {
        // Arrow key handlers

        REGISTER_KEY(NCKEY_LEFT, &TextState::LEFT_ARROW_HANDLER);
        REGISTER_KEY(NCKEY_RIGHT, &TextState::RIGHT_ARROW_HANDLER);
        REGISTER_KEY(NCKEY_DOWN, &TextState::DOWN_ARROW_HANDLER);
        REGISTER_KEY(NCKEY_UP, &TextState::UP_ARROW_HANDLER);

        // Shift Arrow
        REGISTER_MODDED_KEY(NCKEY_LEFT, NCKEY_MOD_SHIFT,
                            &TextState::SHIFT_LEFT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_RIGHT, NCKEY_MOD_SHIFT,
                            &TextState::SHIFT_RIGHT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_DOWN, NCKEY_MOD_SHIFT,
                            &TextState::SHIFT_DOWN_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_UP, NCKEY_MOD_SHIFT,
                            &TextState::SHIFT_UP_ARROW_HANDLER);

        // Ctrl Left/Right
        REGISTER_MODDED_KEY(NCKEY_LEFT, NCKEY_MOD_CTRL,
                            &TextState::CTRL_LEFT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_RIGHT, NCKEY_MOD_CTRL,
                            &TextState::CTRL_RIGHT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_LEFT, NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT,
                            &TextState::CTRL_SHIFT_LEFT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_RIGHT, NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT,
                            &TextState::CTRL_SHIFT_RIGHT_ARROW_HANDLER);

        // Alt Up/down
        REGISTER_MODDED_KEY(NCKEY_UP, NCKEY_MOD_ALT,
                            &TextState::ALT_UP_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_DOWN, NCKEY_MOD_ALT,
                            &TextState::ALT_DOWN_HANDLER);

        // Ctrl Del/Backspace
        REGISTER_MODDED_KEY(NCKEY_BACKSPACE, NCKEY_MOD_CTRL,
                            &TextState::CTRL_BACKSPACE_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_DEL, NCKEY_MOD_CTRL,
                            &TextState::CTRL_DELETE_HANDLER);

        // Ctrl P
        REGISTER_MODDED_KEY('P', NCKEY_MOD_CTRL, &TextState::CTRL_P_HANDLER);

        // File manipulators
        REGISTER_MODDED_KEY('O', NCKEY_MOD_CTRL, &TextState::CTRL_O_HANDLER);
        REGISTER_MODDED_KEY('R', NCKEY_MOD_CTRL, &TextState::CTRL_R_HANDLER);

        // Text Manipulators
        REGISTER_MODDED_KEY('G', NCKEY_MOD_CTRL, &TextState::CTRL_V_HANLDER);
        REGISTER_MODDED_KEY('X', NCKEY_MOD_CTRL, &TextState::CTRL_X_HANDLER);
        REGISTER_MODDED_KEY('C', NCKEY_MOD_CTRL, &TextState::CTRL_C_HANDLER);
    }

  private:
    //   All the handlers for Text Editing
    StateReturn LEFT_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::min(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        } else {
            text_cursor = move_cursor_left(text_cursor);
        }
        text_plane_ptr->chase_point(text_cursor);

        return StateReturn();
    }
    StateReturn RIGHT_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::max(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        } else {
            text_cursor = move_cursor_right(text_cursor);
        }
        text_plane_ptr->chase_point(text_cursor);

        return StateReturn();
    }
    StateReturn UP_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::min(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        }
        text_cursor = move_cursor_up(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        return StateReturn();
    }
    StateReturn DOWN_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::max(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        }
        text_cursor = move_cursor_down(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        return StateReturn();
    }

    StateReturn SHIFT_LEFT_ARROW_HANDLER() {
        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_left(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }
        return StateReturn();
    }
    StateReturn SHIFT_RIGHT_ARROW_HANDLER() {
        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_right(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }
        return StateReturn();
    }
    StateReturn SHIFT_UP_ARROW_HANDLER() {
        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_up(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }
        return StateReturn();
    }
    StateReturn SHIFT_DOWN_ARROW_HANDLER() {
        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_down(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }
        return StateReturn();
    }

    // Word Boundary movement
    StateReturn CTRL_LEFT_ARROW_HANDLER() {

        maybe_anchor_point.reset();
        text_cursor = move_cursor_left_over_boundary(text_cursor);

        return StateReturn();
    }

    StateReturn CTRL_RIGHT_ARROW_HANDLER() {
        maybe_anchor_point.reset();
        text_cursor = move_cursor_right_over_boundary(text_cursor);

        return StateReturn();
    }

    StateReturn BACKSPACE_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);

            Cursor update_start_point = lp;
            Cursor update_old_end_point = rp;
            size_t start_byte = text_buffer.get_offset_from_point(lp);
            size_t old_end_byte = text_buffer.get_offset_from_point(rp);

            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;

            Cursor update_new_end_point = text_cursor;
            size_t new_end_byte =
                text_buffer.get_offset_from_point(update_new_end_point);

            maybe_anchor_point.reset();

            reparse_text(update_start_point, update_old_end_point,
                         update_new_end_point, start_byte, old_end_byte,
                         new_end_byte);
        } else {
            Cursor old_pos = text_cursor;

            Cursor update_old_end_point = text_cursor;
            size_t old_end_byte =
                text_buffer.get_offset_from_point(update_old_end_point);

            LEFT_ARROW_HANDLER();
            Cursor update_start_point = text_cursor;
            Cursor update_new_end_point = text_cursor;

            size_t start_byte =
                text_buffer.get_offset_from_point(update_start_point);
            size_t new_end_byte =
                text_buffer.get_offset_from_point(update_new_end_point);

            text_buffer.insert_backspace_at(old_pos);

            reparse_text(update_start_point, update_old_end_point,
                         update_new_end_point, start_byte, old_end_byte,
                         new_end_byte);
        }
        text_plane_ptr->chase_point(text_cursor);
        return StateReturn();
    }

    StateReturn DELETE_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);

            Cursor update_start_point = lp;
            Cursor update_old_end_point = rp;

            size_t start_byte =
                text_buffer.get_offset_from_point(update_start_point);
            size_t old_end_byte =
                text_buffer.get_offset_from_point(update_old_end_point);

            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            maybe_anchor_point.reset();

            Cursor update_new_end_point = lp;
            size_t new_end_byte =
                text_buffer.get_offset_from_point(update_new_end_point);

            reparse_text(update_start_point, update_old_end_point,
                         update_new_end_point, start_byte, old_end_byte,
                         new_end_byte);
        } else {
            Cursor update_start_point = text_cursor;
            Cursor update_old_end_point = move_cursor_right(update_start_point);

            size_t start_byte =
                text_buffer.get_offset_from_point(update_start_point);

            size_t old_end_byte =
                text_buffer.get_offset_from_point(update_old_end_point);

            text_buffer.insert_delete_at(text_cursor);

            reparse_text(update_start_point, update_old_end_point,
                         update_start_point, start_byte, old_end_byte,
                         start_byte);
        }

        text_plane_ptr->chase_point(text_cursor);
        return StateReturn();
    }

    StateReturn CTRL_DELETE_HANDLER() {
        // this needs to be handled like a delete
        if (maybe_anchor_point) {
            return DELETE_HANDLER();
        }

        maybe_anchor_point = move_cursor_right_over_boundary(text_cursor);
        return DELETE_HANDLER();
    }

    StateReturn CTRL_BACKSPACE_HANDLER() {
        if (maybe_anchor_point) {
            return BACKSPACE_HANDLER();
        }

        maybe_anchor_point = move_cursor_left_over_boundary(text_cursor);
        return BACKSPACE_HANDLER();
    }

    StateReturn ALT_UP_HANDLER() {
        if (maybe_anchor_point) {
            auto [upper_row, lower_row] =
                std::minmax(maybe_anchor_point->row, text_cursor.row);
            if (upper_row > 0) {
                text_buffer.shift_lines_up(upper_row, lower_row + 1);
                --text_cursor.row;
                --maybe_anchor_point->row;

                for (size_t row = upper_row; row <= lower_row; ++row) {
                    text_buffer.starting_byte_offset.update_position_value(
                        row, text_buffer.at(row).size());
                }
            }

        } else if (text_cursor.row > 0) {

            text_buffer.shift_lines_up(text_cursor.row, text_cursor.row + 1);
            --text_cursor.row;

            text_buffer.starting_byte_offset.update_position_value(
                text_cursor.row, text_buffer.at(text_cursor.row).size());

            text_buffer.starting_byte_offset.update_position_value(
                text_cursor.row + 1,
                text_buffer.at(text_cursor.row + 1).size());
        }
        return StateReturn();
    }

    StateReturn ALT_DOWN_HANDLER() {
        if (maybe_anchor_point) {
            auto [upper_row, lower_row] =
                std::minmax(maybe_anchor_point->row, text_cursor.row);
            if (lower_row < text_buffer.num_lines() - 1) {
                text_buffer.shift_lines_down(upper_row, lower_row + 1);
                ++text_cursor.row;
                ++maybe_anchor_point->row;

                for (size_t row = upper_row - 1; row < lower_row; ++row) {
                    text_buffer.starting_byte_offset.update_position_value(
                        row, text_buffer.at(row).size());
                }
            }

        } else if (text_cursor.row < text_buffer.num_lines() - 1) {
            text_buffer.shift_lines_down(text_cursor.row, text_cursor.row + 1);
            ++text_cursor.row;
            text_buffer.starting_byte_offset.update_position_value(
                text_cursor.row, text_buffer.at(text_cursor.row).size());

            text_buffer.starting_byte_offset.update_position_value(
                text_cursor.row - 1,
                text_buffer.at(text_cursor.row - 1).size());
        }

        return StateReturn();
    }

    StateReturn CTRL_SHIFT_LEFT_ARROW_HANDLER() {

        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_left_over_boundary(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }

        return StateReturn();
    }

    StateReturn CTRL_SHIFT_RIGHT_ARROW_HANDLER() {

        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_cursor_right_over_boundary(text_cursor);
        text_plane_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }

        return StateReturn();
    }

    // Clipboard manip
    // Copy
    StateReturn CTRL_C_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            clipboard = text_buffer.get_lines(lp, rp);
        } else {
            // for now do nothing
        }
        return StateReturn();
    }

    // Cut
    StateReturn CTRL_X_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            clipboard = text_buffer.get_lines(lp, rp);

            // we need to get the lines before it's too late
            size_t old_end_offset = text_buffer.get_offset_from_point(rp);

            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            maybe_anchor_point.reset();
            text_plane_ptr->chase_point(lp);
            reparse_text(lp, rp, lp, text_buffer.get_offset_from_point(lp),
                         old_end_offset, text_buffer.get_offset_from_point(lp));
        } else {
            // for now do nothing
        }
        return StateReturn();
        // quit selection mode
    }

    // Paste
    StateReturn CTRL_V_HANLDER() {
        if (clipboard.empty()) {
            return StateReturn();
        }

        auto old_left = text_cursor;
        auto old_right = text_cursor;
        auto old_end_byte_offset = text_buffer.get_offset_from_point(old_right);

        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);

            old_left = lp;
            old_right = rp;
            old_end_byte_offset = text_buffer.get_offset_from_point(old_right);

            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            text_plane_ptr->chase_point(text_cursor);
        }
        text_cursor = text_buffer.insert_text_at(text_cursor, clipboard);
        maybe_anchor_point.reset();
        text_plane_ptr->chase_point(text_cursor);
        auto new_right = text_cursor;

        reparse_text(old_left, old_right, new_right,
                     text_buffer.get_offset_from_point(old_left),
                     old_end_byte_offset,
                     text_buffer.get_offset_from_point(new_right));

        return StateReturn();
    }

    // Parse
    StateReturn CTRL_P_HANDLER() {
        set_parse_lang(Parser<TextBuffer>::LANG::CPP);
        return StateReturn();
    }

    // Handlers that cause state changes

    // Open
    StateReturn CTRL_R_HANDLER() {
        return StateReturn(
            new FileOpenerState(&file, &text_buffer, &text_cursor));
    }

    // Save
    StateReturn CTRL_O_HANDLER() {
        return StateReturn(new FileSaverState(&file, &text_buffer));
    }

    // Search
    StateReturn CTRL_W_HANDLER() {
        // Enter the search State?
        return StateReturn();
    }

    // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Other stuff

    // =============== Helper Methods
    Cursor move_cursor_right(Cursor const &p) const {
        Cursor to_return = p;
        if (to_return.col == text_buffer.at(to_return.row).size() &&
            to_return.row + 1 < text_buffer.num_lines()) {
            // move down one line
            to_return.col = 0;
            to_return.effective_col = 0;
            ++to_return.row;
        } else if (to_return.col < text_buffer.at(to_return.row).size()) {
            to_return.effective_col +=
                StringUtils::symbol_into_width(text_buffer[to_return]);
            ++to_return.col;
        }
        return to_return;
    }

    Cursor move_cursor_up(Cursor const &p) const {
        Cursor to_return = p;
        if (text_plane_ptr->get_wrap_status() == WrapStatus::NOWRAP) {
            // non-wrapping movement
            to_return.col =
                std::min(to_return.col, text_buffer.at(--to_return.row).size());
            return to_return;
        }

        auto [num_rows, num_cols] = text_plane_ptr->get_plane_yx_dim();
        // first is col, second is effective width

        // returns the point moved up on the line
        // unless it cannot do that
        std::optional<Cursor> maybe_up_point = StringUtils::maybe_up_point(
            text_buffer.at(to_return.row), to_return, num_cols);

        if (maybe_up_point) {
            return maybe_up_point.value();
        }
        // then it was already on its first chunk.
        if (to_return.row == 0) {
            to_return.col = 0;
            to_return.effective_col = 0;
        } else {
            return StringUtils::final_chunk(text_buffer.at(--to_return.row),
                                            to_return, num_cols);
        }

        return to_return;
    }

    Cursor move_cursor_down(Cursor const &p) const {
        Cursor to_return = p;

        if (text_plane_ptr->get_wrap_status() == WrapStatus::NOWRAP) {
            // non-wrapping movement
            if (to_return.row + 1 < text_buffer.num_lines()) {
                ++to_return.row;
                to_return.col = std::min(to_return.col,
                                         text_buffer.at(to_return.row).size());
            }
            return to_return;
        }

        auto [num_rows, num_cols] = text_plane_ptr->get_plane_yx_dim();

        std::optional<Cursor> maybe_down_point = StringUtils::maybe_down_point(
            text_buffer.at(to_return.row), to_return, num_cols);

        if (maybe_down_point) {
            return maybe_down_point.value();
        }

        // then it was already on its last chunk.
        if (to_return.row == text_buffer.num_lines() - 1) {

            to_return.col = text_buffer.at(to_return.row).size();
            to_return.effective_col =
                StringUtils::var_width_str_into_effective_width(
                    text_buffer.at(to_return.row));

            return to_return;
        } else {
            return StringUtils::first_chunk(text_buffer.at(++to_return.row),
                                            to_return, num_cols);
        }

        return to_return;
    }

    Cursor move_cursor_left(Cursor const &p) const {
        Cursor to_return = p;
        // update logical cursor
        if (to_return.col > 0) {

            --to_return.col;
            to_return.effective_col -= StringUtils::symbol_into_width(
                text_buffer.at(to_return.row)[to_return.col]);
        } else if (to_return.row > 0) {
            to_return.col = text_buffer.at(--to_return.row).size();
            to_return.effective_col =
                StringUtils::var_width_str_into_effective_width(
                    text_buffer.at(to_return.row));
        }

        return to_return;
    }

    Cursor move_cursor_right_over_boundary(Cursor const &p) const {
        // eventually this needs to be semantic
        // (based on tree-sitter cursor or something)
        // for now we'll do a basic one
        Cursor cursor_to_return = p;
        CharType type_to_skip;
        if (cursor_to_return.col ==
                text_buffer.at(cursor_to_return.row).size() &&
            cursor_to_return.row + 1 < text_buffer.num_lines()) {
            // moves it up one row and to the last char of that row
            cursor_to_return = move_cursor_right(cursor_to_return);
            type_to_skip = char_type(text_buffer[cursor_to_return]);
        } else {
            type_to_skip = char_type(text_buffer[cursor_to_return]);
        }

        // cursor_to_return = move_cursor_right(cursor_to_return);

        while (cursor_to_return.col <
                   text_buffer.at(cursor_to_return.row).size() &&
               char_type(text_buffer[cursor_to_return]) == type_to_skip) {
            cursor_to_return = move_cursor_right(cursor_to_return);
        }

        return cursor_to_return;
    }

    Cursor move_cursor_left_over_boundary(Cursor const &p) const {
        // eventually this needs to be semantic
        // (based on tree-sitter cursor or something)
        // for now we'll do a basic one

        Cursor cursor_to_return = p;
        CharType type_to_skip;
        if (cursor_to_return.col == 0 && cursor_to_return.row > 0) {
            // moves it up one row and to the last char of that row
            cursor_to_return = move_cursor_left(cursor_to_return);
            type_to_skip = char_type('\n');
        } else {
            cursor_to_return = move_cursor_left(cursor_to_return);
            type_to_skip = char_type(text_buffer[cursor_to_return]);
        }

        while (cursor_to_return.col > 0) {
            Cursor lookbehind = move_cursor_left(cursor_to_return);
            if (char_type(text_buffer[lookbehind]) != type_to_skip) {
                break;
            }
            cursor_to_return = lookbehind;
        }

        return cursor_to_return;
    }

    // call this to update the parser
    void reparse_text(Cursor start_point, Cursor old_end_point,
                      Cursor new_end_point, size_t start_byte,
                      size_t old_end_byte, size_t new_end_byte) {
        // quit if there is no parser
        if (!maybe_parser) {
            return;
        }

        // TODO: get the update infomation here
        maybe_parser->update(start_point, old_end_point, new_end_point,
                             start_byte, old_end_byte, new_end_byte);
    }

  private:
    // some helper functions:

    enum class CharType {
        ALPHA_NUMERIC_UNDERSCORE,
        WHITESPACE,
        OTHER,
    };

    static CharType char_type(char ch) {
        if (std::isspace(ch)) {
            return CharType::WHITESPACE;
        }

        if (std::isalpha(ch) || std::isdigit(ch) || ch == '_') {
            return CharType::ALPHA_NUMERIC_UNDERSCORE;
        }

        return CharType::OTHER;
    }
};

struct StateStack {
    // owns all the states?

    std::vector<ProgramState *> state_stack;

    ~StateStack() {
        while (state_stack.empty()) {
            delete state_stack.back();
            state_stack.pop_back();
        }
    }

    void initial_setup(std::optional<std::string_view> maybe_filename) {
        state_stack.push_back(new TextState(maybe_filename));
    }

    void pop() {
        // TODO: do we need to use any casts?
        if (&prompt_state != state_stack.back()) {
            delete state_stack.back();
        }
        state_stack.pop_back();
    }

    void push(ProgramState *new_state) {
        state_stack.push_back(new_state);
    }

    bool empty() const {
        return state_stack.empty();
    }

    ProgramState *active_state() {
        return state_stack.back();
    }

    friend std::ostream &operator<<(std::ostream &os, StateStack const &ss) {
        os << "[";
        for (auto const &st : ss.state_stack) {
            if (st) {
                os << *st;
            } else {
                os << "{nullptr}";
            }
            os << ",";
        }
        os << "]";
        return os;
    }
};

struct Program {

    StateStack state_stack;

    View view;
    EventQueue event_queue;

    Program(std::optional<std::string_view> maybe_filename, notcurses *nc,
            unsigned height, unsigned width)
        : view(nc, height, width),
          event_queue(view.get_nc_ptr()) {

        // This is still plenty ugly.
        ProgramState::set_eq_ptr(&event_queue);
        ProgramState::set_view_ptr(&view);
        view.set_prompt_plane(prompt_state.get_prompt_plane_model());

        state_stack.initial_setup(maybe_filename);
    }

    void run_event_loop() {
        assert(!state_stack.empty());
        state_stack.active_state()->enter();
        while (!state_stack.empty()) {

            // view.render_status();
            state_stack.active_state()->trigger_render();
            view.refresh_screen();
            Event ev = event_queue.get_event();

            // for now handle quitting here
            if (ev.is_input() && ev.get_input().id == 'W' &&
                ev.get_input().modifiers == NCKEY_MOD_CTRL) {
                break;
            }

            // TODO: eventually we need to bubble up unhandled
            // events
            StateReturn sr = state_stack.active_state()->handle_event(ev);
            switch (sr.transition_type) {
                using enum StateReturn::Transition;
            case ENTER: {
                state_stack.push(sr.next_state_ptr);
                state_stack.active_state()->enter();
            } break;
            case EXIT: {
                state_stack.active_state()->exit();
                state_stack.pop();
            }
            // TODO: handle bubbling inputs
            default:
                break;
            }
        }
    }
};
