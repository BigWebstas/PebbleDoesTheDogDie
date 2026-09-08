/* Does the Dog Die? - PebbleKit JS (phone side)
 *
 *   1. watch sends a dictated / typed title       -> /dddsearch?q=
 *   2. watch picks a result                       -> /media/{id}
 *   3. phone streams the trigger list to the watch as one delimited string
 *
 * All networking lives here; the watch only renders. Wire format in parse.js.
 */

var P = require('./parse');
var configPage = require('./config_page');

var MAX_RESULTS = 12;
var MAX_TOPICS = 16;
var MAX_RECENTS = 8;
var MAX_BROWSE_TOPICS = 48;

var API_BASE = 'https://www.doesthedogdie.com';
var DAY_MS = 24 * 60 * 60 * 1000;
var TAXONOMY_TTL_MS = 30 * DAY_MS;

var state = { lastSearchAt: 0 };

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

function loadSettings() {
  var s = { apiKey: '', serpApiKey: '', cacheDays: 7, hideSpoilers: false, hideSensitive: false };
  try {
    var raw = localStorage.getItem('settings');
    if (raw) {
      var parsed = JSON.parse(raw);
      for (var k in parsed) { if (parsed.hasOwnProperty(k)) s[k] = parsed[k]; }
    }
  } catch (e) { /* ignore */ }
  return s;
}

function saveSettings(s) {
  try { localStorage.setItem('settings', JSON.stringify(s)); } catch (e) {}
}

// ---------------------------------------------------------------------------
// Response cache (localStorage)
// ---------------------------------------------------------------------------

var CACHE_PREFIX = 'cache:v1:';

function cacheGet(key, ttlMs) {
  try {
    var raw = localStorage.getItem(CACHE_PREFIX + key);
    if (!raw) return null;
    var o = JSON.parse(raw);
    if (!o || (Date.now() - o.t) > ttlMs) return null;
    return o.v;
  } catch (e) { return null; }
}

function cacheSet(key, value) {
  try {
    localStorage.setItem(CACHE_PREFIX + key, JSON.stringify({ t: Date.now(), v: value }));
  } catch (e) { /* quota / private mode - skip */ }
}

function clearCache() {
  try {
    var kill = [];
    for (var i = 0; i < localStorage.length; i++) {
      var k = localStorage.key(i);
      if (k && k.indexOf(CACHE_PREFIX) === 0) kill.push(k);
    }
    for (var j = 0; j < kill.length; j++) localStorage.removeItem(kill[j]);
  } catch (e) {}
}

// ---------------------------------------------------------------------------
// Recent searches
// ---------------------------------------------------------------------------

function recentsGet() {
  try { return JSON.parse(localStorage.getItem('recents')) || []; }
  catch (e) { return []; }
}

function recentsPush(meta) {
  if (!meta || !meta.id) return;
  var list = recentsGet().filter(function (r) { return r.id !== meta.id; });
  list.unshift({ id: meta.id, name: meta.name || '', year: meta.year || '' });
  list = list.slice(0, MAX_RECENTS);
  try { localStorage.setItem('recents', JSON.stringify(list)); } catch (e) {}
}

// ---------------------------------------------------------------------------
// Messaging to the watch
// ---------------------------------------------------------------------------

function sendToWatch(dict, attempt) {
  attempt = attempt || 0;
  Pebble.sendAppMessage(dict, null, function () {
    if (attempt < 3) {
      setTimeout(function () { sendToWatch(dict, attempt + 1); }, 400);
    }
  });
}

function sendError(msg) {
  console.log('DTDD error: ' + msg);
  sendToWatch({ ERROR: String(msg).slice(0, 120) });
}

function sendStatus(msg) {
  sendToWatch({ STATUS: String(msg).slice(0, 120) });
}

