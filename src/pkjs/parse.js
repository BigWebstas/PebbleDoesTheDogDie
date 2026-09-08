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

// Sort: starred triggers first; then things that happen (highest confidence),
// then inconclusive, then things that don't happen.
function sortTopics(stats) {
  var rank = { 'Y': 0, '?': 1, 'N': 2 };
  return (stats || []).slice().sort(function (a, b) {
    if (!!a._starred !== !!b._starred) return a._starred ? -1 : 1;
    var va = verdictFor(a.yesSum, a.noSum);
    var vb = verdictFor(b.yesSum, b.noSum);
    if (rank[va] !== rank[vb]) return rank[va] - rank[vb];
    var ca = confidence(a.yesSum, a.noSum);
    var cb = confidence(b.yesSum, b.noSum);
    if (va === 'N') return ca - cb;          // least-certain "no" first
    return cb - ca;                          // most-certain "yes" first
  });
}

function topicIdOf(s) {
  return (s.topic && s.topic.id) || s.TopicId || s.topicId || 0;
}

// /media topicItemStats -> "verdict \x1f question \x1f yes \x1f no \x1f comment \x1f starred"
//   opts.hideSpoilers   - drop topics flagged isSpoiler
//   opts.hideSensitive  - drop topics flagged isSensitive
//   opts.starred        - array of starred topic ids (pinned to the top, marked ★)
//   opts.max            - cap the number of rows
// Returns '' when nothing survives (caller shows "no trigger info").
function buildTopicsPayload(stats, opts) {
  opts = opts || {};
  var max = opts.max || 16;
  var starred = opts.starred || [];

  var kept = (stats || []).filter(function (s) {
    if (!s || !s.topic) return false;
    if (s.topic.isVisible === false) return false;
    if (opts.hideSpoilers && s.topic.isSpoiler) return false;
    if (opts.hideSensitive && s.topic.isSensitive) return false;
    // Skip topics nobody has voted on at all.
    return stripToInt(s.yesSum) + stripToInt(s.noSum) > 0;
  });

  kept.forEach(function (s) { s._starred = starred.indexOf(topicIdOf(s)) >= 0; });
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
      s._starred ? '1' : '0',
    ].join(FLD));
  }
  return out.join(REC);
}

// ---------------------------------------------------------------------------
// Trigger browser (v3 /api/v3/topics + /api/v3/topiccategories)
// ---------------------------------------------------------------------------

// [{id,name}] -> "id \x1f name" records, sorted by name
function buildCatsPayload(cats) {
  return (cats || []).slice()
    .sort(function (a, b) { return String(a.name).localeCompare(String(b.name)); })
    .map(function (c) { return [c.id, sanitize(c.name).slice(0, 38)].join(FLD); })
    .join(REC);
}

// topics for one category -> "topicId \x1f doesName \x1f starred" records
function buildBrowseTopicsPayload(topics, starredSet, max) {
  max = max || 48;
  var set = {};
  (starredSet || []).forEach(function (id) { set[id] = true; });
  return (topics || []).slice()
    .sort(function (a, b) {
      return String(a.doesName || a.name).localeCompare(String(b.doesName || b.name));
    })
    .slice(0, max)
    .map(function (t) {
      var q = sanitize(t.doesName || t.name || '').slice(0, 42);
      return [t.id, q, set[t.id] ? '1' : '0'].join(FLD);
    })
    .join(REC);
}

// one topic -> "doesName \x1f notName \x1f description"
function topicDetailPayload(topic) {
  topic = topic || {};
  return [
    sanitize(topic.doesName || topic.name || 'Trigger').slice(0, 42),
    sanitize(topic.notName || '').slice(0, 42),
    clamp(topic.description || '', 300),
  ].join(FLD);
}

// Which category ids a topic belongs to (primary + alt).
function topicCatIds(t) {
  var ids = [];
  if (t.topicCategoryId != null) ids.push(t.topicCategoryId);
  if (t.altTopicCategoryId != null && t.altTopicCategoryId !== t.topicCategoryId) {
    ids.push(t.altTopicCategoryId);
  }
  return ids;
}

// ---------------------------------------------------------------------------
// "In theaters near me": GPS -> SerpApi google_maps cinemas -> google showtimes.
// The cinema filter and geo helpers are copied from PebbleMovieTimes/parse.js -
// looksLikeCinema in particular is load-bearing (SerpApi returns A/V installers
// and playhouses for "movie theater").
// ---------------------------------------------------------------------------

