#include "app_state.h"
#include <string.h>

static AppState G;

AppState* app_state(void) { return &G; }

void app_state_init_defaults(void) {
    memset(&G, 0, sizeof(G));
    G.running = 1;
    G.cfg.clear_clipboard_after_save = 1;
    G.cfg.poll_interval_ms = 2000;
    G.cfg.autostart = 0;
    G.prefix_enabled = false;
    G.prefix[0] = '\0';
    G.prefix_A = 0; G.prefix_B = 0;
    G.curr_base[0] = '\0';
    G.curr_mask = 0u;
}
