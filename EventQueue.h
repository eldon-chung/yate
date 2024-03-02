#pragma once

#include <deque>
#include <string>

#include <notcurses/notcurses.h>

struct Event {
    enum class Type {
        INPUT,
        MESSAGE,
    };

    enum class Target { PARENT, ANY, ALL };

    Type type;
    Target target;
    ncinput input;
    std::string msg;

    Event(ncinput in)
        : type(Type::INPUT),
          target(Target::ANY),
          input(in),
          msg("") {
    }

    Event(std::string_view m)
        : type(Type::MESSAGE),
          target(Target::ANY),
          msg(m) {
    }

    Event(std::string_view m, Target targ)
        : type(Type::MESSAGE),
          target(targ),
          msg(m) {
    }

    using enum Target;
    void set_target_all() {
        target = ALL;
    }

    void set_target_any() {
        target = ANY;
    }

    bool is_message() const {
        return type == Type::MESSAGE;
    }

    bool is_input() const {
        return type == Type::INPUT;
    }

    ncinput get_input() const {
        return input;
    }

    std::string_view get_msg() const {
        return msg;
    }
};

struct EventQueue {
    notcurses *nc_ptr;
    std::deque<Event> event_queue;

    EventQueue(notcurses *np)
        : nc_ptr(np) {
    }

    Event get_event() {
        // has a ref to notcurses to get events?

        // events take higher priority for now
        if (!event_queue.empty()) {
            Event e = event_queue.front();
            event_queue.pop_front();
            return e;
        }

        struct ncinput input;
        notcurses_get(nc_ptr, nullptr, &input);
        return Event{input};
    }
};
