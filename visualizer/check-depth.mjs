// Guards the depth chart against the failure this project cares about most: a
// graph that looks right and states something the engine never said.
//
// The page and this test share visualizer/trace.mjs for the arithmetic. The
// expectations below are derived here from the raw trace arrays by a different
// route, a filtered sum rather than the module's outward walk, so a bug in the
// shared module cannot hide by being copied into the assertion.
//
// Run with: node visualizer/check-depth.mjs <trace.json>

import { readFileSync } from "node:fs";
import { depthByEvent, depthProfile, priceScale } from "./trace.mjs";

const tracePath = process.argv[2];
if (!tracePath) {
  console.error("usage: node check-depth.mjs <trace.json> [rejections.json]");
  process.exit(2);
}

const frames = JSON.parse(readFileSync(tracePath, "utf8")).frames;
const { priceLevels, maximumCumulativeDepth } = priceScale(frames);
const failures = [];

function check(condition, message) {
  if (!condition) failures.push(message);
}

// Depth at a price, defined directly: everything resting at that price or
// better, and nothing at all outside the side's own price range.
function expectedAt(orders, price, descending) {
  if (orders.length === 0) return null;
  const row = BigInt(price);
  let low = null;
  let high = null;
  for (const order of orders) {
    const resting = BigInt(order.price);
    if (low === null || resting < low) low = resting;
    if (high === null || resting > high) high = resting;
  }
  if (row < low || row > high) return null;
  let sum = 0n;
  for (const order of orders) {
    const resting = BigInt(order.price);
    const countsTowardDepth = descending ? resting >= row : resting <= row;
    if (countsTowardDepth) sum += BigInt(order.quantity);
  }
  return sum;
}

check(priceLevels.length > 0, "no price levels were derived from the trace");
check(
  maximumCumulativeDepth > 0n,
  "the replay-wide depth scale is zero, so every bar would be empty",
);
let checkedCells = 0;
let trueScale = 0n;
for (const frame of frames) {
  for (const [orders, descending, label] of [
    [frame.bids, true, "bid"],
    [frame.asks, false, "ask"],
  ]) {
    const actual = depthProfile(orders, descending, priceLevels);
    for (const [index, price] of priceLevels.entries()) {
      const got = actual[index];
      const want = expectedAt(orders, price, descending);
      check(got === want, `step ${frame.step} ${label} @ ${price}: drew ${got}, want ${want}`);
      if (want !== null) {
        checkedCells += 1;
        if (want > trueScale) trueScale = want;
      }
    }

    // The deepest point of a profile must equal everything resting on that side.
    // A side with nothing resting has no depth point at all, which reads as 0.
    const sideTotal = orders.reduce((sum, order) => sum + BigInt(order.quantity), 0n);
    const deepest = actual.reduce((best, value) => {
      if (value === null) return best;
      return best === null || value > best ? value : best;
    }, null) ?? 0n;
    check(
      deepest === sideTotal,
      `step ${frame.step} ${label}: deepest profile point ${deepest} != resting total ${sideTotal}`,
    );
  }
}

// The bar scale must be the deepest depth anywhere in the replay, not a
// round number and not a per-step value. A scale that moves or inflates makes
// a book that just lost 4 units look unchanged, which is the whole chart.
check(
  maximumCumulativeDepth === trueScale,
  `the depth axis is scaled to ${maximumCumulativeDepth}, but the deepest depth in the replay is ${trueScale}`,
);

// The lesson the page exists to teach: a sell of 4 into the bids must pull 4
// units of cumulative bid depth out of the book, and must not touch the asks.
const bidDepth = (frame) => depthProfile(frame.bids, true, priceLevels)
  .reduce((best, value) => (value !== null && (best === null || value > best) ? value : best), null) ?? 0n;
const askDepth = (frame) => depthProfile(frame.asks, false, priceLevels)
  .reduce((best, value) => (value !== null && (best === null || value > best) ? value : best), null) ?? 0n;

const lastFill = frames.length - 1;
const before = bidDepth(frames[lastFill - 1]);
const after = bidDepth(frames[lastFill]);
check(
  before - after === 4n,
  `final step: bid depth fell by ${before} to ${after}, expected a fall of 4`,
);
check(
  askDepth(frames[lastFill - 1]) === askDepth(frames[lastFill]),
  "final step: the ask side moved, but resting asks cannot change",
);

// The history chart plots one point per event, so it has to agree with the
// ladder at every step rather than only at the end.
const history = depthByEvent(frames, priceLevels);
check(
  history.bid.length === frames.length && history.ask.length === frames.length,
  `the history chart has ${history.bid.length} points for ${frames.length} steps`,
);
for (const [index, frame] of frames.entries()) {
  check(
    history.bid[index] === bidDepth(frame) && history.ask[index] === askDepth(frame),
    `step ${index}: history chart says bid ${history.bid[index]} ask ${history.ask[index]}, ` +
    `ladder says bid ${bidDepth(frame)} ask ${askDepth(frame)}`,
  );
}

// A refused event must leave the book exactly as it was. Supplied as a second
// argument because the default scenario has nothing to refuse.
const rejectionPath = process.argv[3];
if (rejectionPath) {
  const rejected = JSON.parse(readFileSync(rejectionPath, "utf8")).frames;
  const refusals = rejected.filter((frame) => frame.accepted === false);
  check(refusals.length > 0, `${rejectionPath} contains no refused events to check`);
  for (const frame of refusals) {
    const before = rejected[frame.step - 1];
    const sameBook = frame.bids.length === before.bids.length &&
      frame.asks.length === before.asks.length &&
      frame.bids.every((order, index) =>
        order.orderId === before.bids[index].orderId &&
        order.price === before.bids[index].price &&
        order.quantity === before.bids[index].quantity) &&
      frame.asks.every((order, index) =>
        order.orderId === before.asks[index].orderId &&
        order.price === before.asks[index].price &&
        order.quantity === before.asks[index].quantity);
    check(sameBook, `refused step ${frame.step} (${frame.error}) changed the book`);
    check(frame.trades.length === 0, `refused step ${frame.step} (${frame.error}) emitted a trade`);
    check(typeof frame.error === "string" && frame.error !== "none",
      `refused step ${frame.step} carries no error string`);
  }
}

if (failures.length > 0) {
  console.error(`${failures.length} depth check(s) failed:`);
  for (const failure of failures) console.error(`  - ${failure}`);
  process.exit(1);
}

console.log(
  `depth ok: ${checkedCells} cells across ${frames.length} steps, ` +
  `scale ${maximumCumulativeDepth}, bid depth ${before} to ${after}` +
  (rejectionPath ? `, ${JSON.parse(readFileSync(rejectionPath, "utf8")).frames.filter((f) => !f.accepted).length} refusals held` : ""),
);
