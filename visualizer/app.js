import { buildScenario, depthByEvent, depthProfile, groupOrders, integerText, priceScale } from "./trace.mjs";

const eventList = document.querySelector("#event-list");
const eventCount = document.querySelector("#event-count");
const selectedTitle = document.querySelector("#selected-title");
const eventSummary = document.querySelector("#event-summary");
const eventExplanation = document.querySelector("#event-explanation");
const prediction = document.querySelector("#prediction");
const stepNumber = document.querySelector("#step-number");
const bestPrices = document.querySelector("#best-prices");
const ladderWrap = document.querySelector("#ladder-wrap");
const priceLadder = document.querySelector("#price-ladder");
const depthChart = document.querySelector("#depth-chart");
const depthHistoryChart = document.querySelector("#depth-history-chart");
const bidAxis = document.querySelector("#bid-axis");
const askAxis = document.querySelector("#ask-axis");
const tradeList = document.querySelector("#trade-list");
const emptyTrades = document.querySelector("#empty-trades");
const previousButton = document.querySelector("#previous-button");
const playButton = document.querySelector("#play-button");
const nextButton = document.querySelector("#next-button");
const stepSlider = document.querySelector("#step-slider");
const playbackStatus = document.querySelector("#playback-status");
const loadError = document.querySelector("#load-error");
const workbench = document.querySelector(".workbench");
const parameterPanel = document.querySelector("#parameter-panel");

const parameterFields = {
  buy1Size: document.querySelector("#param-buy1-size"),
  buy2Size: document.querySelector("#param-buy2-size"),
  buy2Price: document.querySelector("#param-buy2-price"),
  sellPrice: document.querySelector("#param-sell-price"),
  sellSize: document.querySelector("#param-sell-size"),
  sellType: document.querySelector("#param-sell-type"),
  modifyFirst: document.querySelector("#param-modify"),
};

const SVG_NS = "http://www.w3.org/2000/svg";

let frames = [];
let currentStep = 0;
let playbackTimer = null;
let priceLevels = [];
let maximumCumulativeDepth = 0n;
let history = { bid: [], ask: [] };
let liveMode = false;
let requestSerial = 0;
let parameterTimer = null;
let ghostTimer = null;

function validateTrace(trace) {
  if (trace?.schema !== "orda-book/replay-trace/v1" || !Array.isArray(trace.frames)) {
    throw new Error("The replay file uses an unsupported format.");
  }
  if (trace.frames.length < 1 || trace.frames.length > 501) {
    throw new Error("The replay must contain between 1 and 500 events.");
  }

  for (const [index, frame] of trace.frames.entries()) {
    if (frame?.step !== index || !Array.isArray(frame.bids) || !Array.isArray(frame.asks) ||
        !Array.isArray(frame.trades) || typeof frame.accepted !== "boolean") {
      throw new Error(`Replay step ${index} is incomplete.`);
    }
    for (const order of [...frame.bids, ...frame.asks]) {
      integerText(order?.orderId, "order ID");
      if (integerText(order.price, "order price") <= 0n || integerText(order.quantity, "order quantity") <= 0n) {
        throw new Error(`Replay step ${index} contains a non-positive order.`);
      }
    }
    for (const trade of frame.trades) {
      integerText(trade?.restingOrderId, "resting order ID");
      integerText(trade?.incomingOrderId, "incoming order ID");
      if (integerText(trade.price, "trade price") <= 0n || integerText(trade.quantity, "trade quantity") <= 0n ||
          !["BUY", "SELL"].includes(trade.aggressorSide)) {
        throw new Error(`Replay step ${index} contains an invalid trade.`);
      }
    }
    if (index === 0 && frame.event !== null) {
      throw new Error("The first replay step must be the empty book.");
    }
    if (index > 0 && (!frame.event || !["add", "cancel", "modify"].includes(frame.event.kind))) {
      throw new Error(`Replay step ${index} has no recognized event.`);
    }
  }
  return trace.frames;
}

