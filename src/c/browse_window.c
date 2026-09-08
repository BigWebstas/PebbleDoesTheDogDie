#include <pebble.h>
#include "app.h"
#include "ui.h"

// Two singleton MenuLayer screens for the trigger browser:
//   categories  ->  triggers in a category  ->  (topic_detail_window in def mode)
// Back navigation falls out naturally because each level is its own window.

// ===========================================================================
// Categories
// ===========================================================================

static Window *s_cat_window;
static MenuLayer *s_cat_menu;
static StatusBarLayer *s_cat_status;
static TextLayer *s_cat_attrib;

static void topics_window_open(int cat_id, const char *cat_name);  // fwd

static bool cats_ready(void) { return g_cat_count > 0; }

static uint16_t cat_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return cats_ready() ? g_cat_count : 1;
}

static int16_t cat_cell_height(struct MenuLayer *menu, MenuIndex *idx, void *ctx) {
  return cats_ready() ? PBL_IF_ROUND_ELSE(44, 40) : INFO_CELL_HEIGHT;
}

static int16_t cat_header_height(struct MenuLayer *menu, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void cat_draw_header(GContext *gctx, const Layer *cl, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cl, "Trigger categories");
}

static void cat_draw_row(GContext *gctx, const Layer *cl, MenuIndex *idx, void *ctx) {
  if (!cats_ready()) {
    const char *msg = (g_state == STATE_ERROR)
      ? (g_error_msg[0] ? g_error_msg : "Something went wrong.")
      : (g_error_msg[0] ? g_error_msg : "Loading categories…");
    ui_draw_info_cell(gctx, cl, msg);
    return;
  }
  menu_cell_basic_draw(gctx, cl, g_cats[idx->row].name, NULL, NULL);
}

static void cat_select(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!cats_ready()) {
    if (g_state == STATE_ERROR) request_browse_cats();
    return;
  }
  Cat *c = &g_cats[idx->row];
  topics_window_open(c->id, c->name);
}

static void cat_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_cat_status = status_bar_layer_create();
  status_bar_layer_set_colors(s_cat_status, GColorClear, GColorBlack);

  GRect mf = bounds;
#if !defined(PBL_ROUND)
  mf.origin.y += STATUS_BAR_LAYER_HEIGHT;
  mf.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif
  mf.size.h -= ATTRIB_HEIGHT;

  s_cat_menu = menu_layer_create(mf);
  menu_layer_set_callbacks(s_cat_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = cat_num_rows,
    .get_cell_height = cat_cell_height,
    .get_header_height = cat_header_height,
    .draw_header = cat_draw_header,
    .draw_row = cat_draw_row,
    .select_click = cat_select,
  });
  menu_layer_set_click_config_onto_window(s_cat_menu, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_cat_menu, GColorJaegerGreen, GColorWhite);
#endif

  layer_add_child(root, menu_layer_get_layer(s_cat_menu));
  layer_add_child(root, status_bar_layer_get_layer(s_cat_status));
  s_cat_attrib = ui_add_attribution(window);
}

static void cat_window_unload(Window *window) {
  menu_layer_destroy(s_cat_menu);
  status_bar_layer_destroy(s_cat_status);
  text_layer_destroy(s_cat_attrib);
  window_destroy(window);
  s_cat_window = NULL;
  s_cat_menu = NULL;
}

void browse_window_push(void) {
  if (!s_cat_window) {
    s_cat_window = window_create();
    window_set_window_handlers(s_cat_window, (WindowHandlers) {
      .load = cat_window_load,
      .unload = cat_window_unload,
    });
  }
  window_stack_push(s_cat_window, true);
}

// ===========================================================================
// Triggers within a category
// ===========================================================================

static Window *s_top_window;
static MenuLayer *s_top_menu;
static StatusBarLayer *s_top_status;
static TextLayer *s_top_attrib;

static bool topics_ready(void) {
  return g_browse_topic_count > 0 && g_state != STATE_LOADING_BROWSE;
}

