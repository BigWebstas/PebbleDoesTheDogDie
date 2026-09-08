/* Plain-node unit tests for src/pkjs/parse.js  ->  `node test/parse.test.js` */

var assert = require('assert');
var P = require('../src/pkjs/parse');

var pass = 0;
function t(name, fn) {
  try { fn(); pass++; console.log('  ok  ' + name); }
  catch (e) { console.error('  FAIL ' + name + '\n       ' + e.message); process.exitCode = 1; }
}

var REC = '\x1e';
var FLD = '\x1f';

// --- verdictFor ------------------------------------------------------------
t('verdictFor: yes wins', function () {
  assert.strictEqual(P.verdictFor(1336, 118), 'Y');
});
t('verdictFor: no wins', function () {
  assert.strictEqual(P.verdictFor(4, 52), 'N');
});
t('verdictFor: tie and zero are inconclusive', function () {
  assert.strictEqual(P.verdictFor(10, 10), '?');
  assert.strictEqual(P.verdictFor(0, 0), '?');
});
t('verdictFor: string counts', function () {
  assert.strictEqual(P.verdictFor('90', '3'), 'Y');
});

// --- sanitize / clamp ----------------------------------------------------
t('sanitize strips delimiters and control chars', function () {
  assert.strictEqual(P.sanitize('a\x1fb\x1ec\nd'), 'a b c d');
});
t('clamp trims to a word boundary and adds an ellipsis', function () {
  var out = P.clamp('the quick brown fox jumps over the lazy dog', 20);
  assert.ok(out.length <= 20, out);
  assert.ok(/…$/.test(out), out);
});

// --- questionOf --------------------------------------------------------
t('questionOf prefers doesName', function () {
  assert.strictEqual(P.questionOf({ doesName: 'Does the dog die', name: 'a dog dies' }),
    'Does the dog die');
});
t('questionOf falls back to name', function () {
  assert.strictEqual(P.questionOf({ name: 'a spider appears' }), 'a spider appears');
});

// --- buildResultsPayload -------------------------------------------------
t('buildResultsPayload shapes records and caps the count', function () {
  var items = [];
  for (var i = 0; i < 20; i++) {
    items.push({ id: i + 1, name: 'Movie ' + i, releaseYear: '20' + (10 + i),
      itemType: { name: 'Movie' } });
  }
  var payload = P.buildResultsPayload(items, 12);
  var recs = payload.split(REC);
  assert.strictEqual(recs.length, 12);
  var f = recs[0].split(FLD);
  assert.deepStrictEqual(f, ['1', 'Movie 0', '2010', 'Movie']);
});
t('buildResultsPayload skips items with no id or name', function () {
  var payload = P.buildResultsPayload([{ name: 'no id' }, { id: 5 }, { id: 7, name: 'ok' }], 12);
  assert.strictEqual(payload.split(REC).length, 1);
  assert.strictEqual(payload.split(FLD)[1], 'ok');
});

// --- sortTopics --------------------------------------------------------
t('sortTopics: happens first, then unclear, then does-not-happen', function () {
  var stats = [
    { yesSum: 2, noSum: 90, topic: { name: 'no' } },      // N
    { yesSum: 50, noSum: 50, topic: { name: 'tie' } },    // ?
    { yesSum: 200, noSum: 5, topic: { name: 'yes-strong' } },
    { yesSum: 12, noSum: 8, topic: { name: 'yes-weak' } },
  ];
  var order = P.sortTopics(stats).map(function (s) { return s.topic.name; });
  assert.deepStrictEqual(order, ['yes-strong', 'yes-weak', 'tie', 'no']);
});

// --- buildTopicsPayload ------------------------------------------------
function stat(name, yes, no, extra) {
  var topic = { name: name, doesName: 'Does ' + name };
  for (var k in (extra || {})) topic[k] = extra[k];
  return { yesSum: yes, noSum: no, comment: 'top comment for ' + name, topic: topic };
}

