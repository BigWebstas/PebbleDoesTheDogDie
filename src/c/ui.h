#pragma once
#include <pebble.h>

// Height reserved for a single wrapped info / error row in a menu.
#define INFO_CELL_HEIGHT 150

// Draw a word-wrapped informational paragraph inside a menu cell, picking a
// text color that contrasts with the (possibly highlighted) background.
void ui_draw_info_cell(GContext *ctx, const Layer *cell_layer, const char *text);

// Height of the "Powered by DoesTheDogDie.com" footer (required attribution on
// every screen that shows their data). Two lines - the phrase does not fit one
// line at the smallest system font on a 144 px screen.
#define ATTRIB_HEIGHT 26

// Add the attribution footer as the bottom ATTRIB_HEIGHT px of `window`'s root
// layer. Call from window_load after sizing the main content to leave room.
// Returns the layer; singletons can ignore it, per-push windows should
// text_layer_destroy() it in window_unload.
TextLayer *ui_add_attribution(Window *window);
