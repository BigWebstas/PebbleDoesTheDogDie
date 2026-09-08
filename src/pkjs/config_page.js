/* Self-contained settings page, served via a data: URI (no hosting, no Clay).
 * Submits back to the watch app through pebblejs://close#<json>. */

function buildConfigPage(settings) {
  var s = settings || {};

  function esc(v) {
    return String(v == null ? '' : v)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  var cd = parseInt(s.cacheDays, 10) || 7;
  function opt(v, txt) {
    return '<option value="' + v + '"' + (cd === v ? ' selected' : '') + '>' + txt + '</option>';
  }
  var spoilCk = s.hideSpoilers ? 'checked' : '';
  var sensCk = s.hideSensitive ? 'checked' : '';

  return '<!DOCTYPE html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<meta name="color-scheme" content="light dark">' +
    '<title>Does the Dog Die? Settings</title><style>' +
    ':root{--bg:#f2f2f2;--fg:#222;--sub:#666;--card:#fff;--card-shadow:rgba(0,0,0,.08);' +
    '--hint:#888;--field-bg:#fff;--field-border:#ccc;--accent:#c0392b;--on-accent:#fff}' +
    '@media (prefers-color-scheme:dark){:root{--bg:#1c1c1e;--fg:#e6e6e9;--sub:#9a9aa0;' +
    '--card:#2c2c2e;--card-shadow:rgba(0,0,0,.4);--hint:#8e8e93;' +
    '--field-bg:#1c1c1e;--field-border:#48484a;--accent:#ff6b5d;--on-accent:#1c1c1e}}' +
    'body{font-family:-apple-system,Roboto,Helvetica,Arial,sans-serif;margin:0;background:var(--bg);color:var(--fg)}' +
    '.wrap{max-width:520px;margin:0 auto;padding:20px}' +
    'h1{font-size:20px;margin:8px 0 4px}p.sub{margin:0 0 20px;color:var(--sub);font-size:13px}' +
    '.card{background:var(--card);border-radius:12px;padding:16px;margin-bottom:16px;box-shadow:0 1px 3px var(--card-shadow)}' +
    'label{display:block;font-weight:600;font-size:14px;margin-bottom:6px}' +
    '.hint{font-weight:400;color:var(--hint);font-size:12px;margin:2px 0 10px}' +
    'input[type=text]{width:100%;box-sizing:border-box;padding:11px;border:1px solid var(--field-border);' +
    'border-radius:8px;font-size:15px;background:var(--field-bg);color:var(--fg)}' +
    'select{width:100%;box-sizing:border-box;padding:11px;border:1px solid var(--field-border);' +
    'border-radius:8px;font-size:15px;background:var(--field-bg);color:var(--fg)}' +
    '.check{display:flex;align-items:center;gap:8px;font-weight:400;margin-top:8px}' +
    '.check input{width:18px;height:18px}' +
    'button{width:100%;padding:14px;border:0;border-radius:10px;background:var(--accent);color:var(--on-accent);' +
    'font-size:16px;font-weight:600;margin-top:4px}' +
    'a{color:var(--accent)}' +
    '</style></head><body><div class="wrap">' +
    '<h1>Does the Dog Die?</h1>' +
    '<p class="sub">Emotional spoilers and content warnings on your wrist, from doesthedogdie.com.</p>' +

    '<div class="card">' +
    '<label>API key <span style="color:var(--accent)">*</span></label>' +
    '<div class="hint">Required. Make a free account at ' +
    '<a href="https://www.doesthedogdie.com">doesthedogdie.com</a> and copy the ' +
    'API key from your profile page.</div>' +
    '<input type="text" id="key" autocapitalize="off" autocorrect="off" spellcheck="false" ' +
    'placeholder="Paste API key" value="' + esc(s.apiKey) + '">' +
    '</div>' +

    '<div class="card">' +
    '<label>Search a title</label>' +
    '<div class="hint">For watches with no microphone. Type a movie, show, book or ' +
    'game and the results open on your watch when you save.</div>' +
    '<input type="text" id="search" placeholder="e.g. John Wick" value="">' +
    '</div>' +

    '<div class="card">' +
    '<label>Hide by flag</label>' +
    '<div class="hint">doesthedogdie.com marks some triggers as spoilers or as ' +
    'sensitive. Hide them from the list if you prefer.</div>' +
    '<label class="check"><input type="checkbox" id="spoil" ' + spoilCk + '> Hide spoiler triggers</label>' +
    '<label class="check"><input type="checkbox" id="sens" ' + sensCk + '> Hide sensitive triggers</label>' +
    '</div>' +

    '<div class="card">' +
    '<label>Cache triggers for</label>' +
    '<div class="hint">Longer = fewer API calls, staler votes.</div>' +
    '<select id="cache">' + opt(1, '1 day') + opt(3, '3 days') + opt(7, '7 days') + '</select>' +
    '</div>' +

    '<button id="save">Save</button>' +
    '</div><script>' +
    'document.getElementById("save").addEventListener("click",function(){' +
    'var out={apiKey:document.getElementById("key").value.trim(),' +
    'search:document.getElementById("search").value.trim(),' +
    'hideSpoilers:document.getElementById("spoil").checked,' +
    'hideSensitive:document.getElementById("sens").checked,' +
    'cacheDays:parseInt(document.getElementById("cache").value,10)};' +
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(out));});' +
    '</script></body></html>';
}

module.exports = { buildConfigPage: buildConfigPage };
