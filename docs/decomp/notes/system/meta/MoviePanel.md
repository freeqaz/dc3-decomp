# Lane notes: `src/system/meta/MoviePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?IsLoaded@MoviePanel@@UBA_NXZ`

`MoviePanel::IsLoaded` -- moved verbatim from `src/system/meta/MoviePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 236-240):

```text
    // Web skips only the movie half: WebMovieImpl's Ready() is "the <video>
    // element's metadata has arrived", which only its Poll() observes -- a
    // panel waiting on it before it polls could wait forever.  The image's
    // subtitles-loader wait below runs on every build (w23-web: web used to
    // return UIPanel::IsLoaded() before it).
```