function sendRecents() {
  sendToWatch({ RECENTS: P.buildRecentsPayload(recentsGet()) });
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

// GET a doesthedogdie.com endpoint. A non-JSON body (their 404 page is HTML)
// comes back as the 'Not found' error rather than throwing.
function apiGet(path, apiKey, cb, timeoutMs) {
  var req = new XMLHttpRequest();
  req.open('GET', API_BASE + path, true);
  req.setRequestHeader('Accept', 'application/json');
  if (apiKey) req.setRequestHeader('X-API-KEY', apiKey);
  req.timeout = timeoutMs || 20000;

  req.onload = function () {
    var body = null;
    try { body = JSON.parse(req.responseText); } catch (e) {}

    if (req.status >= 200 && req.status < 300) {
      if (body) cb(null, body);
      else cb('Not found');
    } else if (req.status === 401 || req.status === 403) {
      cb('API key rejected. Check it in settings.');
    } else if (req.status === 404) {
      cb('Not found');
    } else if (req.status === 429) {
      cb('Rate limited. Wait a minute.');
    } else {
      console.log('HTTP ' + req.status + ' for ' + path);
      cb('Server error ' + req.status);
    }
  };
  req.onerror = function () { cb('Network error'); };
  req.ontimeout = function () { cb('Request timed out'); };
  req.send();
}

// doesthedogdie.com v3 API (documented, X-API-KEY, no Accept header). Used for
// the trigger taxonomy - the old /dddsearch + /media endpoints have no such
// data. Free tier is 30 req/min, 5000/month, non-commercial.
function v3Get(path, apiKey, cb, timeoutMs) {
  var req = new XMLHttpRequest();
  req.open('GET', API_BASE + path, true);
  if (apiKey) req.setRequestHeader('X-API-KEY', apiKey);
  req.timeout = timeoutMs || 20000;
  req.onload = function () {
    var body = null;
    try { body = JSON.parse(req.responseText); } catch (e) {}
    if (req.status >= 200 && req.status < 300 && body) cb(null, body);
    else if (req.status === 401 || req.status === 403) cb('API key rejected or tier too low.');
    else if (req.status === 429) cb('Rate limited. Wait a minute.');
    else cb('Server error ' + req.status);
  };
  req.onerror = function () { cb('Network error'); };
  req.ontimeout = function () { cb('Request timed out'); };
  req.send();
}

// Plain JSON GET (SerpApi, BigDataCloud) - no DDD headers. SerpApi / OMDb put a
// helpful message in the body on 4xx.
function httpGetJson(url, cb, timeoutMs) {
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = timeoutMs || 20000;
  req.onload = function () {
    var body = null;
    try { body = JSON.parse(req.responseText); } catch (e) {}
    if (req.status >= 200 && req.status < 300) {
      body ? cb(null, body) : cb('Unexpected response');
    } else if (body && body.error) {
      cb(body.error);
    } else {
      cb('Server error ' + req.status);
    }
  };
  req.onerror = function () { cb('Network error'); };
  req.ontimeout = function () { cb('Request timed out'); };
  req.send();
}

// ---------------------------------------------------------------------------
// Location + SerpApi (only used by the "in theaters near me" list)
// ---------------------------------------------------------------------------

function getLocation(cb) {
  if (!navigator.geolocation) { cb('Location unavailable'); return; }
  navigator.geolocation.getCurrentPosition(
    function (pos) { cb(null, { lat: pos.coords.latitude, lon: pos.coords.longitude }); },
    function (err) {
      cb(err && err.code === 1 ? 'Location permission denied on phone' : 'Could not get location');
    },
    { enableHighAccuracy: false, timeout: 15000, maximumAge: 5 * 60 * 1000 }
  );
}

function reverseGeocode(lat, lon, cb) {
  var url = 'https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=' +
    encodeURIComponent(lat) + '&longitude=' + encodeURIComponent(lon) + '&localityLanguage=en';
  httpGetJson(url, function (err, data) { cb(err ? null : P.locationString(data)); }, 10000);
}

function serpUrl(params, serpApiKey) {
  var qs = [];
  for (var k in params) {
    if (params.hasOwnProperty(k) && params[k] != null && params[k] !== '') {
      qs.push(encodeURIComponent(k) + '=' + encodeURIComponent(params[k]));
    }
  }
  qs.push('api_key=' + encodeURIComponent(serpApiKey));
  return 'https://serpapi.com/search.json?' + qs.join('&');
}

// ---------------------------------------------------------------------------
// Step 1: search for a title
// ---------------------------------------------------------------------------

function doSearch(query, force) {
  var s = loadSettings();
  query = String(query == null ? '' : query).trim();
  if (!query) { sendError('Nothing to search for.'); return; }
  if (!s.apiKey) {
    sendError('Add your doesthedogdie.com API key in the app settings on your phone.');
    return;
  }

  var key = 'search:' + query.toLowerCase();
  if (!force) {
    var hit = cacheGet(key, DAY_MS);
    if (hit != null) { sendToWatch({ RESULTS: hit }); return; }
  }

  sendStatus('Searching "' + query + '"');
  apiGet('/dddsearch?q=' + encodeURIComponent(query), s.apiKey, function (err, data) {
    if (err) { sendError(err); return; }
    var items = (data && data.items) || [];
    if (!items.length) { sendError('No match for "' + query + '".'); return; }
    var payload = P.buildResultsPayload(items, MAX_RESULTS);
    cacheSet(key, payload);
    sendToWatch({ RESULTS: payload });
  });
}

// ---------------------------------------------------------------------------
// Step 2: triggers for one title
// ---------------------------------------------------------------------------

// Cache the raw stats + meta (not the built payload) so starring a trigger or
// changing the spoiler filters takes effect without re-fetching.
function emitTopics(stats, meta, s) {
  var payload = P.buildTopicsPayload(stats, {
    hideSpoilers: s.hideSpoilers,
    hideSensitive: s.hideSensitive,
    starred: starredGet(),
    max: MAX_TOPICS,
  });
  if (!payload) { sendError('No trigger votes yet for this title.'); return false; }
  recentsPush(meta);
  sendToWatch({ TOPICS: payload });
  sendRecents();
  return true;
}

function doMedia(id, force) {
  var s = loadSettings();
  id = parseInt(id, 10);
  if (!id) { sendError('Pick a title again.'); return; }
  if (!s.apiKey) { sendError('Add your API key in settings.'); return; }

  var key = 'media:' + id;
  var ttl = (s.cacheDays || 7) * DAY_MS;
  if (!force) {
    var hit = cacheGet(key, ttl);
    if (hit != null && hit.stats) {
      emitTopics(hit.stats, hit.meta, s);
      return;
    }
  }

  sendStatus('Checking triggers');
  apiGet('/media/' + id, s.apiKey, function (err, data) {
    if (err) { sendError(err); return; }
    var stats = (data && data.topicItemStats) || [];
    var item = (data && data.item) || {};
    var meta = { id: id, name: item.name || '', year: item.releaseYear || '' };
    if (emitTopics(stats, meta, s)) cacheSet(key, { stats: stats, meta: meta });
  });
}

// ---------------------------------------------------------------------------
// "In theaters near me": GPS -> nearest ~2 cinemas -> today's movies
// ---------------------------------------------------------------------------

var THEATERS_TTL_MS = 6 * 60 * 60 * 1000;
var NEAREST_N = 2;

function today() {
  var d = new Date();
  return d.getFullYear() + '-' + (d.getMonth() + 1) + '-' + d.getDate();
}

function doTheaters(force) {
  var s = loadSettings();
  if (!s.serpApiKey) {
    sendError('Add a SerpApi key in settings for the In Theaters list.');
    return;
  }

  sendStatus('Finding cinemas near you');
  getLocation(function (err, coords) {
    if (err) { sendError(err); return; }

    var key = 'theaters:' + coords.lat.toFixed(2) + ',' + coords.lon.toFixed(2) + ':' + today();
    if (!force) {
      var hit = cacheGet(key, THEATERS_TTL_MS);
      if (hit != null) { sendToWatch({ RESULTS: hit }); return; }
    }

    reverseGeocode(coords.lat, coords.lon, function (city) {
      var cityShort = city ? city.split(',')[0] : '';

      var mapsUrl = serpUrl({
        engine: 'google_maps', type: 'search', q: 'movie theater',
        ll: '@' + coords.lat + ',' + coords.lon + ',13z', hl: 'en',
      }, s.serpApiKey);

      httpGetJson(mapsUrl, function (e2, data) {
        if (e2) { sendError(e2); return; }
        var cinemas = P.extractTheaters(data);
        for (var i = 0; i < cinemas.length; i++) {
          cinemas[i]._km = (cinemas[i].lat != null)
            ? P.haversineKm(coords.lat, coords.lon, cinemas[i].lat, cinemas[i].lon) : 99999;
        }
        cinemas.sort(function (a, b) { return a._km - b._km; });
        cinemas = cinemas.slice(0, NEAREST_N);
        if (!cinemas.length) { sendError('No cinemas found near you.'); return; }

        sendStatus("Today's showtimes");
        var lists = [];
        var pending = cinemas.length;

        function done() {
          var titles = P.dedupeTitles(lists, MAX_RESULTS);
          if (!titles.length) { sendError('No showtimes listed nearby today.'); return; }
          var payload = P.buildTheatersPayload(titles);
          cacheSet(key, payload);
          sendToWatch({ RESULTS: payload });
        }

        cinemas.forEach(function (c) {
          var q = c.name + (cityShort ? ' ' + cityShort : '') + ' showtimes';
          var url = serpUrl({ engine: 'google', q: q, hl: 'en', gl: 'us' }, s.serpApiKey);
          httpGetJson(url, function (e3, d3) {
            if (!e3 && d3) lists.push(P.extractMovieTitles(d3));
            if (--pending === 0) done();
          }, 15000);
        });
      });
    });
  });
}

// ---------------------------------------------------------------------------
// Trigger browser: v3 taxonomy + starred triggers
// ---------------------------------------------------------------------------

function starredGet() {
  try { return JSON.parse(localStorage.getItem('starred')) || []; }
  catch (e) { return []; }
}

function starToggle(topicId, on) {
  topicId = parseInt(topicId, 10);
  if (!topicId) return;
  var list = starredGet().filter(function (id) { return id !== topicId; });
  if (on) list.push(topicId);
  try { localStorage.setItem('starred', JSON.stringify(list)); } catch (e) {}
}

// { cats:[{id,name}], topics:[{id,doesName,notName,description,catIds:[...]}] }
// cached 30 days - the taxonomy barely changes.
function loadTaxonomy(apiKey, cb) {
  var hit = cacheGet('taxonomy:v1', TAXONOMY_TTL_MS);
  if (hit) { cb(null, hit); return; }

  v3Get('/api/v3/topiccategories', apiKey, function (e1, cats) {
    if (e1) { cb(e1); return; }
    v3Get('/api/v3/topics', apiKey, function (e2, topics) {
      if (e2) { cb(e2); return; }
      var tax = {
        cats: (cats || []).map(function (c) { return { id: c.id, name: c.name }; }),
        topics: (topics || []).map(function (t) {
          return {
            id: t.id,
            doesName: t.doesName || t.name,
            name: t.name,
            notName: t.notName || '',
            description: t.description || '',
            catIds: P.topicCatIds(t),
          };
        }),
      };
      cacheSet('taxonomy:v1', tax);
      cb(null, tax);
    });
  });
}

function doBrowseCats(force) {
  var s = loadSettings();
  if (!s.apiKey) { sendError('Add your API key in settings.'); return; }
  if (force) clearCache();
  sendStatus('Loading categories');
  loadTaxonomy(s.apiKey, function (err, tax) {
    if (err) { sendError(err); return; }
    sendToWatch({ CATS: P.buildCatsPayload(tax.cats) });
  });
}

function doBrowseTopics(catId) {
  var s = loadSettings();
  catId = parseInt(catId, 10);
  if (!s.apiKey) { sendError('Add your API key in settings.'); return; }
  sendStatus('Loading triggers');
  loadTaxonomy(s.apiKey, function (err, tax) {
    if (err) { sendError(err); return; }
    var inCat = tax.topics.filter(function (t) { return t.catIds.indexOf(catId) >= 0; });
    var payload = P.buildBrowseTopicsPayload(inCat, starredGet(), MAX_BROWSE_TOPICS);
    if (!payload) { sendError('No triggers in this category.'); return; }
    sendToWatch({ BROWSE_TOPICS: payload });
  });
}

function doBrowseTopic(topicId) {
  var s = loadSettings();
  topicId = parseInt(topicId, 10);
  loadTaxonomy(s.apiKey, function (err, tax) {
    if (err) { sendError(err); return; }
    var t = null;
    for (var i = 0; i < tax.topics.length; i++) {
      if (tax.topics[i].id === topicId) { t = tax.topics[i]; break; }
    }
    if (!t) { sendError('Trigger not found.'); return; }
    sendToWatch({ TOPIC_DETAIL: P.topicDetailPayload(t) });
  });
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('DTDD: PebbleKit JS ready');
  sendRecents();
});

