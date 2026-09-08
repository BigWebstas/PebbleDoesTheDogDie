#include <pebble.h>
#include "app.h"
#include "ui.h"

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status;
static TextLayer *s_attrib;

static bool is_list_ready(void) {
  return g_state == STATE_RESULTS && g_results_count > 0;
}

static uint16_t get_num_sections(struct MenuLayer *menu, void *ctx) {
  return 1;
}

static int16_t get_header_height(struct MenuLayer *menu, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *gctx, const Layer *cell_layer, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cell_layer, g_query[0] ? g_query : "Results");
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return is_list_ready() ? g_results_count : 1;
}

static int16_t get_cell_height(struct MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) return INFO_CELL_HEIGHT;
#if defined(PBL_ROUND)
  return menu_layer_is_index_selected(menu, idx) ? 60 : 50;
#else
  return 44;
#endif
}

static void draw_row(GContext *gctx, const Layer *cell_layer, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) {
    const char *msg;
    switch (g_state) {
      case STATE_LOADING_SEARCH:
        msg = g_error_msg[0] ? g_error_msg : "Searching…";
        break;
      case STATE_ERROR:
        msg = g_error_msg[0] ? g_error_msg : "Something went wrong.";
        break;
      case STATE_RESULTS:
        msg = "No matches.";
        break;
      default:
        msg = "Loading…";
        break;
    }
    ui_draw_info_cell(gctx, cell_layer, msg);
    return;
  }

  Result *item = &g_results[idx->row];
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

static void select_row(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) {
    if (g_state == STATE_ERROR && g_query[0]) {
      if (strcmp(g_query, THEATERS_QUERY) == 0) request_theaters();
      else request_search(g_query);
    }
    return;
  }
  Result *item = &g_results[idx->row];
  if (item->id == 0) {
    // An "in theaters" movie - no DDD id yet, so search by title.
    request_search(item->name);
  } else {
    request_media(item->id, item->name);
    topics_window_push();
  }
}

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
  menu_frame.size.h -= ATTRIB_HEIGHT;

  s_menu = menu_layer_create(menu_frame);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_row,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu, GColorJaegerGreen, GColorWhite);
#endif

  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, status_bar_layer_get_layer(s_status));
  s_attrib = ui_add_attribution(window);
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  status_bar_layer_destroy(s_status);
  text_layer_destroy(s_attrib);
  window_destroy(window);
  s_window = NULL;
  s_menu = NULL;
}

void results_window_reload(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

void results_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
