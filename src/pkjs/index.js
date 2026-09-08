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

var API_BASE = 'https://www.doesthedogdie.com';
var DAY_MS = 24 * 60 * 60 * 1000;

var state = { lastSearchAt: 0 };

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

function loadSettings() {
  var s = { apiKey: '', cacheDays: 7, hideSpoilers: false, hideSensitive: false };
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

function doMedia(id, force) {
  var s = loadSettings();
  id = parseInt(id, 10);
  if (!id) { sendError('Pick a title again.'); return; }
  if (!s.apiKey) { sendError('Add your API key in settings.'); return; }

  var key = 'media:' + id;
  var ttl = (s.cacheDays || 7) * DAY_MS;
  if (!force) {
    var hit = cacheGet(key, ttl);
    if (hit != null) {
      recentsPush(hit.meta);
      sendToWatch({ TOPICS: hit.payload });
      sendRecents();
      return;
    }
  }

  sendStatus('Checking triggers');
  apiGet('/media/' + id, s.apiKey, function (err, data) {
    if (err) { sendError(err); return; }
    var stats = (data && data.topicItemStats) || [];
    var payload = P.buildTopicsPayload(stats, {
      hideSpoilers: s.hideSpoilers,
      hideSensitive: s.hideSensitive,
      max: MAX_TOPICS,
    });
    if (!payload) { sendError('No trigger votes yet for this title.'); return; }

    var item = (data && data.item) || {};
    var meta = { id: id, name: item.name || '', year: item.releaseYear || '' };
    cacheSet(key, { payload: payload, meta: meta });
    recentsPush(meta);
    sendToWatch({ TOPICS: payload });
    sendRecents();
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
  } else if (d.REQUEST === 'recents') {
    sendRecents();
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
  var keyChanged = incoming.apiKey !== undefined && incoming.apiKey !== s.apiKey;

  if (incoming.apiKey !== undefined) s.apiKey = String(incoming.apiKey).trim();
  if (incoming.cacheDays !== undefined) {
    var cd = parseInt(incoming.cacheDays, 10);
    if (cd === 1 || cd === 3 || cd === 7) s.cacheDays = cd;
  }
  if (incoming.hideSpoilers !== undefined) s.hideSpoilers = !!incoming.hideSpoilers;
  if (incoming.hideSensitive !== undefined) s.hideSensitive = !!incoming.hideSensitive;
  saveSettings(s);

  if (keyChanged) clearCache();

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
