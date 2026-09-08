#include <pebble.h>
#include "app.h"

static Window *s_window;
static ScrollLayer *s_scroll;
static TextLayer *s_title_layer;
static TextLayer *s_verdict_layer;
static TextLayer *s_body_layer;
static StatusBarLayer *s_status;

static char s_title_buf[QUESTION_LEN + 4];
static char s_verdict_buf[48];
static char s_body_buf[COMMENT_LEN + 48];

static void build_text(int topic_idx) {
  if (topic_idx < 0 || topic_idx >= g_topics_count) {
    s_title_buf[0] = s_verdict_buf[0] = s_body_buf[0] = '\0';
    return;
  }
  Topic *t = &g_topics[topic_idx];

  snprintf(s_title_buf, sizeof(s_title_buf), "%s", t->question);

  const char *word = (t->verdict == 'Y') ? "YES"
                   : (t->verdict == 'N') ? "NO" : "UNCLEAR";
  const char *yes = t->yes[0] ? t->yes : "0";
  const char *no  = t->no[0]  ? t->no  : "0";
  snprintf(s_verdict_buf, sizeof(s_verdict_buf), "%s\n%s yes  /  %s no", word, yes, no);

  if (t->comment[0]) {
    snprintf(s_body_buf, sizeof(s_body_buf), "“%s”", t->comment);
  } else {
    snprintf(s_body_buf, sizeof(s_body_buf), "No comment yet.");
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_status = status_bar_layer_create();
  status_bar_layer_set_colors(s_status, GColorClear, GColorBlack);

  GRect content = bounds;
#if !defined(PBL_ROUND)
  content.origin.y += STATUS_BAR_LAYER_HEIGHT;
  content.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif

  s_scroll = scroll_layer_create(content);
  scroll_layer_set_click_config_onto_window(s_scroll, window);
#if defined(PBL_COLOR)
  scroll_layer_set_shadow_hidden(s_scroll, false);
#endif

  const int16_t w = content.size.w;
  const int16_t pad = PBL_IF_ROUND_ELSE(18, 6);
  const int16_t iw = w - 2 * pad;
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);

  s_title_layer = text_layer_create(GRect(pad, 2, iw, 80));
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_title_layer, align);
  text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_title_layer, s_title_buf);
  GSize ts = text_layer_get_content_size(s_title_layer);
  text_layer_set_size(s_title_layer, GSize(iw, ts.h + 4));

  int16_t vy = ts.h + 10;
  s_verdict_layer = text_layer_create(GRect(pad, vy, iw, 60));
  text_layer_set_font(s_verdict_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(s_verdict_layer, align);
  text_layer_set_overflow_mode(s_verdict_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_verdict_layer, s_verdict_buf);
  GSize vs = text_layer_get_content_size(s_verdict_layer);
  text_layer_set_size(s_verdict_layer, GSize(iw, vs.h + 4));

  int16_t by = vy + vs.h + 14;
  s_body_layer = text_layer_create(GRect(pad, by, iw, 2000));
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_body_layer, align);
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_body_layer, s_body_buf);
  GSize bs = text_layer_get_content_size(s_body_layer);
  text_layer_set_size(s_body_layer, GSize(iw, bs.h + 4));

  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_title_layer));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_verdict_layer));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_body_layer));
  scroll_layer_set_content_size(s_scroll, GSize(w, by + bs.h + 16));

  layer_add_child(root, scroll_layer_get_layer(s_scroll));
  layer_add_child(root, status_bar_layer_get_layer(s_status));
}

static void window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_verdict_layer);
  text_layer_destroy(s_body_layer);
  scroll_layer_destroy(s_scroll);
  status_bar_layer_destroy(s_status);
  window_destroy(s_window);
  s_window = NULL;
}

void topic_detail_window_push(int topic_idx) {
  build_text(topic_idx);
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
