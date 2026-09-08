# Does the Dog Die? for Pebble

Check emotional spoilers and content warnings for a movie, show, book, or game
straight from your wrist, backed by the crowd database at
[doesthedogdie.com](https://www.doesthedogdie.com).

- **Voice search.** Press select, say a title. Watches with no microphone type
  the title on the phone settings page instead.
- **In theaters near me.** GPS finds nearby cinemas and lists today's movies;
  tap one to see its triggers. Needs a free SerpApi key.
- **Browse triggers.** Drill the doesthedogdie.com trigger taxonomy by category,
  read each one's definition, and **star** the ones you care about — starred
  triggers pin to the top of every title's list with a ★.
- **Trigger list.** Every tracked trigger for the title with the crowd's
  verdict — **Yes** / **No** / **Unclear** — and the vote tally. On colour
  watches a "yes" is tinted red, a "no" green.
- **Detail.** Open a trigger for the full question, the vote split, and the
  top-voted comment.
- **Recents.** Your last few lookups sit on the home screen.

Runs on basalt, chalk, diorite, emery, and gabbro. (The original aplite Pebble
is not supported — no microphone, and too little RAM for the trigger browser.)

Content and trigger data are Powered by DoesTheDogDie.com.

## Setup

1. Install the `.pbw`.
2. Open the app's settings in the Pebble phone app.
3. Make a free account at [doesthedogdie.com](https://www.doesthedogdie.com) and
   paste the API key from your profile page.
4. Optional, for "In theaters near me": paste a free
   [SerpApi](https://serpapi.com/manage-api-key) key (100 searches/month).

## Build

```sh
export PATH="$HOME/.local/bin:$PATH"
pebble build
pebble install --emulator basalt
```

See [CLAUDE.md](CLAUDE.md) for the architecture, the AppMessage wire format, and
how to test without an API key.

## Credits

Trigger data © the doesthedogdie.com community. This app is an unofficial
client and is not affiliated with doesthedogdie.com.
