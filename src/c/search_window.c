#include <pebble.h>
#include "app.h"
#include "ui.h"

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status;
static Layer *s_splash;
static GBitmap *s_logo;

#if defined(PBL_MICROPHONE)
static const char *VOICE_TITLE = "Search by voice";
static const char *VOICE_SUB   = "Press select, say a title";
#else
static const char *VOICE_TITLE = "Search from phone";
static const char *VOICE_SUB   = "This watch has no mic - use the app settings";
#endif

// Row 0 is always the "search" action. Rows 1.. are recent titles.
static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return 1 + g_recents_count;
}

static int16_t get_cell_height(struct MenuLayer *menu, MenuIndex *idx, void *ctx) {
#if defined(PBL_ROUND)
  return menu_layer_is_index_selected(menu, idx) ? 60 : 50;
#else
  return 44;
#endif
}

static void draw_row(GContext *gctx, const Layer *cell_layer, MenuIndex *idx, void *ctx) {
  if (idx->row == 0) {
    menu_cell_basic_draw(gctx, cell_layer, VOICE_TITLE, VOICE_SUB, NULL);
    return;
  }

  int r = idx->row - 1;
  if (r >= g_recents_count) return;
  Result *item = &g_recents[r];

  char subtitle[YEAR_LEN + TYPE_LEN + 4];
  if (item->year[0] && item->type[0]) {
    snprintf(subtitle, sizeof(subtitle), "%s  %s", item->year, item->type);
  } else if (item->year[0]) {
    snprintf(subtitle, sizeof(subtitle), "%s", item->year);
  } else {
    snprintf(subtitle, sizeof(subtitle), "%s", item->type);
  }
  menu_cell_basic_draw(gctx, cell_layer, item->name, subtitle[0] ? subtitle : NULL, NULL);
}

static int16_t get_header_height(struct MenuLayer *menu, uint16_t section, void *ctx) {
  return g_recents_count ? MENU_CELL_BASIC_HEADER_HEIGHT : 0;
}

static void draw_header(GContext *gctx, const Layer *cell_layer, uint16_t section, void *ctx) {
  if (g_recents_count) menu_cell_basic_header_draw(gctx, cell_layer, "Recent");
}

static void select_row(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (idx->row == 0) {
#if defined(PBL_MICROPHONE)
    dictation_start();
#else
    vibes_short_pulse();
#endif
    return;
  }
  int r = idx->row - 1;
  if (r >= g_recents_count) return;
  request_media(g_recents[r].id, g_recents[r].name);
  topics_window_push();
}

// ---- Splash (logo + text), shown until recents load -----------------------

static void splash_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  int16_t text_y = b.size.h / 3;
  if (s_logo) {
    GRect lr = gbitmap_get_bounds(s_logo);
    int16_t top = b.size.h / 3 - lr.size.h / 2 - 4;
    GRect dst = GRect((b.size.w - lr.size.w) / 2, top, lr.size.w, lr.size.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_logo, dst);
    text_y = top + lr.size.h + 10;
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Does the Dog Die?", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(6, text_y, b.size.w - 12, b.size.h - text_y - 6),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void update_splash_visibility(void) {
  if (s_splash) {
    layer_set_hidden(s_splash, g_recents_loaded);
    layer_mark_dirty(s_splash);
  }
}

// ---- Window lifecycle ----------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_status = status_bar_layer_create();
  status_bar_layer_set_colors(s_status, GColorClear, GColorBlack);

  GRect menu_frame = bounds;
#if !defined(PBL_ROUND)
  menu_frame.origin.y += STATUS_BAR_LAYER_HEIGHT;
  menu_frame.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif

  s_menu = menu_layer_create(menu_frame);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_row,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu, GColorJaegerGreen, GColorWhite);
#endif

  s_logo = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LOGO);
  s_splash = layer_create(menu_frame);
  layer_set_update_proc(s_splash, splash_update);

  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, s_splash);
  layer_add_child(root, status_bar_layer_get_layer(s_status));

  update_splash_visibility();
}

static void window_unload(Window *window) {
  layer_destroy(s_splash);
  gbitmap_destroy(s_logo);
  menu_layer_destroy(s_menu);
  status_bar_layer_destroy(s_status);
  s_window = NULL;
  s_menu = NULL;
  s_splash = NULL;
  s_logo = NULL;
}

void search_window_reload(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
  update_splash_visibility();
}

void search_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
