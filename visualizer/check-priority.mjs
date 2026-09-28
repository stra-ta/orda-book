// Guards the lesson the page exists to teach: at one price the oldest order
// fills first, and a modify is a cancel and replace, so an order that is
// modified goes to the back of its queue even when its price and size do not
// change.
//
// Both traces are engine output and this file only reads them. What it adds is
// the comparison between the two runs, and an assertion that the panel's
// untouched controls still build exactly the scenarios on disk. Nothing here
// executes the engine; the CMake fixture tests do that.
//
// Not covered: that the browser wires its checkbox to modifyFirst. That is a
// DOM question and this file has no DOM.
//
// Run with: node visualizer/check-priority.mjs <default.json> <queue-position.json>

import { readFileSync } from "node:fs";
import { buildScenario } from "./trace.mjs";

const [defaultPath, queuePath] = process.argv.slice(2);
if (!defaultPath || !queuePath) {
  console.error("usage: node visualizer/check-priority.mjs <default.json> <queue-position.json>");
  process.exit(2);
}

const failures = [];
function check(condition, message) {
  if (!condition) failures.push(message);
}

const loadFrames = (path) => JSON.parse(readFileSync(path, "utf8")).frames;
const defaultFrames = loadFrames(defaultPath);
const queueFrames = loadFrames(queuePath);
const fillsOf = (frames) => frames[frames.length - 1].trades
  .map((trade) => `${trade.restingOrderId}:${trade.quantity}`);

// Everything below compares the default run with the same run plus one modify,
// so confirm that is actually the only difference between the two files.
const modifyStep = queueFrames.findIndex((frame) => frame.event?.kind === "modify");
check(modifyStep > 0, `${queuePath} contains no modify event, so it cannot explain the contrast`);
const withoutModify = queueFrames.filter((frame, index) => index !== modifyStep);
check(
  withoutModify.length === defaultFrames.length &&
  withoutModify.every((frame, index) =>
    JSON.stringify(frame.event) === JSON.stringify(defaultFrames[index].event)),
  `${queuePath} is not ${defaultPath} with a single modify inserted`,
);

// The regression this file was written for. Re-sending the second bid looks
// like the same event and produces the same fills, which teaches nothing.
check(
  fillsOf(defaultFrames).join(" ") !== fillsOf(queueFrames).join(" "),
  `the modify did not change the fill order (${fillsOf(queueFrames).join(" ")} in both files), ` +
  "so the toggle demonstrates nothing",
);

// The rule itself, checked against the resting snapshots rather than against a
// golden fill order, so the assertion states the rule and not one expected run.
function checkQueueDecidesFills(frames, label) {
  const matching = frames[frames.length - 1];
  const prior = frames[frames.length - 2];
  check(matching.trades.length > 0, `${label}: the last event produced no trades`);
  if (matching.trades.length === 0) return;

  const price = matching.trades[0].price;
  const restingSide = matching.trades[0].aggressorSide === "SELL" ? "bids" : "asks";
  const queue = prior[restingSide]
    .filter((order) => order.price === price)
    .map((order) => order.orderId);
  const consumed = [...new Set(matching.trades.map((trade) => trade.restingOrderId))];
  check(
    queue.length > 1,
    `${label}: only one resting order at ${price}, so there is no queue for the fill order to show`,
  );
  check(
    consumed.join(" ") === queue.join(" "),
    `${label}: filled ${consumed.join(" ")} at ${price}, but the queue was ${queue.join(" ")}`,
  );
  const filled = matching.trades.reduce((sum, trade) => sum + BigInt(trade.quantity), 0n);
  check(filled === 4n, `${label}: filled ${filled} units, expected the same 4 in both runs`);
  for (const trade of matching.trades) {
    check(trade.price === price, `${label}: a fill happened at ${trade.price}, not ${price}`);
  }
}

checkQueueDecidesFills(defaultFrames, defaultPath);
checkQueueDecidesFills(queueFrames, queuePath);

// The reversal has to come from queue position alone, or the toggle is
// demonstrating a size change or a price change instead.
if (modifyStep > 0) {
  const modifiedId = queueFrames[modifyStep].event.orderId;
  const before = queueFrames[modifyStep - 1].bids.find((order) => order.orderId === modifiedId);
  const after = queueFrames[modifyStep].bids.find((order) => order.orderId === modifiedId);
  check(Boolean(before && after), `order ${modifiedId} was not left resting by its own modify`);
  check(queueFrames[modifyStep].trades.length === 0, "the modify emitted a trade");
  if (before && after) {
    check(
      before.price === after.price && before.quantity === after.quantity,
      `the modify changed order ${modifiedId} from ${before.quantity}@${before.price} to ` +
      `${after.quantity}@${after.price}, so the reversal is not purely about queue position`,
    );
    const defaultQueue = defaultFrames[modifyStep - 1].bids
      .filter((order) => order.price === before.price)
      .map((order) => order.orderId);
    check(
      defaultQueue[0] === modifiedId,
      `order ${modifiedId} is not first in the default queue (${defaultQueue.join(" ")}), ` +
      "so moving it to the back changes nothing",
    );
  }
}

// The panel's untouched controls must build exactly the scenarios on disk.
// The fixture tests replay the files and this asserts the page still asks for
// them, which a fixture test cannot see.
const scenarioText = (relative) => readFileSync(new URL(relative, import.meta.url), "utf8")
  .split("\n")
  .filter((line) => line.trim() && !line.startsWith("#"))
  .join("\n") + "\n";
check(
  scenarioText("./scenarios/price-time-priority.events") === buildScenario(),
  "the panel's default controls no longer build scenarios/price-time-priority.events",
);
check(
  scenarioText("./scenarios/queue-position.events") === buildScenario({ modifyFirst: true }),
  "the panel's modify toggle no longer builds scenarios/queue-position.events",
);

if (failures.length > 0) {
  console.error(`${failures.length} priority check(s) failed:`);
  for (const failure of failures) console.error(`  - ${failure}`);
  process.exit(1);
}

console.log(
  `priority ok: ${defaultPath} fills ${fillsOf(defaultFrames).join(", ")}, ` +
  `${queuePath} fills ${fillsOf(queueFrames).join(", ")} once the older bid is modified`,
);
