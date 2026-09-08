#include <pebble.h>
#include "app.h"

// ---------------------------------------------------------------------------
// Global data store
// ---------------------------------------------------------------------------
Result g_recents[MAX_RECENTS];
int    g_recents_count = 0;
bool   g_recents_loaded = false;

Result g_results[MAX_RESULTS];
int    g_results_count = 0;
char   g_query[64] = {0};

Topic  g_topics[MAX_TOPICS];
int    g_topics_count = 0;
int    g_media_id = 0;
char   g_media_name[TITLE_LEN] = {0};

Cat         g_cats[MAX_CATS];
int         g_cat_count = 0;
BrowseTopic g_browse_topics[MAX_BROWSE_TOPICS];
int         g_browse_topic_count = 0;
int         g_browse_cat_id = 0;
char        g_browse_cat_name[CAT_NAME_LEN] = {0};
int         g_def_topic_id = 0;
char        g_def_does[QUESTION_LEN] = {0};
char        g_def_not[QUESTION_LEN] = {0};
char        g_def_body[DEF_LEN] = {0};
bool        g_def_ready = false;

AppState g_state = STATE_START;
char     g_error_msg[128] = {0};

static void watchdog_start(void);
static void watchdog_stop(void);

// ---------------------------------------------------------------------------
// Small parsing helpers (shared shape with the other Pebble apps in this repo)
// ---------------------------------------------------------------------------

static void copy_field(char *dst, size_t dst_len, const char *start, const char *end) {
  size_t n = (size_t)(end - start);
  if (n >= dst_len) n = dst_len - 1;
  memcpy(dst, start, n);
  dst[n] = '\0';
}

static bool next_field(const char **cursor, const char *record_end,
                       const char **out_start, const char **out_end) {
  if (*cursor > record_end) return false;
  const char *s = *cursor;
  const char *p = s;
  while (p < record_end && *p != FLD_SEP) p++;
  *out_start = s;
  *out_end = p;
  *cursor = (p < record_end) ? p + 1 : record_end + 1;
  return true;
}

// Walk "field \x1f field \x1e field \x1f field" records, calling `handle` for
// each with a cursor positioned at the record start and the record end.
typedef void (*RecordFn)(const char *rec_start, const char *rec_end, int index);

