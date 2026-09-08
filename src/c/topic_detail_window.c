#include <pebble.h>
#include "app.h"
#include "ui.h"

// Two modes share this ScrollLayer:
//   MODE_VERDICT - a trigger's yes/no result for one title (from topics_window)
//   MODE_DEF     - a trigger's definition, from the browser; SELECT stars it
typedef enum { MODE_VERDICT, MODE_DEF } DetailMode;

static Window *s_window;
static ScrollLayer *s_scroll;
static TextLayer *s_title_layer;
static TextLayer *s_sub_layer;
static TextLayer *s_body_layer;
static StatusBarLayer *s_status;
static TextLayer *s_attrib;

static DetailMode s_mode;
static bool s_def_starred;

static char s_title_buf[QUESTION_LEN + 8];
static char s_sub_buf[64];
static char s_body_buf[DEF_LEN + 80];

// --- content -------------------------------------------------------------

static void build_buffers(void) {
  if (s_mode == MODE_VERDICT) {
    // s_title_buf / s_sub_buf / s_body_buf were filled by _push().
    return;
  }

  // MODE_DEF
  if (!g_def_ready) {
    snprintf(s_title_buf, sizeof(s_title_buf), "%s", g_def_does[0] ? g_def_does : "Trigger");
    s_sub_buf[0] = '\0';
    snprintf(s_body_buf, sizeof(s_body_buf), "%s",
             (g_state == STATE_ERROR && g_error_msg[0]) ? g_error_msg : "Loading…");
    return;
  }
  snprintf(s_title_buf, sizeof(s_title_buf), "%s", g_def_does);
  snprintf(s_sub_buf, sizeof(s_sub_buf), "%s", g_def_not[0] ? g_def_not : "");
  snprintf(s_body_buf, sizeof(s_body_buf), "%s\n\n%s",
           g_def_body[0] ? g_def_body : "No description.",
           s_def_starred ? "★ Starred — press select to unstar"
                         : "Press select to star this trigger");
}

static void build_content(void) {
  build_buffers();

  Layer *root = window_get_root_layer(s_window);
  GRect b = layer_get_bounds(root);
  GRect content = b;
#if !defined(PBL_ROUND)
  content.origin.y += STATUS_BAR_LAYER_HEIGHT;
  content.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif
  content.size.h -= ATTRIB_HEIGHT;

  const int16_t w = content.size.w;
  const int16_t pad = PBL_IF_ROUND_ELSE(18, 6);
  const int16_t iw = w - 2 * pad;
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);

  s_title_layer = text_layer_create(GRect(pad, 2, iw, 90));
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_title_layer, align);
  text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_title_layer, s_title_buf);
  GSize ts = text_layer_get_content_size(s_title_layer);
  text_layer_set_size(s_title_layer, GSize(iw, ts.h + 4));

  int16_t sy = ts.h + 8;
  s_sub_layer = text_layer_create(GRect(pad, sy, iw, 70));
  text_layer_set_font(s_sub_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(s_sub_layer, align);
  text_layer_set_overflow_mode(s_sub_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_sub_layer, s_sub_buf);
  GSize ss = s_sub_buf[0] ? text_layer_get_content_size(s_sub_layer) : GSize(iw, 0);
  text_layer_set_size(s_sub_layer, GSize(iw, ss.h + 4));

  int16_t by = sy + ss.h + (s_sub_buf[0] ? 12 : 4);
  s_body_layer = text_layer_create(GRect(pad, by, iw, 3000));
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_body_layer, align);
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_body_layer, s_body_buf);
  GSize bs = text_layer_get_content_size(s_body_layer);
  text_layer_set_size(s_body_layer, GSize(iw, bs.h + 4));

  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_title_layer));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_sub_layer));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_body_layer));
  scroll_layer_set_content_size(s_scroll, GSize(w, by + bs.h + 16));
  scroll_layer_set_content_offset(s_scroll, GPoint(0, 0), false);
}

static void teardown_content(void) {
  if (s_title_layer) { text_layer_destroy(s_title_layer); s_title_layer = NULL; }
  if (s_sub_layer)   { text_layer_destroy(s_sub_layer);   s_sub_layer = NULL; }
  if (s_body_layer)  { text_layer_destroy(s_body_layer);  s_body_layer = NULL; }
}

// --- star toggle (MODE_DEF) --------------------------------------------

static void def_select(ClickRecognizerRef rec, void *context) {
  if (s_mode != MODE_DEF || !g_def_ready) return;
  s_def_starred = !s_def_starred;
  request_star(g_def_topic_id, s_def_starred);
  // keep the browse list row in sync
  for (int i = 0; i < g_browse_topic_count; i++) {
    if (g_browse_topics[i].id == g_def_topic_id) {
      g_browse_topics[i].starred = s_def_starred;
      break;
    }
  }
  vibes_short_pulse();
  browse_window_reload();
  teardown_content();
  build_content();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, def_select);
}

// --- lifecycle ---------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_status = status_bar_layer_create();
  status_bar_layer_set_colors(s_status, GColorClear, GColorBlack);

  GRect content = b;
#if !defined(PBL_ROUND)
  content.origin.y += STATUS_BAR_LAYER_HEIGHT;
  content.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif
  content.size.h -= ATTRIB_HEIGHT;

  s_scroll = scroll_layer_create(content);
  if (s_mode == MODE_DEF) {
    scroll_layer_set_callbacks(s_scroll, (ScrollLayerCallbacks) { .click_config_provider = click_config });
  }
  scroll_layer_set_click_config_onto_window(s_scroll, window);
#if defined(PBL_COLOR)
  scroll_layer_set_shadow_hidden(s_scroll, false);
#endif

  build_content();

  layer_add_child(root, scroll_layer_get_layer(s_scroll));
  layer_add_child(root, status_bar_layer_get_layer(s_status));
  s_attrib = ui_add_attribution(window);
}

static void window_unload(Window *window) {
  teardown_content();
  scroll_layer_destroy(s_scroll);
  status_bar_layer_destroy(s_status);
  text_layer_destroy(s_attrib);
  window_destroy(s_window);
  s_window = NULL;
  s_scroll = NULL;
}

void topic_detail_window_reload(void) {
  if (s_window && s_mode == MODE_DEF) {
    teardown_content();
    build_content();
  }
}

// --- entry points -----------------------------------------------------

void topic_detail_window_push(int topic_idx) {
  s_mode = MODE_VERDICT;
  if (topic_idx < 0 || topic_idx >= g_topics_count) {
    s_title_buf[0] = s_sub_buf[0] = s_body_buf[0] = '\0';
  } else {
    Topic *t = &g_topics[topic_idx];
    snprintf(s_title_buf, sizeof(s_title_buf), "%s", t->question);
    const char *word = (t->verdict == 'Y') ? "YES" : (t->verdict == 'N') ? "NO" : "UNCLEAR";
    snprintf(s_sub_buf, sizeof(s_sub_buf), "%s   %s yes / %s no",
             word, t->yes[0] ? t->yes : "0", t->no[0] ? t->no : "0");
    if (t->comment[0]) snprintf(s_body_buf, sizeof(s_body_buf), "“%s”", t->comment);
    else               snprintf(s_body_buf, sizeof(s_body_buf), "No comment yet.");
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

void topic_detail_window_push_def(void) {
  s_mode = MODE_DEF;
  s_def_starred = false;
  for (int i = 0; i < g_browse_topic_count; i++) {
    if (g_browse_topics[i].id == g_def_topic_id) { s_def_starred = g_browse_topics[i].starred; break; }
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
