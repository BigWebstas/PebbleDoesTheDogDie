#include <pebble.h>
#include "ui.h"

void ui_draw_info_cell(GContext *ctx, const Layer *cell_layer, const char *text) {
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);

  GRect bounds = layer_get_bounds(cell_layer);
  GRect box = grect_inset(bounds, GEdgeInsets(6, 8));

  graphics_draw_text(ctx, text,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     box, GTextOverflowModeWordWrap,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft),
                     NULL);
}

TextLayer *ui_add_attribution(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  GRect f = GRect(0, b.size.h - ATTRIB_HEIGHT, b.size.w, ATTRIB_HEIGHT);

  TextLayer *t = text_layer_create(f);
  text_layer_set_text(t, "Powered by\nDoesTheDogDie.com");
  text_layer_set_font(t, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  text_layer_set_overflow_mode(t, GTextOverflowModeWordWrap);
  text_layer_set_background_color(t, GColorClear);
#if defined(PBL_COLOR)
  text_layer_set_text_color(t, GColorDarkGray);
#endif
  layer_add_child(root, text_layer_get_layer(t));
  return t;
}
