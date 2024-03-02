#include "Program.h"

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    std::optional<std::string_view> maybe_filename;
    if (argc > 1) {
        // for now we assuming second argv is filename
        maybe_filename = argv[1];
    }

    // set up initial state
    Program program_state(maybe_filename);
    program_state.run_event_loop();
    return 0;
}
