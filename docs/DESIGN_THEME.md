# Design theme

The visualizer's theme, so the next page or the next project can start from the
same rules instead of reinventing them.

The single source is the `:root` block at the top of
[`visualizer/style.css`](../visualizer/style.css).
This note says what the values mean, because the values alone are not the theme.

## The idea

An annotated order ledger.
White paper, black ink, grey rules, and one muted leafy green that only ever
means the reader's attention or a change the engine made.

No second hue.
The bid side is leaf and the ask side is graphite, so the two sides never both
look like the accent and the layout carries the side without relying on colour.
Adding a colour means adding a meaning, and the page has no use for a third one.

## Colour

| Token | Value | Carries |
|---|---|---|
| `--paper` | `#f7f8f4` | The canvas, and the band the price axis is drawn on |
| `--panel` | `#ffffff` | Every card, so the faint grid never runs through data |
| `--ink` | `#1b211d` | Headings, prices, ids, the current step |
| `--ink-2` | `#3a423c` | Button text, and the ask side of the ladder |
| `--muted` | `#536058` | Explanations, the intro copy |
| `--subtle` | `#6b726b` | Panel labels, captions, the disabled control |
| `--rule` | `#d9ded7` | Card borders, the table header rule |
| `--rule-soft` | `#e9ece6` | Row separators, the chart's maximum-depth line |
| `--grid` | `#ecefe8` | The page background grid, visible only in the gutters |
| `--leaf` | `#426849` | The accent: selection, fills, the bid side, primary control |
| `--leaf-deep` | `#315638` | Leaf text on a leaf background |
| `--leaf-wash` | `#e9f0e8` | The bid depth area, the selected row, the prediction block |
| `--graphite` | `#252c27` | The one dark surface: the band that opens the source section |
| `--graphite-2` | `#3d453f` | The ask side, where the bid side would be leaf |
| `--band-label` | `#8fbf9d` | The section label on graphite |
| `--band-text` | `#dfe4de` | The sentence that sits on graphite |
| `--refused` | `#7c5341` | A refused event's label and flag |
| `--refused-wash` | `#f6efe9` | A refused event's ground, and the load error |
| `--sans` | system stack | Everything read as words |
| `--mono` | system mono | Everything the engine counted |

Measured contrast, all AA or better for body text:
ink on panel 16.4:1, muted 6.6:1, subtle 5.0:1, leaf on panel 6.4:1,
white on leaf 6.4:1, leaf-deep on leaf-wash 7.2:1, band label on graphite 6.9:1,
band text on graphite 11.1:1, code ink on paper 15.4:1.

`subtle` on `leaf-wash` is 4.3:1, so leaf-wash is never used as the background
for `--subtle` text.
The prediction and the selected row both use `--leaf-deep` or `--ink` for that
reason.

## The one dark surface

The page has exactly one dark band, and it opens the source section: the part of
the page that is not the replay but the engine behind it.

It used to close the order book, restating the matching rule. That spent the
heaviest surface on the page on the sentence the intro already carries. A dark
band marks a change of kind, and it should carry the strongest claim the page
has, which here is provenance: every number above came out of that code, and the
page runs no matching logic of its own.

If a second dark band is ever wanted, the answer is no. The first one works
because it is the only one.

## Diagrams

`ARCHITECTURE_OVERVIEW.svg` uses the same tokens, and the file is small enough
that it is worth saying how they map.

The canvas is `--paper` and the four stage panels are `--panel` cards with a
`--rule` border, the same relationship the page has between the page and a card.
Nodes inside a panel are `--paper` again, which is what the code blocks do inside
the source panel: a ruled block on a white card.

The accent moves in a diagram. Leaf marks the flow, the numbers, and the subject
of the figure, because a diagram has one subject and one direction and no reader
has selected anything yet. It does not mark a selection there, and graphite marks
nothing at all, because a diagram has no single band to put it on.

Type is the same two families at the same relationship: sans for everything read
as words, mono for the numbers. No webfont, as on the page.

## Type

Three sizes on any one screen, and two families.

- Display: the page title, heavy sans, tight tracking, one weight.
- Body: 12.5px to 17px sans. The event summary is the largest thing in the rail.
- Data: 11px to 14px mono, tabular numerals, for every price, size, id and count.
- Panel labels: 10px uppercase with 0.14em tracking, in `--subtle`.
  There are three or four on a screen and nothing else is uppercase.

The quoted code is the one place the mono font is not carrying a number.
It is 11.5px on a wide screen and 11px below 900px, where a phone is close
enough to the column width that a step down keeps the longest quote inside its
block.
It is also the only text on the page allowed to scroll sideways: a wrapped line
of C++ is a line the reader has to reconstruct, so the block scrolls instead.
When it does scroll it takes `tabindex`, so the reader can reach and scroll it
from the keyboard, and only then, so a wide window does not add five stops to the
tab order.

Its highlight is four tones out of the page's own greys, plus weight. The leaf
accent already means something specific, so the code does not spend it: keywords
and calls carry the skeleton, types step down, comments recede. The markup holds
the quotes as plain text and the highlighter runs over that at load, so the file
stays something a test can compare against the source, and the highlighter puts
the text back if it ever changes a word.

The queue ticket is the exception worth naming: at 10.5px below 660px it is the
smallest text on the page, because the id and its size must stay on one line
through a five-column ladder on a phone.
That is a deliberate trade of size for legibility of the data the page is about.

## Space