function node(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

function svgNode(tag, className) {
  const element = document.createElementNS(SVG_NS, tag);
  if (className) element.setAttribute("class", className);
  return element;
}

function svgPath(className, d) {
  const element = svgNode("path", className);
  element.setAttribute("d", d);
  return element;
}

function sideName(side) {
  return side === "BUY" ? "Buy" : "Sell";
}

function eventLabel(event) {
  if (!event) return "Empty book";
  if (event.kind === "add") {
    // A market order has no price, so the engine's 0 placeholder never shows.
    const at = event.orderType === "MARKET" ? "" : ` @ ${event.price}`;
    return `${sideName(event.side)} ${event.orderType === "MARKET" ? "market " : ""}${event.quantity}${at}`;
  }
  if (event.kind === "cancel") return `Cancel order #${event.orderId}`;
  return `Modify #${event.orderId} to ${event.quantity} @ ${event.price}`;
}

function eventHeadline(frame) {
  if (frame.accepted === false) return "Rejected by the engine";
  const event = frame.event;
  if (!event) return "Starting position";
  if (event.kind === "add") {
    const at = event.orderType === "MARKET" ? "at the best price" : `at ${event.price}`;
    return `${sideName(event.side)} ${event.quantity} ${at}`;
  }
  if (event.kind === "cancel") return `Cancel order #${event.orderId}`;
  return `Modify order #${event.orderId}`;
}

const rejectionReason = {
  duplicate_order_id: "That order id is already resting in the book.",
  unknown_order_id: "No resting order has that id.",
  invalid_price: "The price has to be a positive integer.",
  invalid_quantity: "The quantity has to be a positive integer.",
  quantity_overflow: "The book already holds that much size at once.",
  capacity_exceeded: "The engine is at its reserved order capacity.",
  price_out_of_range: "That price is outside the engine's configured range.",
  insufficient_liquidity: "Not enough resting size to fill the whole order.",
  would_take_liquidity: "Post-only may not trade on arrival, and this order would.",
};

function tradeQuantity(trades) {
  return trades.reduce((total, trade) => total + integerText(trade.quantity, "trade quantity"), 0n);
}

function findOrder(frame, orderId) {
  return [...frame.bids, ...frame.asks].find((order) => order.orderId === orderId);
}

function prepareBookScale() {
  ({ priceLevels, maximumCumulativeDepth } = priceScale(frames));
  history = depthByEvent(frames, priceLevels);
}

function renderAxis() {
  for (const axis of [bidAxis, askAxis]) {
    axis.textContent = maximumCumulativeDepth.toString();
  }
}

function describeEvent(frame) {
  if (frame.accepted === false) {
    return [
      `The engine refused order #${frame.event.orderId}.`,
      rejectionReason[frame.error] ?? `The engine reported ${frame.error}.`,
    ];
  }

  const event = frame.event;
  if (!event) {
    return [
      "The book starts empty. Move through the list to see orders queue and match.",
      "A resting order waits until a new order can trade with it.",
    ];
  }

  if (event.kind === "cancel") {
    return [
      `Order #${event.orderId} was removed from the book.`,
      "Canceling an order does not create a trade.",
    ];
  }

  if (event.kind === "modify") {
    const changedOrder = findOrder(frame, event.orderId);
    const previousFrame = frames[frame.step - 1];
    const previousOrder = previousFrame ? findOrder(previousFrame, event.orderId) : undefined;
    const samePrice = previousOrder?.price === event.price;
    const sameSize = previousOrder?.quantity === event.quantity;
    const summary = frame.trades.length
      ? `Order #${event.orderId} changed, then matched ${tradeQuantity(frame.trades)} units.`
      : samePrice && sameSize
        ? `Order #${event.orderId} was re-sent with the same price and size.`
        : `Order #${event.orderId} moved to ${event.quantity} at ${event.price}.`;
    // Re-sending an order at its own price is the clearest case of the rule:
    // nothing about the order changed except its place in the queue.
    const explanation = !changedOrder
      ? "The modified order did not leave a resting remainder."
      : samePrice
        ? "A change is a cancel and replace, so the order went to the back of its price queue rather than keeping its place."
        : "A modified order joins the back of its new price queue.";
    return [summary, explanation];
  }

  if (frame.trades.length === 0) {
    const rests = findOrder(frame, event.orderId);
    const previousFrame = frames[frame.step - 1];
    const previousOrders = event.side === "BUY" ? previousFrame.bids : previousFrame.asks;
    const olderOrder = previousOrders.find((order) => order.price === event.price);
    if (rests && olderOrder) {
      return [
        `${sideName(event.side)} order #${event.orderId} joined behind #${olderOrder.orderId} at ${event.price}.`,
        `The older ${sideName(event.side).toLowerCase()} keeps its place at this price.`,
      ];
    }
    return [
      `${sideName(event.side)} order #${event.orderId} joined at ${event.price}.`,
      rests ? "No opposite order could trade at this price, so the order waits in the book." : "No compatible resting order was available. Nothing was added to the book.",
    ];
  }

  const total = tradeQuantity(frame.trades);
  const remainder = findOrder(frame, event.orderId);
  const summary = remainder
    ? `${sideName(event.side)} order #${event.orderId} traded ${total}; ${remainder.quantity} remain at ${remainder.price}.`
    : `${sideName(event.side)} order #${event.orderId} traded ${total} across ${frame.trades.length} fill${frame.trades.length === 1 ? "" : "s"}.`;
  return [summary, "The order crossed the best available price. At one price, arrival order decides who fills first."];
}

function renderSideCell(group, cumulative, side) {
  const cell = node("div", `side-cell ${side}-side`);
  cell.setAttribute("role", "cell");
  if (cumulative !== null) {
    // The drawn value stays in the DOM and the accessibility tree, because the
    // visible label lives inside the chart and screen readers skip that.
    const unit = cumulative === 1n ? "unit" : "units";
    const label = node("span", "depth-value", `${cumulative} ${unit} from the touch`);
    label.dataset.cumulative = cumulative.toString();
    label.dataset.max = maximumCumulativeDepth.toString();
    cell.append(label);
  }
  cell.append(node("span", "order-lines", group
    ? group.orders.map((order) => `#${order.orderId} · ${order.quantity}`).join("   ")
    : ""));
  return cell;
}

// Read from the stylesheet so the band and the axis label cannot drift apart.
function chartBand() {
  const declared = getComputedStyle(document.documentElement).getPropertyValue("--chart-span");
  const percent = Number.parseFloat(declared);
  return Number.isFinite(percent) && percent > 0 ? percent / 100 : 0.76;
}

function chartGeometry() {
  const ladder = priceLadder.getBoundingClientRect();
  const wrap = ladderWrap.getBoundingClientRect();
  const priceCell = priceLadder.querySelector(".price-cell");
  const buyBand = priceLadder.querySelector(".chart-cell-buy")?.getBoundingClientRect();
  const askBand = priceLadder.querySelector(".chart-cell-ask")?.getBoundingClientRect();
  const offsetX = ladder.left - wrap.left;
  const priceLeft = priceCell ? priceCell.getBoundingClientRect().left - wrap.left : ladder.width / 3;
  const priceRight = priceCell ? priceCell.getBoundingClientRect().right - wrap.left : (ladder.width * 2) / 3;
  const band = chartBand();
  // Each bar runs from the price column out to the outer edge of its own chart
  // column, which is where the order text starts.
  return {
    offsetX,
    width: ladder.width,
    height: ladder.height,
    priceLeft,
    priceRight,
    rowHeight: priceLevels.length > 0 ? ladder.height / priceLevels.length : 0,
    bidSpan: Math.max(0, priceLeft - (buyBand ? buyBand.left - wrap.left : 0)) * band,
    askSpan: Math.max(0, (askBand ? askBand.right - wrap.left : ladder.width) - priceRight) * band,
  };
}

function profileOutline(profile, rowHeight, xOf, axisX) {
  const points = [];
  const tips = [];
  let firstY = null;
  let lastY = 0;
  for (const [index, value] of profile.entries()) {
    if (value === null) continue;
    const x = xOf(value);
    const top = index * rowHeight;
    const bottom = top + rowHeight;
    if (firstY === null) firstY = top;
    points.push([x, top], [x, bottom]);
    tips.push({ x, y: top + rowHeight / 2, value });
    lastY = bottom;
  }
  if (points.length === 0) return null;
  const trace = points.map(([x, y]) => `L ${x.toFixed(2)} ${y.toFixed(2)}`).join(" ");
  return {
    edge: `M ${axisX.toFixed(2)} ${firstY.toFixed(2)} ${trace}`,
    area: `M ${axisX.toFixed(2)} ${firstY.toFixed(2)} ${trace} L ${axisX.toFixed(2)} ${lastY.toFixed(2)} Z`,
    tips,
  };
}

function drawProfileSide(group, profile, side, geometry, withLabels) {
  const bid = side === "buy";
  const axisX = bid ? geometry.priceLeft : geometry.priceRight;
  const span = bid ? geometry.bidSpan : geometry.askSpan;
  const xOf = (value) => axisX +
    (bid ? -1 : 1) * (Number((value * 1000n) / maximumCumulativeDepth) / 1000) * span;
  const outline = profileOutline(profile, geometry.rowHeight, xOf, axisX);
  if (!outline) return;

  group.append(svgPath(`depth-area depth-area-${side}`, outline.area));
  group.append(svgPath(`depth-edge depth-edge-${side}`, outline.edge));
  if (!withLabels) return;

  for (const tip of outline.tips) {
    const text = svgNode("text", "depth-tip");
    text.textContent = tip.value.toString();
    text.setAttribute("x", (bid ? tip.x - 4 : tip.x + 4).toFixed(2));
    text.setAttribute("y", (tip.y + 3).toFixed(2));
    text.setAttribute("text-anchor", bid ? "end" : "start");
    group.append(text);
  }
}

function renderDepthChart(previousFrame, animate) {
  const frame = frames[currentStep];
  const geometry = chartGeometry();
  depthChart.replaceChildren();
  if (geometry.width === 0 || geometry.height === 0) return;
  depthChart.setAttribute("viewBox", `0 0 ${geometry.width} ${geometry.height}`);

  const current = svgNode("g");
  if (maximumCumulativeDepth > 0n) {
    for (const x of [geometry.priceLeft - geometry.bidSpan, geometry.priceRight + geometry.askSpan]) {
      const line = svgNode("line", "depth-max-line");
      line.setAttribute("x1", x.toFixed(2));
      line.setAttribute("x2", x.toFixed(2));
      line.setAttribute("y1", "0");
      line.setAttribute("y2", geometry.height.toFixed(2));
      current.append(line);
    }
  }

  const bidProfile = depthProfile(frame.bids, true, priceLevels);
  const askProfile = depthProfile(frame.asks, false, priceLevels);
  drawProfileSide(current, bidProfile, "buy", geometry, true);
  drawProfileSide(current, askProfile, "ask", geometry, true);
  depthChart.append(current);

  if (!animate || !previousFrame) return;
  const previousBid = depthProfile(previousFrame.bids, true, priceLevels);
  const previousAsk = depthProfile(previousFrame.asks, false, priceLevels);
  const hasPrevious = previousBid.some((value) => value !== null) || previousAsk.some((value) => value !== null);
  if (!hasPrevious) return;

  // The old profile fades out beside the new one. Path data cannot be
  // transitioned when a side's row range changes, and seeing both shapes at
  // once says more than a morph would.
  const ghost = svgNode("g", "depth-ghost is-visible");
  drawProfileSide(ghost, previousBid, "buy", geometry, false);
  drawProfileSide(ghost, previousAsk, "ask", geometry, false);
  depthChart.append(ghost);
  window.clearTimeout(ghostTimer);
  ghostTimer = window.setTimeout(() => ghost.remove(), 620);
}

function renderHistory() {
  depthHistoryChart.replaceChildren();
  const width = depthHistoryChart.getBoundingClientRect().width || ladderWrap.getBoundingClientRect().width;
  const height = 64;
  if (width === 0) return;
  depthHistoryChart.setAttribute("viewBox", `0 0 ${width} ${height}`);

  const padX = 6;
  const padTop = 9;
  const padBottom = 9;
  const count = history.bid.length;
  // The x axis spans the whole replay so it never moves under the reader, but
  // only the events already reached are drawn. Stepping through should not
  // hand over the answer before you get there.
  const shown = Math.min(currentStep + 1, count);
  const xOf = (index) => (count <= 1 ? padX : padX + (index * (width - 2 * padX)) / (count - 1));
  const yOf = (value) => (height - padBottom) -
    (maximumCumulativeDepth === 0n ? 0 : (Number((value * 1000n) / maximumCumulativeDepth) / 1000) * (height - padTop - padBottom));

  if (shown > 0) {
    for (const [values, side] of [[history.bid, "bid"], [history.ask, "ask"]]) {
      let path = `M ${xOf(0).toFixed(2)} ${yOf(values[0]).toFixed(2)}`;
      for (let index = 0; index < shown - 1; index += 1) {
        path += ` L ${xOf(index).toFixed(2)} ${yOf(values[index + 1]).toFixed(2)}`;
        path += ` L ${xOf(index + 1).toFixed(2)} ${yOf(values[index + 1]).toFixed(2)}`;
      }
      depthHistoryChart.append(svgPath(`history-line history-line-${side}`, path));
      for (let index = 0; index < shown; index += 1) {
        const dot = svgNode("circle", `history-dot history-dot-${side}`);
        dot.setAttribute("cx", xOf(index).toFixed(2));
        dot.setAttribute("cy", yOf(values[index]).toFixed(2));
        dot.setAttribute("r", "1.9");
        depthHistoryChart.append(dot);
      }
    }
  }

  const cursor = svgNode("line", "history-cursor");
  cursor.setAttribute("x1", xOf(currentStep).toFixed(2));
  cursor.setAttribute("x2", xOf(currentStep).toFixed(2));
  cursor.setAttribute("y1", String(padTop - 4));
  cursor.setAttribute("y2", String(height - padBottom + 4));
  depthHistoryChart.append(cursor);

  const bidNow = history.bid[currentStep] ?? 0n;
  const askNow = history.ask[currentStep] ?? 0n;
  depthHistoryChart.setAttribute(
    "aria-label",
    `Cumulative depth by event, up to event ${currentStep} of ${count - 1}. ` +
    `Bids hold ${bidNow} and asks hold ${askNow}, against a maximum of ${maximumCumulativeDepth}.`);
}

function renderBook(frame, previousFrame, animate) {
  const bids = groupOrders(frame.bids);
  const asks = groupOrders(frame.asks);

  priceLadder.replaceChildren();

  const touchedPrices = new Set(frame.trades.map((trade) => trade.price));
  if (frame.accepted !== false && (frame.event?.kind === "add" || frame.event?.kind === "modify")) {
    touchedPrices.add(frame.event.price);
  }
  const previousOrder = frame.event ? findOrder(previousFrame ?? { bids: [], asks: [] }, frame.event.orderId) : undefined;
  if (previousOrder) touchedPrices.add(previousOrder.price);

  const bidProfile = depthProfile(frame.bids, true, priceLevels);
  const askProfile = depthProfile(frame.asks, false, priceLevels);

  for (const [index, price] of priceLevels.entries()) {
    const row = node("div", "book-row");
    row.setAttribute("role", "row");
    if (animate && touchedPrices.has(price)) row.classList.add("is-touched");
    const buyCell = renderSideCell(bids.get(price), bidProfile[index], "buy");
    const bidBand = node("span", "chart-cell chart-cell-buy");
    bidBand.setAttribute("aria-hidden", "true");
    const priceCell = node("span", "price-cell", price);
    priceCell.setAttribute("role", "cell");
    const askBand = node("span", "chart-cell chart-cell-ask");
    askBand.setAttribute("aria-hidden", "true");
    const sellCell = renderSideCell(asks.get(price), askProfile[index], "sell");
    row.append(buyCell, bidBand, priceCell, askBand, sellCell);
    priceLadder.append(row);
  }

  if (frame.bids.length === 0 && frame.asks.length === 0) {
    bestPrices.textContent = "No resting orders";
  } else {
    const bidPrices = [...bids.keys()].sort((a, b) => integerText(a, "price") > integerText(b, "price") ? -1 : 1);
    const askPrices = [...asks.keys()].sort((a, b) => integerText(a, "price") < integerText(b, "price") ? -1 : 1);
    const bestBid = bidPrices[0];
    const bestAsk = askPrices[0];
    const bidText = bestBid === undefined ? "Best bid ·" : `Best bid ${bestBid}`;
    const askText = bestAsk === undefined ? "Best ask ·" : `Best ask ${bestAsk}`;
    const gapText = bestBid !== undefined && bestAsk !== undefined
      ? ` · gap ${(integerText(bestAsk, "price") - integerText(bestBid, "price")).toString()}`
      : "";
    bestPrices.textContent = `${bidText} · ${askText}${gapText}`;
  }

  renderDepthChart(previousFrame, animate);
  renderHistory();
}

function renderTrades(frame, animate) {
  tradeList.replaceChildren();
  tradeList.classList.toggle("is-animating", animate && frame.trades.length > 0);
  emptyTrades.hidden = frame.trades.length > 0;
  for (const [index, trade] of frame.trades.entries()) {
    const item = node("li", "trade-item");
    if (animate) item.style.animationDelay = `${460 + index * 130}ms`;
    const restingSide = trade.aggressorSide === "BUY" ? "sell" : "buy";
    item.append(
      node("span", "trade-index", String(index + 1).padStart(2, "0")),
      node("span", "trade-counterparty", `Against ${restingSide} #${trade.restingOrderId}`),
      node("span", "trade-values", `${trade.quantity} @ ${trade.price}`),
    );
    tradeList.append(item);
  }
}

function timelineButton(step, label, rejected) {
  const button = node("button", rejected ? "event-choice is-rejected" : "event-choice");
  button.type = "button";
  button.append(
    node("span", "event-index", String(step).padStart(2, "0")),
    node("span", "event-label", label),
  );
  button.setAttribute("aria-label", rejected
    ? `Step ${step}: rejected, ${label}`
    : `Step ${step}: ${label}`);
  button.addEventListener("click", () => setStep(step));
  return button;
}

function renderTimeline() {
  eventList.replaceChildren();
  const start = node("li");
  start.append(timelineButton(0, "Empty book", false));
  eventList.append(start);

  for (const frame of frames.slice(1)) {
    const item = node("li");
    const rejected = frame.accepted === false;
    const label = rejected ? `${eventLabel(frame.event)} · refused` : eventLabel(frame.event);
    item.append(timelineButton(frame.step, label, rejected));
    eventList.append(item);
  }
}

function updateTimelineSelection() {
  for (const [index, button] of [...eventList.querySelectorAll("button")].entries()) {
    if (index === currentStep) {
      button.setAttribute("aria-current", "step");
    } else {
      button.removeAttribute("aria-current");
    }
  }
}

function stopPlayback() {
  if (playbackTimer !== null) window.clearInterval(playbackTimer);
  playbackTimer = null;
  playButton.textContent = "Play";
  playButton.setAttribute("aria-label", "Play replay");
  playButton.setAttribute("aria-pressed", "false");
}

function setStep(step, { animate = false, keepPlayback = false } = {}) {
  if (!keepPlayback) stopPlayback();
  const previousFrame = frames[currentStep];
  currentStep = Math.max(0, Math.min(step, frames.length - 1));
  const frame = frames[currentStep];
  const shouldAnimate = animate && frame.step !== previousFrame?.step;
  const [summary, explanation] = describeEvent(frame);

  selectedTitle.textContent = eventHeadline(frame);
  eventSummary.textContent = summary;
  eventExplanation.textContent = explanation;
  stepNumber.textContent = `${currentStep} / ${frames.length - 1}`;
  stepSlider.value = String(currentStep);
  stepSlider.setAttribute("aria-valuetext", currentStep === 0
    ? "Start of replay"
    : `Step ${currentStep} of ${frames.length - 1}: ${eventLabel(frame.event)}`);
  previousButton.disabled = currentStep === 0;
  nextButton.disabled = currentStep === frames.length - 1;
  updateTimelineSelection();
  workbench.classList.toggle("animate-step", shouldAnimate);
  renderBook(frame, previousFrame, shouldAnimate);
  renderTrades(frame, shouldAnimate);

  // Ask the reader to commit before the answer appears. The question is only
  // fair when the next event actually has a queue to choose from, so it is
  // driven by the trace: more than one resting order gets filled.
  const nextFrame = frames[currentStep + 1];
  const restingTouched = new Set(nextFrame?.trades.map((trade) => trade.restingOrderId) ?? []);
  if (restingTouched.size > 1) {
    prediction.hidden = false;
    prediction.textContent = `Next: ${eventLabel(nextFrame.event)}. Which resting order fills first?`;
  } else {
    prediction.hidden = true;
    prediction.textContent = "";
  }

  const label = currentStep === 0 ? "Start" : eventLabel(frame.event) +
    (frame.accepted === false ? " · refused" : "");
  playbackStatus.textContent = `Step ${currentStep} of ${frames.length - 1} · ${label}`;
}

function startPlayback() {
  if (currentStep === frames.length - 1) setStep(0, { keepPlayback: true });
  playButton.textContent = "Pause";
  playButton.setAttribute("aria-label", "Pause replay");
  playButton.setAttribute("aria-pressed", "true");
  playbackTimer = window.setInterval(() => {
    if (currentStep >= frames.length - 1) {
      stopPlayback();
      return;
    }
    setStep(currentStep + 1, { animate: true, keepPlayback: true });
    if (currentStep === frames.length - 1) stopPlayback();
  }, 1050);
}

previousButton.addEventListener("click", () => setStep(currentStep - 1));
nextButton.addEventListener("click", () => setStep(currentStep + 1, { animate: true }));
playButton.addEventListener("click", () => {
  if (playbackTimer === null) startPlayback();
  else stopPlayback();
});
stepSlider.addEventListener("input", () => setStep(Number(stepSlider.value)));

document.addEventListener("keydown", (event) => {
  if (event.altKey || event.ctrlKey || event.metaKey || event.target instanceof HTMLInputElement ||
      event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLSelectElement) return;
  if (event.key === "ArrowLeft") {
    event.preventDefault();
    setStep(currentStep - 1);
  } else if (event.key === "ArrowRight") {
    event.preventDefault();
    setStep(currentStep + 1, { animate: true });
  }
});

new ResizeObserver(() => {
  if (frames.length > 0 && currentStep > 0) {
    renderDepthChart(frames[currentStep - 1], false);
    renderHistory();
  }
}).observe(ladderWrap);

function adoptTrace(trace) {
  frames = validateTrace(trace);
  prepareBookScale();
  renderAxis();
  eventCount.textContent = `${frames.length - 1} events`;
  stepSlider.max = String(frames.length - 1);
  currentStep = 0;
  loadError.hidden = true;
  renderTimeline();
  setStep(0);
}

function readParameters() {
  return buildScenario({
    buy1Size: BigInt(parameterFields.buy1Size.value),
    buy2Size: BigInt(parameterFields.buy2Size.value),
    buy2Price: BigInt(parameterFields.buy2Price.value),
    sellPrice: BigInt(parameterFields.sellPrice.value),
    sellSize: BigInt(parameterFields.sellSize.value),
    sellType: parameterFields.sellType.value,
    modifyFirst: parameterFields.modifyFirst.checked,
  });
}

function syncParameterAvailability() {
  const market = parameterFields.sellType.value === "MARKET";
  parameterFields.sellPrice.disabled = market;
}

async function requestTrace(events) {
  const response = await fetch(`/api/trace?events=${encodeURIComponent(events)}`);
  if (!response.ok) {
    const body = await response.json().catch(() => null);
    throw new Error(body?.error ?? `The engine returned HTTP ${response.status}.`);
  }
  return response.json();
}

async function refreshFromParameters() {
  if (!liveMode) return;
  const serial = ++requestSerial;
  try {
    const trace = await requestTrace(readParameters());
    if (serial !== requestSerial) return;
    adoptTrace(trace);
  } catch (error) {
    if (serial !== requestSerial) return;
    loadError.hidden = false;
    loadError.textContent = `The engine rejected that scenario. ${error.message}`;
    console.error("orda-book parameter replay failed", error);
  }
}

function scheduleParameterRefresh() {
  syncParameterAvailability();
  window.clearTimeout(parameterTimer);
  parameterTimer = window.setTimeout(refreshFromParameters, 120);
}

async function probeLiveMode() {
  try {
    await requestTrace(buildScenario());
    return true;
  } catch {
    return false;
  }
}

async function loadStaticTrace() {
  let response;
  try {
    response = await fetch("./data/price-time-priority.json");
  } catch {
    showLoadError("Couldn't reach the replay file. Serve the visualizer folder over HTTP, then refresh.");
    return;
  }
  if (!response.ok) {
    showLoadError(`The replay file is missing (HTTP ${response.status}). Regenerate it from the engine, then refresh.`);
    return;
  }
  try {
    adoptTrace(await response.json());
  } catch (error) {
    showLoadError("The replay file doesn't match what this page expects. Regenerate it from the engine, then refresh.");
    console.error("orda-book replay validation failed", error);
  }
}

async function loadReplay() {
  liveMode = await probeLiveMode();
  if (liveMode) {
    parameterPanel.hidden = false;
    syncParameterAvailability();
    await refreshFromParameters();
    return;
  }
  await loadStaticTrace();
}

function showLoadError(message) {
  loadError.hidden = false;
  loadError.textContent = message;
  eventCount.textContent = "Unavailable";
  playbackStatus.textContent = "Replay unavailable";
  previousButton.disabled = true;
  nextButton.disabled = true;
  playButton.disabled = true;
  stepSlider.disabled = true;
}

for (const field of Object.values(parameterFields)) {
  field.addEventListener("input", scheduleParameterRefresh);
  field.addEventListener("change", scheduleParameterRefresh);
}

loadReplay();
