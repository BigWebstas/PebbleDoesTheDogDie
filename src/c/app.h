#pragma once
#include <pebble.h>

// Wire format delimiters (must match src/pkjs/parse.js)
#define REC_SEP  '\x1e'   // between records
#define FLD_SEP  '\x1f'   // between fields in a record

// g_query sentinel while the results screen holds the "in theaters" movie list
// (rather than DDD search hits); used for the error-retry path.
#define THEATERS_QUERY "In theaters"

#define MAX_RESULTS       12   // search hits shown on the watch
#define MAX_RECENTS       8    // recent titles on the home screen
#define MAX_TOPICS        16   // triggers shown for one title
#define MAX_CATS          40   // trigger categories in the browser
#define MAX_BROWSE_TOPICS 48   // triggers listed under one category

#define TITLE_LEN     56   // media title
#define YEAR_LEN      8
#define TYPE_LEN      16   // "Movie", "TV Show", ...
#define QUESTION_LEN  44   // "Does the dog die"
#define COUNT_LEN     8    // vote count as text, e.g. "1336"
#define COMMENT_LEN   122  // top comment, clamped on the phone
#define CAT_NAME_LEN  40   // "Animal Injury or Death"
#define DEF_LEN       320  // a trigger's description, clamped on the phone

typedef struct {
  int  id;                 // doesthedogdie.com media id
  char name[TITLE_LEN];
  char year[YEAR_LEN];     // may be empty
  char type[TYPE_LEN];     // may be empty
} Result;

typedef struct {
  char verdict;            // 'Y' happens, 'N' doesn't, '?' inconclusive
  char question[QUESTION_LEN];
  char yes[COUNT_LEN];
  char no[COUNT_LEN];
  char comment[COMMENT_LEN];  // may be empty
  bool starred;              // user pinned this trigger in the browser
} Topic;

typedef struct {
  int  id;
  char name[CAT_NAME_LEN];
} Cat;

typedef struct {
  int  id;                    // doesthedogdie.com topic id
  char question[QUESTION_LEN]; // doesName, e.g. "Does the dog die"
  bool starred;
} BrowseTopic;

typedef enum {
  STATE_START,             // home screen, nothing requested yet
  STATE_LOADING_SEARCH,
  STATE_RESULTS,
  STATE_LOADING_TOPICS,
  STATE_TOPICS,
  STATE_LOADING_BROWSE,
  STATE_BROWSE,
  STATE_ERROR,
} AppState;

// --- global data store (defined in main.c) ---
extern Result g_recents[MAX_RECENTS];
extern int    g_recents_count;
extern bool   g_recents_loaded;

extern Result g_results[MAX_RESULTS];
extern int    g_results_count;
extern char   g_query[64];        // last dictated / typed search text

extern Topic  g_topics[MAX_TOPICS];
extern int    g_topics_count;
extern int    g_media_id;
extern char   g_media_name[TITLE_LEN];

// trigger browser
extern Cat         g_cats[MAX_CATS];
extern int         g_cat_count;
extern BrowseTopic g_browse_topics[MAX_BROWSE_TOPICS];
extern int         g_browse_topic_count;
extern int         g_browse_cat_id;             // 0 = showing the category list
extern char        g_browse_cat_name[CAT_NAME_LEN];
extern int         g_def_topic_id;              // topic whose definition is open
extern char        g_def_does[QUESTION_LEN];
extern char        g_def_not[QUESTION_LEN];
extern char        g_def_body[DEF_LEN];
extern bool        g_def_ready;

extern AppState g_state;
extern char     g_error_msg[128];  // doubles as the "loading..." status line

// --- actions (main.c) ---
// force = bypass the phone-side response cache and hit the API fresh.
void request_search(const char *query);
void request_media(int media_id, const char *name);
void request_recents(void);
void request_theaters(void);   // GPS -> nearby cinemas -> today's movies
void request_browse_cats(void);
void request_browse_topics(int cat_id, const char *cat_name);
void request_browse_topic(int topic_id);   // fetch one trigger's definition
void request_star(int topic_id, bool on);
void dictation_start(void);   // no-op where there is no microphone

// --- windows ---
void search_window_push(void);
void search_window_reload(void);

void results_window_push(void);
void results_window_reload(void);

void topics_window_push(void);
void topics_window_reload(void);

void topic_detail_window_push(int topic_idx);   // verdict mode (from a title)
void topic_detail_window_push_def(void);        // definition mode (from the browser)
void topic_detail_window_reload(void);

void browse_window_push(void);
void browse_window_reload(void);