static uint16_t top_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return topics_ready() ? g_browse_topic_count : 1;
}

static int16_t top_cell_height(struct MenuLayer *menu, MenuIndex *idx, void *ctx) {
  return topics_ready() ? PBL_IF_ROUND_ELSE(44, 40) : INFO_CELL_HEIGHT;
}

static int16_t top_header_height(struct MenuLayer *menu, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void top_draw_header(GContext *gctx, const Layer *cl, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cl, g_browse_cat_name[0] ? g_browse_cat_name : "Triggers");
}

static void top_draw_row(GContext *gctx, const Layer *cl, MenuIndex *idx, void *ctx) {
  if (!topics_ready()) {
    const char *msg;
    if (g_state == STATE_ERROR) msg = g_error_msg[0] ? g_error_msg : "Something went wrong.";
    else if (g_browse_topic_count == 0 && g_state == STATE_BROWSE) msg = "No triggers here.";
    else msg = g_error_msg[0] ? g_error_msg : "Loading triggers…";
    ui_draw_info_cell(gctx, cl, msg);
    return;
  }
  BrowseTopic *b = &g_browse_topics[idx->row];
  char title[QUESTION_LEN + 4];
  if (b->starred) snprintf(title, sizeof(title), "★ %s", b->question);
  else            snprintf(title, sizeof(title), "%s", b->question);
  menu_cell_basic_draw(gctx, cl, title, b->starred ? "Starred" : NULL, NULL);
}

static void top_select(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!topics_ready()) {
    if (g_state == STATE_ERROR && g_browse_cat_id) {
      request_browse_topics(g_browse_cat_id, g_browse_cat_name);
    }
    return;
  }
  request_browse_topic(g_browse_topics[idx->row].id);
  topic_detail_window_push_def();
}

static void top_long_select(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!topics_ready()) return;
  BrowseTopic *b = &g_browse_topics[idx->row];
  b->starred = !b->starred;
  request_star(b->id, b->starred);
  vibes_short_pulse();
  menu_layer_reload_data(menu);
}

static void top_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_top_status = status_bar_layer_create();
  status_bar_layer_set_colors(s_top_status, GColorClear, GColorBlack);

  GRect mf = bounds;
#if !defined(PBL_ROUND)
  mf.origin.y += STATUS_BAR_LAYER_HEIGHT;
  mf.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif
  mf.size.h -= ATTRIB_HEIGHT;

  s_top_menu = menu_layer_create(mf);
  menu_layer_set_callbacks(s_top_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = top_num_rows,
    .get_cell_height = top_cell_height,
    .get_header_height = top_header_height,
    .draw_header = top_draw_header,
    .draw_row = top_draw_row,
    .select_click = top_select,
    .select_long_click = top_long_select,
  });
  menu_layer_set_click_config_onto_window(s_top_menu, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_top_menu, GColorJaegerGreen, GColorWhite);
#endif

  layer_add_child(root, menu_layer_get_layer(s_top_menu));
  layer_add_child(root, status_bar_layer_get_layer(s_top_status));
  s_top_attrib = ui_add_attribution(window);
}

static void top_window_unload(Window *window) {
  menu_layer_destroy(s_top_menu);
  status_bar_layer_destroy(s_top_status);
  text_layer_destroy(s_top_attrib);
  window_destroy(window);
  s_top_window = NULL;
  s_top_menu = NULL;
}

static void topics_window_open(int cat_id, const char *cat_name) {
  request_browse_topics(cat_id, cat_name);
  if (!s_top_window) {
    s_top_window = window_create();
    window_set_window_handlers(s_top_window, (WindowHandlers) {
      .load = top_window_load,
      .unload = top_window_unload,
    });
  }
  window_stack_push(s_top_window, true);
}

// ===========================================================================

void browse_window_reload(void) {
  if (s_cat_menu) menu_layer_reload_data(s_cat_menu);
  if (s_top_menu) menu_layer_reload_data(s_top_menu);
}
