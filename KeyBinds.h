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

    KeyBinds() = default;

    std::function<void()> operator[](ncinput input) {
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

        std::cerr << "fn index: " << index << std::endl;

        return std::function<void(void)>(null_func);
    }
};