function haversineKm(lat1, lon1, lat2, lon2) {
  var R = 6371;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

var CINEMA_NAME_RE = /\b(cinemas?|cineplex|multiplex|megaplex|movie theat(re|er))\b/i;
var CHAIN_RE = /\b(amc|regal|cinemark|cinepolis|cin[eé]polis|megaplex|marcus|harkins|showcase|odeon|vue|picturehouse|alamo drafthouse|landmark|ipic|studio movie grill|emagine|maya cinemas|bow tie|reading cinemas|cmx|malco|santikos|violet crown|fat ?cats|look dine-in)\b/i;
var NOT_CINEMA_RE = /amphitheat|performing arts|concert|live music|playhouse|opera|symphony|orchestra|philharmon|ballet|stadium|\barena\b|fairground|convention center|community theat|dinner theat|repertory|shakespear|children'?s theat|black box|little theat|\btheat(re|er) (compan|troupe|guild)|\bplayers\b|civic (center|theat)|manufacturer|production (service|compan)|installation|home cinema|audio.?visual|integrator|\bequipment\b|\brental\b|contractor/i;
var CINEMA_TYPE_RE = /^movie theater$|drive-?in theater/i;

function looksLikeCinema(name, type) {
  name = String(name || '').trim();
  type = String(type || '').trim();
  if (name.length < 4) return false;
  if (NOT_CINEMA_RE.test(name) || NOT_CINEMA_RE.test(type)) return false;
  if (CINEMA_TYPE_RE.test(type)) return true;
  if (type) return CHAIN_RE.test(name);
  return CINEMA_NAME_RE.test(name) || CHAIN_RE.test(name);
}

// SerpApi google_maps data -> [{ name, lat, lon }]
function extractTheaters(data) {
  var raw = [];
  if (data && data.local_results && data.local_results.length) raw = data.local_results;
  else if (data && data.place_results) raw = [data.place_results];

  var list = [];
  for (var i = 0; i < raw.length; i++) {
    var r = raw[i];
    if (!r || !r.title) continue;
    var type = r.type || (r.types && r.types.join(' ')) || '';
    if (!looksLikeCinema(r.title, type)) continue;
    var g = r.gps_coordinates || {};
    list.push({
      name: r.title,
      lat: (g.latitude != null) ? g.latitude : null,
      lon: (g.longitude != null) ? g.longitude : null,
    });
  }
  return list;
}

// SerpApi google (showtimes box) data -> ["Movie name", ...] for today (or the
// first day listed). Titles only - we don't need the showtimes themselves.
function extractMovieTitles(data) {
  var blocks = (data && data.showtimes) ||
    (data && data.answer_box && data.answer_box.showtimes) ||
    (data && data.knowledge_graph && data.knowledge_graph.showtimes) || null;
  if (!blocks || !blocks.length) return [];

  var block = null;
  for (var i = 0; i < blocks.length; i++) {
    if (blocks[i].movies && blocks[i].movies.length) {
      if (!block) block = blocks[i];
      if (/today/i.test(blocks[i].day || '')) { block = blocks[i]; break; }
    }
  }
  if (!block) return [];

  var out = [];
  for (var m = 0; m < block.movies.length; m++) {
    var name = block.movies[m] && block.movies[m].name;
    if (name) out.push(sanitize(name));
  }
  return out;
}

// Merge arrays of title strings, case-insensitive de-dupe, keep first-seen
// order, cap the count.
function dedupeTitles(lists, max) {
  max = max || 12;
  var seen = {};
  var out = [];
  for (var i = 0; i < (lists || []).length; i++) {
    var arr = lists[i] || [];
    for (var j = 0; j < arr.length && out.length < max; j++) {
      var title = sanitize(arr[j]);
      var key = title.toLowerCase();
      if (!title || seen[key]) continue;
      seen[key] = true;
      out.push(title);
    }
  }
  return out;
}

// [title, ...] -> "0 \x1f title \x1f "" \x1f "In theaters"" records. id 0 tells
// the watch to reach the title via a search rather than a media fetch.
function buildTheatersPayload(titles) {
  var out = [];
  for (var i = 0; i < (titles || []).length; i++) {
    var t = sanitize(titles[i]).slice(0, 54);
    if (t) out.push(['0', t, '', 'In theaters'].join(FLD));
  }
  return out.join(REC);
}

// BigDataCloud reverse geocode -> "City, Region, Country" or null
function locationString(data) {
  if (!data) return null;
  var parts = [];
  var city = data.city || data.locality;
  if (!city && data.localityInfo && data.localityInfo.administrative) {
    var admin = data.localityInfo.administrative;
    if (admin[3] && admin[3].name) city = admin[3].name;
  }
  if (city) parts.push(city);
  if (data.principalSubdivision) parts.push(data.principalSubdivision);
  var country = data.countryName;
  if (country === 'United States of America' || country === 'USA') country = 'United States';
  if (country) parts.push(country);
  return parts.length ? parts.join(', ') : null;
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
  topicIdOf: topicIdOf,
  buildCatsPayload: buildCatsPayload,
  buildBrowseTopicsPayload: buildBrowseTopicsPayload,
  topicDetailPayload: topicDetailPayload,
  topicCatIds: topicCatIds,
  haversineKm: haversineKm,
  looksLikeCinema: looksLikeCinema,
  extractTheaters: extractTheaters,
  extractMovieTitles: extractMovieTitles,
  dedupeTitles: dedupeTitles,
  buildTheatersPayload: buildTheatersPayload,
  locationString: locationString,
};
