#include "Program.h"
#include <notcurses/notcurses.h>

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {

    std::optional<std::string_view> maybe_filename;
    if (argc > 1) {
        // for now we assuming second argv is filename
        maybe_filename = argv[1];
    }

    static struct notcurses_options nc_options = {
        // .loglevel = NCLOGLEVEL_TRACE,
        .flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR,
    };
    notcurses *nc = notcurses_init(&nc_options, nullptr);
    unsigned int y, x;
    ncplane_dim_yx(notcurses_stdplane(nc), &y, &x);
    // disable the conversion into the signal
    notcurses_linesigs_disable(nc);

    // set up initial state
    {
        Program program_state(maybe_filename, nc, y, x);
        program_state.run_event_loop();
    }
    // we have to stop this only after the program has quit
    notcurses_stop(nc);

    return 0;
}
