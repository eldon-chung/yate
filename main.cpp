#include <assert.h>

#include <iostream>

#include <notcurses/notcurses.h>

#include "state.h"
#include "view.h"

// ideally this ends up handling the meta state, not just the
// buffer
void handle_keypress(ncinput nc_input, State &state) {
    // for now we keep it at this level
    //  eventually this is render text_plane

    if (nc_input.evtype == NCTYPE_RELEASE) {
        return;
    }

    // bit of a hack since I'm supposed
    // to access the values via other means
    if (nc_input.modifiers != 0) {
        return;
    }

    // for now it just handles the chars
    state.handle_keypress(nc_input);
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    // set up initial state
    State state;

    // TODO: make this an actual constructed thing
    View view = std::move(View::init_view());

    view.render(state);

    while (true) {
        handle_keypress(view.get_keypress(), state);
        view.render(state);
    }
}