static int parse_records(const char *payload, int max, RecordFn handle) {
  int count = 0;
  const char *p = payload;
  const char *end = payload + strlen(payload);

  while (p < end && count < max) {
    const char *rec_end = p;
    while (rec_end < end && *rec_end != REC_SEP) rec_end++;
    if (rec_end > p) {
      handle(p, rec_end, count);
      count++;
    }
    p = (rec_end < end) ? rec_end + 1 : end;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Record handlers
// ---------------------------------------------------------------------------

static void fill_result(Result *r, const char *rec_start, const char *rec_end) {
  r->id = 0;
  r->name[0] = r->year[0] = r->type[0] = '\0';

  const char *cur = rec_start;
  const char *fs, *fe;
  char idbuf[12] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(idbuf, sizeof(idbuf), fs, fe);
  r->id = atoi(idbuf);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(r->name, TITLE_LEN, fs, fe);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(r->year, YEAR_LEN, fs, fe);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(r->type, TYPE_LEN, fs, fe);
}

static void handle_recent(const char *rec_start, const char *rec_end, int index) {
  fill_result(&g_recents[index], rec_start, rec_end);
}

static void handle_result(const char *rec_start, const char *rec_end, int index) {
  fill_result(&g_results[index], rec_start, rec_end);
}

static void handle_topic(const char *rec_start, const char *rec_end, int index) {
  Topic *t = &g_topics[index];
  t->verdict = '?';
  t->question[0] = t->yes[0] = t->no[0] = t->comment[0] = '\0';
  t->starred = false;

  const char *cur = rec_start;
  const char *fs, *fe;
  char vbuf[4] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) {
    copy_field(vbuf, sizeof(vbuf), fs, fe);
    if (vbuf[0]) t->verdict = vbuf[0];
  }
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(t->question, QUESTION_LEN, fs, fe);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(t->yes, COUNT_LEN, fs, fe);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(t->no, COUNT_LEN, fs, fe);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(t->comment, COMMENT_LEN, fs, fe);
  char sbuf[4] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(sbuf, sizeof(sbuf), fs, fe);
  t->starred = (sbuf[0] == '1');
}

static void handle_cat(const char *rec_start, const char *rec_end, int index) {
  Cat *c = &g_cats[index];
  c->id = 0;
  c->name[0] = '\0';
  const char *cur = rec_start;
  const char *fs, *fe;
  char idbuf[12] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(idbuf, sizeof(idbuf), fs, fe);
  c->id = atoi(idbuf);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(c->name, CAT_NAME_LEN, fs, fe);
}

static void handle_browse_topic(const char *rec_start, const char *rec_end, int index) {
  BrowseTopic *b = &g_browse_topics[index];
  b->id = 0;
  b->question[0] = '\0';
  b->starred = false;
  const char *cur = rec_start;
  const char *fs, *fe;
  char idbuf[12] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(idbuf, sizeof(idbuf), fs, fe);
  b->id = atoi(idbuf);
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(b->question, QUESTION_LEN, fs, fe);
  char sbuf[4] = {0};
  if (next_field(&cur, rec_end, &fs, &fe)) copy_field(sbuf, sizeof(sbuf), fs, fe);
  b->starred = (sbuf[0] == '1');
}

// ---------------------------------------------------------------------------
// AppMessage: inbound
// ---------------------------------------------------------------------------

static void reload_all(void) {
  search_window_reload();
  results_window_reload();
  topics_window_reload();
  browse_window_reload();
  topic_detail_window_reload();
}

static void set_status(const char *msg) {
  strncpy(g_error_msg, msg, sizeof(g_error_msg) - 1);
  g_error_msg[sizeof(g_error_msg) - 1] = '\0';
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *err = dict_find(iter, MESSAGE_KEY_ERROR);
  if (err && err->type == TUPLE_CSTRING && err->length > 1) {
    watchdog_stop();
    g_state = STATE_ERROR;
    set_status(err->value->cstring);
    reload_all();
    return;
  }

  Tuple *status = dict_find(iter, MESSAGE_KEY_STATUS);
  if (status && status->type == TUPLE_CSTRING) {
    // The phone is alive and working - give it a fresh timeout window.
    watchdog_start();
    set_status(status->value->cstring);
    reload_all();
  }

  Tuple *recents = dict_find(iter, MESSAGE_KEY_RECENTS);
  if (recents && recents->type == TUPLE_CSTRING) {
    g_recents_count = parse_records(recents->value->cstring, MAX_RECENTS, handle_recent);
    g_recents_loaded = true;
    search_window_reload();
  }

  Tuple *results = dict_find(iter, MESSAGE_KEY_RESULTS);
  if (results && results->type == TUPLE_CSTRING) {
    watchdog_stop();
    g_results_count = parse_records(results->value->cstring, MAX_RESULTS, handle_result);
    g_state = STATE_RESULTS;
    g_error_msg[0] = '\0';
    results_window_reload();
  }

  Tuple *topics = dict_find(iter, MESSAGE_KEY_TOPICS);
  if (topics && topics->type == TUPLE_CSTRING) {
    watchdog_stop();
    g_topics_count = parse_records(topics->value->cstring, MAX_TOPICS, handle_topic);
    g_state = STATE_TOPICS;
    g_error_msg[0] = '\0';
    topics_window_reload();
  }

  Tuple *cats = dict_find(iter, MESSAGE_KEY_CATS);
  if (cats && cats->type == TUPLE_CSTRING) {
    watchdog_stop();
    g_cat_count = parse_records(cats->value->cstring, MAX_CATS, handle_cat);
    g_state = STATE_BROWSE;
    g_error_msg[0] = '\0';
    browse_window_reload();
  }

  Tuple *btopics = dict_find(iter, MESSAGE_KEY_BROWSE_TOPICS);
  if (btopics && btopics->type == TUPLE_CSTRING) {
    watchdog_stop();
    g_browse_topic_count = parse_records(btopics->value->cstring, MAX_BROWSE_TOPICS,
                                         handle_browse_topic);
    g_state = STATE_BROWSE;
    g_error_msg[0] = '\0';
    browse_window_reload();
  }

  Tuple *tdet = dict_find(iter, MESSAGE_KEY_TOPIC_DETAIL);
  if (tdet && tdet->type == TUPLE_CSTRING) {
    watchdog_stop();
    const char *p = tdet->value->cstring;
    const char *end = p + strlen(p);
    const char *cur = p;
    const char *fs, *fe;
    g_def_does[0] = g_def_not[0] = g_def_body[0] = '\0';
    if (next_field(&cur, end, &fs, &fe)) copy_field(g_def_does, QUESTION_LEN, fs, fe);
    if (next_field(&cur, end, &fs, &fe)) copy_field(g_def_not, QUESTION_LEN, fs, fe);
    if (next_field(&cur, end, &fs, &fe)) copy_field(g_def_body, DEF_LEN, fs, fe);
    g_def_ready = true;
    g_state = STATE_BROWSE;
    topic_detail_window_reload();
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
  g_state = STATE_ERROR;
  snprintf(g_error_msg, sizeof(g_error_msg), "Message dropped.\nTry again.");
  reload_all();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Watchdog: surface an error instead of a stuck loading screen.
// ---------------------------------------------------------------------------
#define LOAD_TIMEOUT_MS 45000
static AppTimer *s_watchdog = NULL;

static void watchdog_fire(void *ctx) {
  s_watchdog = NULL;
  if (g_state == STATE_LOADING_SEARCH || g_state == STATE_LOADING_TOPICS ||
      g_state == STATE_LOADING_BROWSE) {
    g_state = STATE_ERROR;
    snprintf(g_error_msg, sizeof(g_error_msg), "No response from phone.\nTry again.");
    reload_all();
  }
}

static void watchdog_start(void) {
  if (s_watchdog) app_timer_cancel(s_watchdog);
  s_watchdog = app_timer_register(LOAD_TIMEOUT_MS, watchdog_fire, NULL);
}

static void watchdog_stop(void) {
  if (s_watchdog) { app_timer_cancel(s_watchdog); s_watchdog = NULL; }
}

// ---------------------------------------------------------------------------
// Actions (watch -> phone)
// ---------------------------------------------------------------------------

void request_search(const char *query) {
  strncpy(g_query, query, sizeof(g_query) - 1);
  g_query[sizeof(g_query) - 1] = '\0';
  g_state = STATE_LOADING_SEARCH;
  g_error_msg[0] = '\0';
  g_results_count = 0;
  results_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "search");
  dict_write_cstring(out, MESSAGE_KEY_QUERY, g_query);
  dict_write_int32(out, MESSAGE_KEY_FORCE, 0);
  app_message_outbox_send();
  watchdog_start();
}

void request_media(int media_id, const char *name) {
  g_media_id = media_id;
  strncpy(g_media_name, name ? name : "", sizeof(g_media_name) - 1);
  g_media_name[sizeof(g_media_name) - 1] = '\0';
  g_state = STATE_LOADING_TOPICS;
  g_error_msg[0] = '\0';
  g_topics_count = 0;
  topics_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "media");
  dict_write_int32(out, MESSAGE_KEY_MEDIA_ID, media_id);
  dict_write_int32(out, MESSAGE_KEY_FORCE, 0);
  app_message_outbox_send();
  watchdog_start();
}

void request_recents(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "recents");
  app_message_outbox_send();
}