t('buildTopicsPayload: record shape and verdict', function () {
  var payload = P.buildTopicsPayload([stat('x happen', 100, 3)], {});
  var f = payload.split(FLD);
  assert.strictEqual(f[0], 'Y');
  assert.strictEqual(f[1], 'Does x happen');
  assert.strictEqual(f[2], '100');
  assert.strictEqual(f[3], '3');
  assert.strictEqual(f[4], 'top comment for x happen');
});
t('buildTopicsPayload: drops zero-vote and invisible topics', function () {
  var payload = P.buildTopicsPayload([
    stat('voted', 5, 1),
    stat('unvoted', 0, 0),
    stat('hidden', 9, 1, { isVisible: false }),
  ], {});
  assert.strictEqual(payload.split(REC).length, 1);
  assert.strictEqual(payload.split(FLD)[1], 'Does voted');
});
t('buildTopicsPayload: hideSpoilers / hideSensitive filters', function () {
  var stats = [stat('plain', 5, 1), stat('spoily', 5, 1, { isSpoiler: true }),
    stat('touchy', 5, 1, { isSensitive: true })];
  assert.strictEqual(
    P.buildTopicsPayload(stats, { hideSpoilers: true }).split(REC).length, 2);
  assert.strictEqual(
    P.buildTopicsPayload(stats, { hideSpoilers: true, hideSensitive: true }).split(REC).length, 1);
});
t('buildTopicsPayload: empty when nothing survives', function () {
  assert.strictEqual(P.buildTopicsPayload([stat('unvoted', 0, 0)], {}), '');
});
t('buildTopicsPayload: caps rows and clamps the comment', function () {
  var stats = [];
  for (var i = 0; i < 30; i++) stats.push(stat('t' + i, 10, i));
  stats[0].comment = new Array(400).join('x ');
  var payload = P.buildTopicsPayload(stats, { max: 16 });
  var recs = payload.split(REC);
  assert.strictEqual(recs.length, 16);
  assert.ok(recs[0].split(FLD)[4].length <= 120, 'comment clamped');
});

// --- in theaters near me -----------------------------------------------
t('looksLikeCinema keeps real cinemas, drops venues and A/V shops', function () {
  assert.ok(P.looksLikeCinema('AMC Empire 25', 'Movie theater'));
  assert.ok(P.looksLikeCinema('Alamo Drafthouse Cinema', ''));
  assert.ok(!P.looksLikeCinema('Radio City Music Hall', 'Concert hall'));
  assert.ok(!P.looksLikeCinema('Bob\'s Home Theater Installation', 'Audio visual equipment supplier'));
});
t('extractTheaters filters and keeps coordinates', function () {
  var data = { local_results: [
    { title: 'Regal Union Square', type: 'Movie theater',
      gps_coordinates: { latitude: 40.735, longitude: -73.991 } },
    { title: 'The Town Hall', type: 'Auditorium' },
    { position: 2 },
  ] };
  var list = P.extractTheaters(data);
  assert.strictEqual(list.length, 1);
  assert.strictEqual(list[0].name, 'Regal Union Square');
  assert.strictEqual(list[0].lat, 40.735);
});
t('extractMovieTitles reads the today block', function () {
  var data = { showtimes: [
    { day: 'Tomorrow', movies: [{ name: 'Old Movie' }] },
    { day: 'Today', movies: [{ name: 'Dune: Part Two' }, { name: 'Kung Fu Panda 4' }] },
  ] };
  assert.deepStrictEqual(P.extractMovieTitles(data), ['Dune: Part Two', 'Kung Fu Panda 4']);
});
t('extractMovieTitles returns [] when there is no showtimes box', function () {
  assert.deepStrictEqual(P.extractMovieTitles({ organic_results: [] }), []);
});
t('dedupeTitles merges, de-dupes case-insensitively, caps, keeps order', function () {
  var out = P.dedupeTitles([['Dune', 'Godzilla'], ['dune', 'Wicked', 'Godzilla', 'Moana 2']], 3);
  assert.deepStrictEqual(out, ['Dune', 'Godzilla', 'Wicked']);
});
t('buildTheatersPayload emits zero-id records', function () {
  var payload = P.buildTheatersPayload(['Dune: Part Two', 'Wicked']);
  var recs = payload.split(REC);
  assert.strictEqual(recs.length, 2);
  assert.deepStrictEqual(recs[0].split(FLD), ['0', 'Dune: Part Two', '', 'In theaters']);
});

console.log('\n' + pass + ' passing');
