# 03 — Pages honour availability and the sequential skip

**What to build:** A character can greet the player differently the second time
they talk. A page can be written to happen only once, or only when the story has
reached a certain point, and the conversation works around the pages that are not
currently available rather than showing them anyway.

A page is available when three things hold: the author enabled it, its check flag
is either absent or currently true, and it has not already been burnt by its
one-time bit. The authored enable bit never changes at runtime — the loaded game
data is read-only.

When the conversation arrives at a page that is not available, it scans forward by
index through the dialogue's pages until it finds one that is, and ends if it runs
off the end. Skipped pages run nothing and their own next-page field is never
consulted. This is by index rather than by following next-page links, and ADR-0002
records why: adjacency is what makes the greeting-variant pattern need no extra
wiring, and a scan by index cannot loop forever the way a walk through a page graph
could. The same rule governs the entry page and every later navigation alike.

A one-time page is burnt when it is displayed — that is what "read" means.

**Blocked by:** 02 — A dialogue appears on screen and runs to its end.

**Status:** done

- [x] A page disabled by its author is never shown
- [x] A page whose check flag is false is skipped; the same page is shown once that flag is true
- [x] A page with no check flag is always available on that count
- [x] A one-time page is shown the first time and skipped on every later visit
- [x] An unavailable page is replaced by the next available page by index, not by its next-page field
- [x] Skipped pages set no flags, run nothing, and are not marked read
- [x] A dialogue whose pages are all unavailable ends instead of hanging
- [x] The same skip applies to the entry page and to navigation alike
- [x] The greeting-variant pattern works: a one-time opening page, then its replacement, both leading to the same page
- [x] Tests cover each availability condition alone and in combination, through the mock bridge