// Ask the phone for the movies playing at nearby cinemas. The reply is a
// normal RESULTS message whose records carry id 0 - picking one runs a
// title search (see results_window.c).
void request_theaters(void) {
  strncpy(g_query, THEATERS_QUERY, sizeof(g_query) - 1);
  g_query[sizeof(g_query) - 1] = '\0';
  g_state = STATE_LOADING_SEARCH;
  g_error_msg[0] = '\0';
  g_results_count = 0;
  results_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "theaters");
  dict_write_int32(out, MESSAGE_KEY_FORCE, 0);
  app_message_outbox_send();
  watchdog_start();
}

// ---------------------------------------------------------------------------
// Trigger browser
// ---------------------------------------------------------------------------

void request_browse_cats(void) {
  g_browse_cat_id = 0;
  g_browse_cat_name[0] = '\0';
  g_state = STATE_LOADING_BROWSE;
  g_error_msg[0] = '\0';
  g_cat_count = 0;
  browse_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "browse_cats");
  app_message_outbox_send();
  watchdog_start();
}

void request_browse_topics(int cat_id, const char *cat_name) {
  g_browse_cat_id = cat_id;
  strncpy(g_browse_cat_name, cat_name ? cat_name : "", CAT_NAME_LEN - 1);
  g_browse_cat_name[CAT_NAME_LEN - 1] = '\0';
  g_state = STATE_LOADING_BROWSE;
  g_error_msg[0] = '\0';
  g_browse_topic_count = 0;
  browse_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "browse_topics");
  dict_write_int32(out, MESSAGE_KEY_PARENT_ID, cat_id);
  app_message_outbox_send();
  watchdog_start();
}

