# Unavailable dialogue pages are skipped by index, not by next_page_id

When navigation lands on a page that is not available — disabled, gated off by its
check flag, or already burnt — the engine scans forward by index through the dialogue's
`page_range` until it finds one that is, and ends the dialogue if it runs off the end.
The skipped pages' flags and display scripts do not run, and their own `next_page_id` is
never consulted.

A reader would reasonably assume the skip follows `next_page_id`, since that is what the
page carries for navigation. It does not, for two reasons. Adjacency is what makes the
greeting-variant pattern work: a one-time "Hey, I'm Glitch!" at index 0 followed by
"Nice to see you again!" at index 1, both pointing at the same hub page, needs nothing
authored to connect them beyond their order. And a scan by index terminates on its own,
where following `next_page_id` through a page graph could cycle forever inside one frame.

The same rule covers the dialogue's entry page, `next_page_id` and `goto_page_id`, so
there is one behaviour to learn.
