/* Pure helpers that turn doesthedogdie.com responses into the delimited strings
 * the watch parses. No Pebble or network APIs in here so it runs under plain
 * node (see test/parse.test.js).
 *
 * Wire format (must match src/c/app.h):
 *   record separator \x1e   field separator \x1f
 *   RESULTS / RECENTS : id \x1f name \x1f year \x1f type      (repeated)
 *   TOPICS            : verdict \x1f question \x1f yes \x1f no \x1f comment
 *   verdict is one char: Y (happens) / N (does not) / ? (inconclusive)
 */

var REC = '\x1e';
var FLD = '\x1f';

var COMMENT_MAX = 118;
var QUESTION_MAX = 42;

function sanitize(str) {
  return String(str == null ? '' : str)
    .replace(/[\x00-\x1f]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function clamp(str, n) {
  str = sanitize(str);
  return str.length > n ? str.slice(0, n - 1).replace(/\s+\S*$/, '') + '…' : str;
}

function stripToInt(v) {
  var n = parseInt(v, 10);
  return isNaN(n) ? 0 : n;
}

// yesSum vs noSum -> 'Y' | 'N' | '?'
function verdictFor(yes, no) {
  yes = stripToInt(yes);
  no = stripToInt(no);
  if (yes === 0 && no === 0) return '?';
  if (yes > no) return 'Y';
  if (no > yes) return 'N';
  return '?';
}

function confidence(yes, no) {
  yes = stripToInt(yes);
  no = stripToInt(no);
  var total = yes + no;
  if (!total) return 0;
  return Math.max(yes, no) / total;
}

// The question form DTDD shows on the site, e.g. "Does the dog die".
function questionOf(topic) {
  topic = topic || {};
  var q = topic.doesName || topic.name || '';
  q = sanitize(q);
  if (!q) return 'Unknown trigger';
  return q.length > QUESTION_MAX ? q.slice(0, QUESTION_MAX - 1) + '…' : q;
}

// search /dddsearch items -> "id \x1f name \x1f year \x1f type" records
function buildResultsPayload(items, max) {
  items = items || [];
  max = max || 12;
  var out = [];
  for (var i = 0; i < items.length && out.length < max; i++) {
    var it = items[i] || {};
    if (!it.id || !it.name) continue;
    var type = (it.itemType && it.itemType.name) || '';
    out.push([
      it.id,
      sanitize(it.name).slice(0, 54),
      sanitize(it.releaseYear || ''),
      sanitize(type),
    ].join(FLD));
  }
  return out.join(REC);
}

// recents [{id,name,year}] -> same record shape, type column shows "Recent"
function buildRecentsPayload(list) {
  list = list || [];
  var out = [];
  for (var i = 0; i < list.length; i++) {
    var r = list[i] || {};
    if (!r.id || !r.name) continue;
    out.push([r.id, sanitize(r.name).slice(0, 54), sanitize(r.year || ''), 'Recent'].join(FLD));
  }
  return out.join(REC);
}

// Sort: things that happen first (highest confidence), then inconclusive, then
// things that don't happen (highest confidence last within their group).
function sortTopics(stats) {
  var rank = { 'Y': 0, '?': 1, 'N': 2 };
  return (stats || []).slice().sort(function (a, b) {
    var va = verdictFor(a.yesSum, a.noSum);
    var vb = verdictFor(b.yesSum, b.noSum);
    if (rank[va] !== rank[vb]) return rank[va] - rank[vb];
    var ca = confidence(a.yesSum, a.noSum);
    var cb = confidence(b.yesSum, b.noSum);
    if (va === 'N') return ca - cb;          // least-certain "no" first
    return cb - ca;                          // most-certain "yes" first
  });
}

// /media topicItemStats -> "verdict \x1f question \x1f yes \x1f no \x1f comment"
//   opts.hideSpoilers   - drop topics flagged isSpoiler
//   opts.hideSensitive  - drop topics flagged isSensitive
//   opts.max            - cap the number of rows
// Returns '' when nothing survives (caller shows "no trigger info").
function buildTopicsPayload(stats, opts) {
  opts = opts || {};
  var max = opts.max || 16;

  var kept = (stats || []).filter(function (s) {
    if (!s || !s.topic) return false;
    if (s.topic.isVisible === false) return false;
    if (opts.hideSpoilers && s.topic.isSpoiler) return false;
    if (opts.hideSensitive && s.topic.isSensitive) return false;
    // Skip topics nobody has voted on at all.
    return stripToInt(s.yesSum) + stripToInt(s.noSum) > 0;
  });

  kept = sortTopics(kept).slice(0, max);

  var out = [];
  for (var i = 0; i < kept.length; i++) {
    var s = kept[i];
    out.push([
      verdictFor(s.yesSum, s.noSum),
      questionOf(s.topic),
      stripToInt(s.yesSum),
      stripToInt(s.noSum),
      clamp(s.comment || '', COMMENT_MAX),
    ].join(FLD));
  }
  return out.join(REC);
}

module.exports = {
  sanitize: sanitize,
  clamp: clamp,
  stripToInt: stripToInt,
  verdictFor: verdictFor,
  confidence: confidence,
  questionOf: questionOf,
  sortTopics: sortTopics,
  buildResultsPayload: buildResultsPayload,
  buildRecentsPayload: buildRecentsPayload,
  buildTopicsPayload: buildTopicsPayload,
};