void request_browse_topic(int topic_id) {
  g_def_topic_id = topic_id;
  g_def_ready = false;
  g_state = STATE_LOADING_BROWSE;
  g_error_msg[0] = '\0';
  topic_detail_window_reload();

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "browse_topic");
  dict_write_int32(out, MESSAGE_KEY_TOPIC_ID, topic_id);
  app_message_outbox_send();
  watchdog_start();
}

// Fire-and-forget: the watch flips its own star flag immediately, the phone
// just persists the change.
void request_star(int topic_id, bool on) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_cstring(out, MESSAGE_KEY_REQUEST, "star");
  dict_write_int32(out, MESSAGE_KEY_TOPIC_ID, topic_id);
  dict_write_int32(out, MESSAGE_KEY_STAR, on ? 1 : 0);
  app_message_outbox_send();
}

// ---------------------------------------------------------------------------
// Dictation (voice search). Compiled to a no-op where there is no microphone.
// ---------------------------------------------------------------------------
#if defined(PBL_MICROPHONE)
static DictationSession *s_dictation = NULL;
static char s_dictation_buf[200];

static void dictation_cb(DictationSession *session, DictationSessionStatus status,
                         char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription && transcription[0]) {
    results_window_push();
    request_search(transcription);
  }
}

void dictation_start(void) {
  if (s_dictation) dictation_session_start(s_dictation);
}

static void dictation_init(void) {
  s_dictation = dictation_session_create(sizeof(s_dictation_buf), dictation_cb, NULL);
  if (s_dictation) dictation_session_enable_confirmation(s_dictation, true);
}

static void dictation_deinit(void) {
  if (s_dictation) { dictation_session_destroy(s_dictation); s_dictation = NULL; }
}
#else
void dictation_start(void) {}
static void dictation_init(void) {}
static void dictation_deinit(void) {}
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  // Generous inbox: a whole trigger list arrives as one string.
  app_message_open(4096, 256);

  dictation_init();

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  app_touch_navigation_enable(true);
#endif

  search_window_push();
}

static void deinit(void) {
  dictation_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