Pebble.addEventListener('appmessage', function (e) {
  var d = e.payload || {};
  if (d.REQUEST === 'search') {
    var now = Date.now();
    if (now - state.lastSearchAt < 2000) { console.log('ignoring rapid search'); return; }
    state.lastSearchAt = now;
    doSearch(d.QUERY, !!d.FORCE);
  } else if (d.REQUEST === 'media') {
    doMedia(d.MEDIA_ID, !!d.FORCE);
  } else if (d.REQUEST === 'theaters') {
    doTheaters(!!d.FORCE);
  } else if (d.REQUEST === 'recents') {
    sendRecents();
  } else if (d.REQUEST === 'browse_cats') {
    doBrowseCats(!!d.FORCE);
  } else if (d.REQUEST === 'browse_topics') {
    doBrowseTopics(d.PARENT_ID);
  } else if (d.REQUEST === 'browse_topic') {
    doBrowseTopic(d.TOPIC_ID);
  } else if (d.REQUEST === 'star') {
    starToggle(d.TOPIC_ID, d.STAR === 1);
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL('data:text/html;charset=utf-8,' +
    encodeURIComponent(configPage.buildConfigPage(loadSettings())));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var incoming;
  try { incoming = JSON.parse(decodeURIComponent(e.response)); }
  catch (err) { try { incoming = JSON.parse(e.response); } catch (e2) { return; } }

  var s = loadSettings();
  var keyChanged = incoming.apiKey !== undefined && String(incoming.apiKey).trim() !== s.apiKey;
  var serpChanged = incoming.serpApiKey !== undefined && String(incoming.serpApiKey).trim() !== s.serpApiKey;

  if (incoming.apiKey !== undefined) s.apiKey = String(incoming.apiKey).trim();
  if (incoming.serpApiKey !== undefined) s.serpApiKey = String(incoming.serpApiKey).trim();
  if (incoming.cacheDays !== undefined) {
    var cd = parseInt(incoming.cacheDays, 10);
    if (cd === 1 || cd === 3 || cd === 7) s.cacheDays = cd;
  }
  if (incoming.hideSpoilers !== undefined) s.hideSpoilers = !!incoming.hideSpoilers;
  if (incoming.hideSensitive !== undefined) s.hideSensitive = !!incoming.hideSensitive;
  saveSettings(s);

  if (keyChanged) clearCache();
  else if (serpChanged) clearCache();  // drops the theaters:* entries too

  // The "search a title" box on the config page is the no-microphone path.
  var typed = incoming.search && String(incoming.search).trim();
  if (typed) {
    doSearch(typed, false);
  }
});

// Exposed for test/parse.test.js; ignored by the Pebble runtime.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { _loadSettings: loadSettings, _recentsPush: recentsPush };
}
