# Order-by-order visualizer

This page steps through one price-time-priority example.
Its event-by-event book snapshots and trades come from `orda_replay --trace-json`, not a second matching implementation in JavaScript.
It is an explainer for the engine, not a market feed, and it never invents a timestamp: every axis is either price or event order.

A hosted copy is published at <https://stra-ta.github.io/orda-book/>.
It is this page in static mode, so the parameter panel is not there.

The queue is what the example turns on.
At one price the oldest order fills first, and a change is a cancel and replace, so a modified order goes to the back of its queue even when its price and size do not change.
Before a fill that has a genuine choice in it, the page asks you to predict which resting order fills first, and it only asks when more than one resting order is actually in the queue.

## The two charts

**Cumulative depth by price.** Each price row is filled out to the total size resting from the touch out to that price, not the size at that level alone.
Bids grow leftward from the best bid and asks grow rightward from the best ask, so the two profiles never meet in the middle.
The price rows between the touch on each side are the spread, which the panel heading states as a price gap.
Price is the vertical axis and is shared with the ladder, so a bar and the queue it belongs to are always on the same row.
The order ids live in their own columns either side of the chart, so a bar never covers its own queue, and the bands are trimmed so the value at the deepest tip has a gutter to sit in.

Bar length is scaled to the deepest depth anywhere in the replay and never rescales between steps.
That is the whole point: a book that just lost four units has to look thinner.
`check-depth.mjs` asserts that scale, so a rescaled or inflated axis fails the build.

**Cumulative depth by event.** A short step-line under the book showing how much size each side held after every event.
Its x axis spans the whole replay so it does not shift under the reader, but only the events already reached are drawn, so stepping does not hand over the answer early.

`trace.mjs` holds the arithmetic and nothing else, so the page and the test share one implementation.
Each bar's value stays in the DOM as `data-cumulative` and in the accessibility tree, because the visible label lives inside the SVG and screen readers skip that.

## Run it statically

The page falls back to the checked-in trace, which is what a static host gets.
The parameter panel stays hidden, because there is no engine to re-run the numbers.
`.github/workflows/pages.yml` publishes this folder to GitHub Pages on a push to `main` that touches it, minus the two check scripts and this README.

```sh
cmake -S . -B build-verify -DCMAKE_BUILD_TYPE=Debug
cmake --build build-verify --target orda_replay --parallel
python3 -m http.server 8000 --directory visualizer
```

Open <http://localhost:8000>.
The browser needs HTTP for the trace file and the ES module.
No build step, no dependencies.

## Run it with adjustable numbers

`scripts/serve_visualizer.py` adds one endpoint on top of the static server:

```
GET /api/trace?events=<url-encoded event text>
```

It writes the text to a temporary file, runs `orda_replay --trace-json` on it, and returns the engine's JSON.
With that endpoint reachable the page reveals the parameter panel, and every change is replayed by the real matcher: sizes, the second bid's price, the sell's order type, and a toggle that modifies the older bid before the sell arrives.
That last one is the counterfactual the page exists for.
Re-sending order 1 at the same price and size moves it behind order 2, so the same four units fill `2:2` then `1:2` instead of `1:3` then `2:1`.

```sh
cmake --build build-verify --target orda_replay --parallel
python3 scripts/serve_visualizer.py
```

Dev only. It binds to loopback, caps the scenario size, runs one fixed binary with no shell, and writes only to its own temporary directory.
Do not expose the server.
The page itself is safe to host statically, which is what the Pages workflow above does.
The default parameter values reproduce `scenarios/price-time-priority.events` exactly, so the first paint matches the checked-in fixture.

## Refused orders

An order the engine rejects is part of the story, so a trace frame records it: `accepted: false`, the error name, no trades, and the book exactly as it was.
`scenarios/rejections.events` covers a duplicate id, a crossing post-only, and a fill-or-kill with too little resting size.
The page marks those rows `refused` and explains the reason in the detail panel.

The export only fails for a parse error or the 500-event cap, never for a rejected event.
Both fields were already declared on every frame in the `replay-trace/v1` format, and no published trace predates it, so recording a refusal adds no new field and the version stays at `v1`.

## Refresh the example

After changing a scenario or the matcher, regenerate the trace from the engine:

```sh
cmake --build build-verify --target orda_replay --parallel
for scenario in price-time-priority rejections queue-position; do
  ./build-verify/orda_replay --trace-json "visualizer/scenarios/$scenario.events" > "visualizer/data/$scenario.json"
done
ctest --test-dir build-verify --output-on-failure
```

Six tests cover this page, and a seventh checks that the page's script parses.
`visualizer_trace_fixture`, `visualizer_rejection_fixture`, and `visualizer_queue_position_fixture` replay their source events and compare every exported frame with the checked-in data, and each checks that exporting more than 500 events fails instead of writing a partial trace.
`visualizer_depth_chart` recomputes both charts from the trace by a different route, a filtered sum instead of the module's outward walk, and checks that every refused event left the book and the trade log alone.
`visualizer_priority_order` asserts the lesson nothing else can: the same four units fill `1:3, 2:1` by default and `2:2, 1:2` once the older bid is modified, that the modify changed no price or size, that the fills consume the queue in order, and that the panel's default controls still build the checked scenarios.
`visualizer_script_syntax` runs `node --check` on `app.js`, which nothing else in the suite parses.
That check is only meaningful because of `package.json` in this folder, which marks it as ES modules: without it, `node --check` reads `app.js` as CommonJS, fails on the first `import`, and reports success for any error after it.
The three Node checks are skipped when Node is absent, so a C++-only runner still passes.
Run them on their own with:

```sh
node visualizer/check-depth.mjs visualizer/data/price-time-priority.json visualizer/data/rejections.json
node visualizer/check-priority.mjs visualizer/data/price-time-priority.json visualizer/data/queue-position.json
```

Order IDs, prices, and quantities use decimal strings in JSON so browsers do not round 64-bit integers.
Trace export is capped at 500 events to keep snapshots bounded.
