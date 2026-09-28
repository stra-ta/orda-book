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
| `--graphite` | `#252c27` | The matching-rule band at the foot of the book |
| `--graphite-2` | `#3d453f` | The ask side, where the bid side would be leaf |
| `--refused` | `#7c5341` | A refused event's label and flag |
| `--refused-wash` | `#f6efe9` | A refused event's ground, and the load error |
| `--sans` | system stack | Everything read as words |
| `--mono` | system mono | Everything the engine counted |

Measured contrast, all AA or better for body text:
ink on panel 16.4:1, muted 6.6:1, subtle 5.0:1, leaf on panel 6.4:1,
white on leaf 6.4:1, leaf-deep on leaf-wash 7.2:1, rule label on graphite 6.9:1.

`subtle` on `leaf-wash` is 4.3:1, so leaf-wash is never used as the background
for `--subtle` text.
The prediction and the selected row both use `--leaf-deep` or `--ink` for that
reason.

## Type

Three sizes on any one screen, and two families.

- Display: the page title, heavy sans, tight tracking, one weight.
- Body: 12.5px to 17px sans. The event summary is the largest thing in the rail.
- Data: 11px to 14px mono, tabular numerals, for every price, size, id and count.
- Panel labels: 10px uppercase with 0.14em tracking, in `--subtle`.
  There are three or four on a screen and nothing else is uppercase.

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
status and the keyboard hint at the far end. It is 58px on a laptop and 93px
where the status wraps under the controls. Below 660px the slider takes its own
row and the keyboard hint is dropped, because there are no arrow keys on a
phone.

## Boxes that hold still

Card heights are fixed, not minimums, wherever the content varies by step:
the selected event panel at 253px, the fills panel at 148px, and the ladder rows
at a 66px floor.
A box that resizes while the reader is stepping is a box they watch instead of
the data.

Two things in the rail vary by step and both are handled by reserving their space
rather than by toggling them out of flow:

- The prediction block is revealed with `visibility`, so its 80px is always
  reserved and the fills panel below never moves.
- The `−` caption under the ladder is revealed the same way, because below 660px
  it wraps to a second line, and a caption that grows by a line when a fill lands
  moves everything under it.

Measured across every step of the default, modify and rejection traces, at
1440, 1280, 1080, 1024, 920, 900, 700, 480, 390 and 320px: the book panel, the
selected panel and the fills panel each hold one position and one height.

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