A 4px base, used as 6, 8, 12, 14, 16, 20, 26.
Cards carry 16px side padding and a 6px radius, except the transport, which is
tighter at 12px vertically because it is a control strip rather than a content
card. Buttons have 4px.
Nothing is rounded further, because this is a ledger.

The rail is `minmax(248px, 292px)` and the book takes the rest.
Under 1080px the rail caps at 258px; under 900px the rail dissolves and the page
becomes one column in reading order: pick an event, read the book, then read
what it did.

The transport is one flex row that wraps: buttons, slider, counter, then the
keyboard hint at the far end. It is 58px on a laptop and 93px where the hint
wraps under the controls. Below 660px the hint is dropped entirely, because
there are no arrow keys on a phone.

The source section is not a card.
It sits on the page the way the title and the intro do, and the only chrome left on
it is the band. A card under a column of text was the whole problem: it put a white
canvas beside the text that the text did not fill, and that unfilled part of a white
rectangle reads as dead space where the same amount of page beside the hero reads as
margin. The rules between items are `--rule` rather than `--rule-soft`, because on
the page ground, next to the faint grid, a soft rule is nearly invisible and the
sections run together. The band keeps square corners for the same reason the card was
removed: it is a section band, and every card on this page is 6px.

It is set as a document rather than a card grid: a heading, a paragraph, the excerpt,
and the source link under it. The prose column is 580px and each excerpt is as wide as
its own longest line, up to 660px, so a short excerpt is a short block and the right of
the column is a rag.

The parameter panel, which only the loopback dev server reveals, sits full width
between the transport and the replay rather than in the rail. The book column
cannot grow to match a card that tall, so in the rail it left the whole right
column empty below the book: 566px of nothing at the widths where a reader would
see it.

The run switch sits under the intro, not in the transport, because the transport
is a position within one run and the switch is which run, and one card holding both
would make the control bar two things at once. It is the same scale as a control
button and the pressed state is the only accent on that row.

## Boxes that hold still

Two panels vary in content by step: the selected event panel and the fills panel
below it. Both have to hold one height while the reader steps, or the reader is
watching the layout instead of the data.

Their height is measured, not written down. `measureRailBoxes()` in `app.js`
renders every step's text and every step's fill list once, keeps the tallest, and
fixes the two boxes there. The measurement runs per run and per width, so a run
with no fills does not reserve room for two receipts, and a phone does not
reserve what the text needs on a laptop. The stylesheet sets neither height.

Hard-coding the worst case any run reaches was the first attempt, and it was the
wrong trade. It kept the boxes still and left 40px of dead space at the densest
step of the default run and 120px at the emptiest, which was most of the page's
vertical slack.

The prediction block and the `−` caption are still revealed with `visibility`
rather than `display`, so the steps they appear on do not change their panel's
content height before the measurement runs.

The ladder rows keep a 70px floor rather than a fixed height, because the number
of orders resting at one price is real data and a row has to hold them.
70px is three orders, which is the most any shipped scenario reaches, and it is
measured rather than guessed: a third order at one price is 4px taller than two,
and a row that grows for it drags the caption, the history chart and the source
section under it.
The floor was 66px until that was measured against the rejection trace, where
three bids rest at 100 on the last step.

Two things in the rail vary by step and both are handled by reserving their space
rather than by toggling them out of flow:

- The prediction block is revealed with `visibility`, so its 80px is always
  reserved and the fills panel below never moves.
- The `−` caption under the ladder is revealed the same way, because below 660px
  it wraps to a second line, and a caption that grows by a line when a fill lands
  moves everything under it.

Measured across every step of both runs at 1440, 1280, 1081, 1080, 1024, 900,
768, 700, 660, 480, 390 and 320px, the book panel, the selected panel, the fills
panel and the event tape each hold one position and one height, and nothing
clips. The selected panel measures 229px at 1280 and 182px at 900, which is the
same content needing less room in a wider box.

## Motion

Decoration on top of a redraw, never a substitute for one.
Every motion below is skipped when the reader prefers reduced motion, and the
reduced-motion block in the stylesheet stays as a second backstop.

| Motion | Duration | Says |
|---|---|---|
| The depth edge draws out from the price axis | 380ms | This much size is resting here now |
| The sweep stroke across the row depth left | 620ms | This much size went, at this price |
| The ghost of the previous profile fades | 520ms | This is what it was a moment ago |
| The history line grows, newest dot last | 380ms / 260ms | The replay reached this event |
| The event summary arrives | 200ms | Something changed in the rail |
| Fill receipts fade in, staggered 130ms | 240ms | These fills belong to this step |
| The touched price row washes | 700ms | This price is the one that moved |

Durations run from 130ms for a hover to 700ms for the price wash.
Nothing loops, because a looping animation on a data page is noise pretending to
be signal.

Two rules for adding more:

- It may only point at a value the trace already contains.
  A fill sweep drawn across a row the engine did not touch would be a lie the
  animation tells more loudly than the text does.
- The sweep and the ghost describe the *change*, not the new state, so both are
  skipped entirely under reduced motion rather than shown instantly.
  A shape that appears and vanishes without a fade is a flash, and the new
  profile, the `−` marks and the fills list already carry the information.
  The draw-in is likewise only replayed on a step, never on a resize, or the
  edges would flicker under a dragging window.

## What is deliberately not themed

- No dark mode. The page declares `color-scheme: light` and keeps a light canvas
  under a dark OS scheme, so the palette cannot half-apply.
- No webfont. The display weight is a system sans at 780, which needs no download
  and cannot shift the layout on arrival.
- No shadow. The faint page grid and the rules carry the depth; a drop shadow
  would add a second depth language to a flat ledger.
