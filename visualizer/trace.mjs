// Read-only helpers over an orda-book replay trace. Nothing here touches the
// DOM, so the page and the CTest that guards the depth chart share one
// implementation of the arithmetic.

export function integerText(value, label) {
  if (typeof value !== "string" || !/^-?\d+$/.test(value)) {
    throw new Error(`The replay has an invalid ${label}.`);
  }
  return BigInt(value);
}

export function groupOrders(orders) {
  const groups = new Map();
  for (const order of orders) {
    const group = groups.get(order.price) ?? { orders: [], total: 0n };
    group.orders.push(order);
    group.total += integerText(order.quantity, "order quantity");
    groups.set(order.price, group);
  }
  return groups;
}

export function totalQuantity(group) {
  return group?.total ?? 0n;
}

export function priceOrder(descending) {
  return (left, right) => {
    const a = integerText(left, "price");
    const b = integerText(right, "price");
    if (a === b) return 0;
    return descending ? (a > b ? -1 : 1) : (a < b ? -1 : 1);
  };
}

// Running total of size outward from the touch: bids from the best bid down,
// asks from the best ask up. One entry per resting price level.
export function cumulativeDepth(orders, descending) {
  const groups = groupOrders(orders);
  let running = 0n;
  const cumulative = new Map();
  for (const price of [...groups.keys()].sort(priceOrder(descending))) {
    running += totalQuantity(groups.get(price));
    cumulative.set(price, running);
  }
  return cumulative;
}

// Cumulative depth for every price row in the replay. Rows between a side's
// best and worst price carry the last known total, so a price with no orders
// of its own does not break the profile. Rows outside that range are null,
// meaning that side has no depth there at all.
export function depthProfile(orders, descending, priceLevels) {
  const cumulative = cumulativeDepth(orders, descending);
  const profile = new Array(priceLevels.length).fill(null);
  if (cumulative.size === 0) return profile;

  const walked = [...cumulative.keys()];
  const first = priceLevels.indexOf(walked[0]);
  const last = priceLevels.indexOf(walked[walked.length - 1]);
  const step = descending ? 1 : -1;
  let carried = null;
  for (let index = first; index !== last + step; index += step) {
    const price = priceLevels[index];
    if (cumulative.has(price)) carried = cumulative.get(price);
    profile[index] = carried;
  }
  return profile;
}

// Price rows and depth scale for the whole replay. The scale is stable across
// steps on purpose: if it rescaled, every step would look the same and a
// book that got thinner would look unchanged.
export function priceScale(frames) {
  const prices = new Set();
  let maximumCumulativeDepth = 0n;

  for (const frame of frames) {
    for (const order of [...frame.bids, ...frame.asks]) prices.add(order.price);
    for (const [orders, descending] of [[frame.bids, true], [frame.asks, false]]) {
      for (const value of cumulativeDepth(orders, descending).values()) {
        if (value > maximumCumulativeDepth) maximumCumulativeDepth = value;
      }
    }
  }

  return { priceLevels: [...prices].sort(priceOrder(true)), maximumCumulativeDepth };
}

// Deepest cumulative depth on each side after every event. The x axis of the
// history chart is this list's index, which is event order and not a clock.
export function depthByEvent(frames, priceLevels) {
  const bid = [];
  const ask = [];
  for (const frame of frames) {
    for (const [orders, descending, series] of [
      [frame.bids, true, bid],
      [frame.asks, false, ask],
    ]) {
      let deepest = 0n;
      for (const value of depthProfile(orders, descending, priceLevels).values()) {
        if (value !== null && value > deepest) deepest = value;
      }
      series.push(deepest);
    }
  }
  return { bid, ask };
}

// The event text the parameter panel sends to the engine. Order 1 and order 2
// share a price by default so the price-time lesson is the one on screen.
export function buildScenario({
  buy1Size = 3n,
  buy2Size = 2n,
  buy2Price = 100n,
  sellPrice = 100n,
  sellSize = 4n,
  sellType = "LIMIT",
  modifyFirst = false,
} = {}) {
  const lines = [
    "ADD 1 BUY 100 " + buy1Size,
    `ADD 2 BUY ${buy2Price} ${buy2Size}`,
    "ADD 3 BUY 99 2",
  ];
  // A change is a cancel and replace, so re-sending the older bid with the same
  // price and size still sends it to the back of its queue. Modifying the
  // second bid would look like the same event and teach nothing, because order
  // 2 is already behind order 1.
  if (modifyFirst) {
    lines.push(`MODIFY 1 100 ${buy1Size}`);
  }
  lines.push("ADD 4 SELL 101 3", "ADD 5 SELL 102 2");
  if (sellType === "MARKET") {
    lines.push(`ADD 6 SELL MARKET ${sellSize}`);
  } else if (sellType === "LIMIT") {
    // Plain limit is the parser's default, so the default scenario text stays
    // byte-identical to visualizer/scenarios/price-time-priority.events.
    lines.push(`ADD 6 SELL ${sellPrice} ${sellSize}`);
  } else {
    lines.push(`ADD 6 SELL ${sellPrice} ${sellSize} ${sellType}`);
  }
  return lines.join("\n") + "\n";
}
