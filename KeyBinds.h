#pragma once

#include "notcurses/notcurses.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "text_buffer.h"
#include "view.h"

// list all the handler functions here?

struct KeyInfo {
    // first 32 bits are id, remaining bits are modifiers
    uint64_t id_modifiers;

    KeyInfo(ncinput input)
        : id_modifiers(((uint64_t)input.id << 32) | input.modifiers) {}

    KeyInfo(KeyInfo const &) = default;
    KeyInfo(KeyInfo &&) = default;
    KeyInfo &operator=(KeyInfo const &) = default;
    KeyInfo &operator=(KeyInfo const &) = default;
};

struct KeyBinds {
    std::vector<std::pair<KeyInfo, std::function<void()>>> handler_list;
};
