#pragma once

#include "notcurses/notcurses.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include <iostream>

#include "text_buffer.h"
#include "view.h"

// list all the handler functions here?

void null_func() {}

struct KeyBinds {
    std::array<std::function<void()>, 1024> handler_list;

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

    std::function<void()> operator[](ncinput input) {
        return handler_list[get_hash(input)];
    }

    // returns false if handler is not registered, happens if attempting to
    // overwrite a a handler that isnt null_func
    bool register_handler(ncinput input, std::function<void()> handler) {
        auto ptr = handler_list[get_hash(input)].target<void (*)()>();
        if (ptr && *ptr != null_func) {
            return false;
        }
        handler_list[get_hash(input)] = std::move(handler);
        return true;
    }
};
