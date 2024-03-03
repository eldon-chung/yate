#pragma once

#include <signal.h>

#include <assert.h>

#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

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

StateReturn null_func() {
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
        event_queue_ptr->post_message(to_send);

        cmd_buf.clear();
        prompt_str.clear();
        cursor = 0;
        has_response = false;
    }

    void register_keybinds() {
        // nothing here right now
    }

    void setup(std::string_view ps, std::string_view target) {
        prompt_str = ps;
        target_str = target;
    }

    PromptPlaneModel get_prompt_plane_model() {
        return PromptPlaneModel{&prompt_str, &cursor, &cmd_buf};
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

        if (nc_input.modifiers == NCKEY_MOD_CTRL && nc_input.id == 'C') {
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
            if (!file_ptr->try_open_or_create()) {
                // file was not just created
                substate = ASK_OVERWRITE;
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
            case SUCCESS:
                event_queue_ptr->post_message(maybe_target_for_response.value(),
                                              "SUCCESS");
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
            view_ptr->notify("File cannot be written to.");
            substate = FAIL;
        } else if (!file_ptr->write(text_buffer->get_view())) {
            view_ptr->notify("Error saving to file.");
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
    std::optional<std::string> maybe_filename;

  public:
    FileOpenerState(File *fp, TextBuffer *tbp)
        : ProgramState(),
          file_ptr(fp),
          text_buffer_ptr(tbp),
          maybe_filename(std::nullopt) {
    }

    ~FileOpenerState() {
    }

    void print(std::ostream &os) const {
        os << "{FileOpenerState }";
    }

    StateReturn handle_msg(std::string_view msg) {
        // this is where the bulk of our logic is done

        // Note: we need to decide what to do about
        // the currently existing file
        // for now we quit without saving which is not ideal

        // we resume our computations here?
        if (!msg.starts_with("FileOpenerState:")) {
            return StateReturn(false);
        }

        std::string_view remaining = msg.substr(16);

        size_t jump_idx = (size_t)-1;
        if (remaining.starts_with("STARTFILEOPEN")) {
            jump_idx = 0;
        } else if (remaining.starts_with("OPENFILENAME:")) {
            remaining = remaining.substr(13);
            if (remaining.starts_with("str=") && remaining.size() > 4) {
                // get the filename
                maybe_filename = remaining.substr(4);
                jump_idx = 1;
            } else {
                return StateReturn(StateReturn::Transition::EXIT);
            }
        } else if (remaining.starts_with("SAVECURRENTFILE:")) {
            remaining = remaining.substr(16);
            if (remaining == "str=Y" || remaining == "str=y") {
                // get the filename
                // then we need to start the save state
                return StateReturn(new FileSaverState(file_ptr, text_buffer_ptr,
                                                      "FileOpenerState"));
            } else if (remaining == "str=N" || remaining == "str=n") {
                jump_idx = 2;
            } else {
                return StateReturn(StateReturn::Transition::EXIT);
            }
        } else if (remaining.starts_with("str=SAVEFAILED")) {
            return StateReturn(StateReturn::Transition::EXIT);
        } else if (remaining.starts_with("str=SAVESUCCESS")) {
            jump_idx = 2;
        }

        switch (jump_idx) {
        case 0:
            // ask for file name to open
            prompt_state.setup("Enter filename to open:",
                               "FileOpenerState:OPENFILENAME");
            return StateReturn(&prompt_state);
        case 1:
            // we got the filename, for now always ask about
            // saving contents
            // TODO: check against the undo stack
            assert(maybe_filename.has_value());
            prompt_state.setup(
                "Do you want to save your current contents? [y/N]",
                "FileOpenerState:SAVECURRENTFILE");
            return StateReturn(&prompt_state);
        case 2:
            // now all we do is open the file
            {
                File attempt = File(maybe_filename.value());
                if (attempt.get_mode() == File::Mode::READONLY ||
                    attempt.get_mode() == File::Mode::READWRITE) {
                    *file_ptr = std::move(attempt);
                    text_buffer_ptr->load_contents(
                        file_ptr->get_file_contents().value());
                } else {
                    view_ptr->notify("File cannot be read from.");
                    // don't overwrite file_ptr, don't mutate
                    // text_buffer_ptr
                }
                return StateReturn(StateReturn::Transition::EXIT);
            }
        default:
            break;
        }
        return StateReturn(false);
    }

    StateReturn handle_input([[maybe_unused]] ncinput nc_input) {
        return StateReturn(false);
    }

    void enter() {
        // send a message to kick things off
        event_queue_ptr->post_message("FileOpenerState:STARTFILEOPEN");
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
    Point text_cursor;
    std::optional<Point> maybe_anchor_point;
    std::vector<std::string> clipboard;
    size_t plane_fd;

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
        // do a non-creating open
        plane_fd = view_ptr->create_text_plane(this->get_text_plane_model());
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

    TextPlaneModel get_text_plane_model() {
        return TextPlaneModel{&text_buffer, &text_cursor, &maybe_anchor_point};
    }

    StateReturn handle_msg([[maybe_unused]] std::string_view msg) {
        // for now ignore it
        return StateReturn();
    }

    void trigger_render() {
        view_ptr->focus_text();
        view_ptr->render_text();
    }

    StateReturn handle_input(ncinput nc_input) {
        // we're going to manually handle some cases to save on
        // lookup
        if (nc_input.modifiers == 0 &&
            ((nc_input.id >= 32 && nc_input.id <= 255) ||
             nc_input.id == NCKEY_TAB)) {

            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            }
            text_buffer.insert_char_at(text_cursor, (char)nc_input.id);
            RIGHT_ARROW_HANDLER();
            view_ptr->chase_point(text_cursor);
            return StateReturn();
        }

        // TODO: handle unicode insertions

        // TODO: handle all the other modifiers for these cases
        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_BACKSPACE) {
            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            } else {
                Point old_pos = text_cursor;
                LEFT_ARROW_HANDLER();
                text_buffer.insert_backspace_at(old_pos);
            }
            view_ptr->chase_point(text_cursor);
            return StateReturn();
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_ENTER) {
            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            }
            text_buffer.insert_newline_at(text_cursor);
            RIGHT_ARROW_HANDLER();
            view_ptr->chase_point(text_cursor);
            return StateReturn();
        }

        if (nc_input.modifiers == 0 && nc_input.id == NCKEY_DEL) {
            if (maybe_anchor_point) {
                auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
                text_buffer.remove_text_at(lp, rp);
                text_cursor = lp;
                maybe_anchor_point.reset();
            } else {
                text_buffer.insert_delete_at(text_cursor);
            }
            view_ptr->chase_point(text_cursor);
            return StateReturn();
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
                            &TextState::LEFT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_RIGHT, NCKEY_MOD_SHIFT,
                            &TextState::RIGHT_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_DOWN, NCKEY_MOD_SHIFT,
                            &TextState::DOWN_ARROW_HANDLER);
        REGISTER_MODDED_KEY(NCKEY_UP, NCKEY_MOD_SHIFT,
                            &TextState::UP_ARROW_HANDLER);

        // File manipulators
        REGISTER_MODDED_KEY('O', NCKEY_MOD_CTRL, &TextState::CTRL_O_HANDLER);
        REGISTER_MODDED_KEY('R', NCKEY_MOD_CTRL, &TextState::CTRL_R_HANDLER);
        REGISTER_MODDED_KEY('U', NCKEY_MOD_CTRL, &TextState::CTRL_U_HANLDER);
    }

  private:
    //   All the handlers for Text Editing
    StateReturn LEFT_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::min(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        } else {
            text_cursor = move_point_left(text_cursor);
        }
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }
    StateReturn RIGHT_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::max(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        } else {
            text_cursor = move_point_right(text_cursor);
        }
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }
    StateReturn UP_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::min(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        }
        text_cursor = move_point_up(text_cursor);
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }
    StateReturn DOWN_ARROW_HANDLER() {
        if (maybe_anchor_point) {
            text_cursor = std::max(text_cursor, *maybe_anchor_point);
            maybe_anchor_point.reset();
        }
        text_cursor = move_point_down(text_cursor);
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }

    StateReturn SHIFT_LEFT_ARROW_HANDLER() {
        if (!maybe_anchor_point) {
            maybe_anchor_point = text_cursor;
        }

        text_cursor = move_point_left(text_cursor);
        view_ptr->chase_point(text_cursor);

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

        text_cursor = move_point_right(text_cursor);
        view_ptr->chase_point(text_cursor);

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

        text_cursor = move_point_up(text_cursor);
        view_ptr->chase_point(text_cursor);

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

        text_cursor = move_point_down(text_cursor);
        view_ptr->chase_point(text_cursor);

        assert(maybe_anchor_point);
        if (*maybe_anchor_point == text_cursor) {
            maybe_anchor_point.reset();
        }
        return StateReturn();
    }

    // Clipboard manip
    StateReturn CTRL_C_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            clipboard = text_buffer.get_lines(lp, rp);
        } else {
            // for now do nothing
        }
        return StateReturn();
    }

    StateReturn CTRL_X_HANDLER() {
        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            clipboard = text_buffer.get_lines(lp, rp);
            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            maybe_anchor_point.reset();
            view_ptr->chase_point(lp);
        } else {
            // for now do nothing
        }
        return StateReturn();
        // quit selection mode
    }

    StateReturn CTRL_V_HANDLER() {
        if (clipboard.empty()) {
            return StateReturn();
        }

        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            view_ptr->chase_point(text_cursor);
        }
        text_cursor = text_buffer.insert_text_at(text_cursor, clipboard);
        maybe_anchor_point.reset();
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }

    // Paste
    StateReturn CTRL_U_HANLDER() {
        if (clipboard.empty()) {
            return StateReturn();
        }

        if (maybe_anchor_point) {
            auto [lp, rp] = std::minmax(*maybe_anchor_point, text_cursor);
            text_buffer.remove_text_at(lp, rp);
            text_cursor = lp;
            view_ptr->chase_point(text_cursor);
        }
        text_cursor = text_buffer.insert_text_at(text_cursor, clipboard);
        maybe_anchor_point.reset();
        view_ptr->chase_point(text_cursor);
        return StateReturn();
    }

    // Handlers that cause state changes

    // Open
    StateReturn CTRL_R_HANDLER() {
        return StateReturn(new FileOpenerState(&file, &text_buffer));
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
    Point move_point_right(Point const &p) const {
        Point to_return = p;
        if (to_return.col == text_buffer.at(to_return.row).size() &&
            to_return.row + 1 < text_buffer.num_lines()) {
            // move down one line
            to_return.col = 0;
            ++to_return.row;
        } else if (to_return.col < text_buffer.at(to_return.row).size()) {
            ++to_return.col;
        }
        return to_return;
    }

    Point move_point_up(Point const &p) const {
        Point to_return = p;
        if (view_ptr->get_wrap_status() == WrapStatus::WRAP) {
            auto [num_rows, num_cols] = view_ptr->get_text_plane_dim(plane_fd);
            if (to_return.col >= num_cols) {
                to_return.col -= num_cols;
            } else if (to_return.row > 0) {
                --to_return.row;
                size_t line_size = text_buffer.at(to_return.row).size();
                size_t last_chunk_col = line_size / num_cols * line_size;
                assert(to_return.col < num_cols);
                to_return.col =
                    std::min(to_return.col + last_chunk_col, line_size);
            } else if (to_return.row == 0) {
                to_return.col = 0;
            }
        } else {
            // non-wrapping movement
            to_return.col =
                std::min(to_return.col, text_buffer.at(--to_return.row).size());
        }

        return to_return;
    }

    Point move_point_down(Point const &p) const {
        Point to_return = p;
        if (view_ptr->get_wrap_status() == WrapStatus::WRAP) {
            auto [num_rows, num_cols] = view_ptr->get_text_plane_dim(plane_fd);
            size_t curr_line_size = text_buffer.at(to_return.row).size();

            if (curr_line_size == 0) {
                to_return.col = 0;
                to_return.row =
                    std::min(text_buffer.num_lines() - 1, to_return.row + 1);
            } else if ((to_return.col / curr_line_size * curr_line_size) +
                           num_cols <=
                       curr_line_size) {
                to_return.col =
                    std::min(to_return.col + num_cols, curr_line_size);
            } else if (to_return.row + 1 == text_buffer.num_lines()) {
                to_return.col = text_buffer.at(to_return.row).size();
            } else if (to_return.row + 1 < text_buffer.num_lines()) {
                ++to_return.row;
                to_return.col = std::min(to_return.col,
                                         text_buffer.at(to_return.row).size());
            }
        } else {
            // non-wrapping movement
            if (to_return.row + 1 < text_buffer.num_lines()) {
                ++to_return.row;
                to_return.col = std::min(to_return.col,
                                         text_buffer.at(to_return.row).size());
            }
        }

        return to_return;
    }

    Point move_point_left(Point const &p) const {
        Point to_return = p;
        // update logical cursor
        if (to_return.col > 0) {
            --to_return.col;
        } else if (to_return.row > 0) {
            to_return.col = text_buffer.at(--to_return.row).size();
        }
        return to_return;
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

    Program(std::optional<std::string_view> maybe_filename)
        : view(std::move(View::init_view())),
          event_queue(view.get_nc_ptr()) {

        // This is still plenty ugly.
        ProgramState::set_eq_ptr(&event_queue);
        ProgramState::set_view_ptr(&view);
        view.set_prompt_plane(prompt_state.get_prompt_plane_model());

        state_stack.initial_setup(maybe_filename);
    }

    void run_event_loop() {
        assert(!state_stack.empty());

        // if no notification render status?
        view.render_text();

        state_stack.active_state()->enter();
        while (!state_stack.empty()) {

            view.render_status();
            state_stack.active_state()->trigger_render();
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
