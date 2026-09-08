#include <pebble.h>
#include "app.h"
#include "ui.h"

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status;
static TextLayer *s_attrib;

static bool is_list_ready(void) {
  return g_state == STATE_TOPICS && g_topics_count > 0;
}

static const char *verdict_word(char v) {
  switch (v) {
    case 'Y': return "Yes";
    case 'N': return "No";
    default:  return "Unclear";
  }
}

static uint16_t get_num_sections(struct MenuLayer *menu, void *ctx) {
  return 1;
}

static int16_t get_header_height(struct MenuLayer *menu, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *gctx, const Layer *cell_layer, uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gctx, cell_layer, g_media_name[0] ? g_media_name : "Triggers");
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return is_list_ready() ? g_topics_count : 1;
}

static int16_t get_cell_height(struct MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) return INFO_CELL_HEIGHT;
#if defined(PBL_ROUND)
  return menu_layer_is_index_selected(menu, idx) ? 66 : 54;
#else
  return 48;
#endif
}

static void draw_row(GContext *gctx, const Layer *cell_layer, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) {
    const char *msg;
    switch (g_state) {
      case STATE_LOADING_TOPICS:
        msg = g_error_msg[0] ? g_error_msg : "Checking…";
        break;
      case STATE_ERROR:
        msg = g_error_msg[0] ? g_error_msg : "Something went wrong.";
        break;
      case STATE_TOPICS:
        msg = "No trigger info yet.";
        break;
      default:
        msg = "Loading…";
        break;
    }
    ui_draw_info_cell(gctx, cell_layer, msg);
    return;
  }

  Topic *t = &g_topics[idx->row];

#if defined(PBL_COLOR)
  if (!menu_cell_layer_is_highlighted(cell_layer) && (t->verdict == 'Y' || t->verdict == 'N')) {
    graphics_context_set_fill_color(gctx, t->verdict == 'Y' ? GColorMelon : GColorMintGreen);
    graphics_fill_rect(gctx, layer_get_bounds(cell_layer), 0, GCornerNone);
    graphics_context_set_text_color(gctx, GColorBlack);
  }
#endif

  char subtitle[40];
  if (t->yes[0] || t->no[0]) {
    snprintf(subtitle, sizeof(subtitle), "%s  ·  %s yes / %s no",
             verdict_word(t->verdict),
             t->yes[0] ? t->yes : "0", t->no[0] ? t->no : "0");
  } else {
    snprintf(subtitle, sizeof(subtitle), "%s", verdict_word(t->verdict));
  }

  char title[QUESTION_LEN + 4];
  if (t->starred) snprintf(title, sizeof(title), "★ %s", t->question);
  else           snprintf(title, sizeof(title), "%s", t->question);

  menu_cell_basic_draw(gctx, cell_layer, title, subtitle, NULL);
}

static void select_row(MenuLayer *menu, MenuIndex *idx, void *ctx) {
  if (!is_list_ready()) {
    if (g_state == STATE_ERROR && g_media_id) request_media(g_media_id, g_media_name);
    return;
  }
  topic_detail_window_push(idx->row);
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

void topics_window_reload(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

void topics_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
