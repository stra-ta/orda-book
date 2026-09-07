#include "order_book.hpp"
#include "pooled_order_book.hpp"
#include "ladder_order_book.hpp"
#include "reference_order_book.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <vector>

namespace {

class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed == 0 ? 0x9e3779b97f4a7c15ULL : seed) {}

  std::uint64_t next() {
    std::uint64_t value = state_;
    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    state_ = value;
    return value;
  }

  std::size_t uniform(std::size_t bound) {
    return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
  }

 private:
  std::uint64_t state_;
};

lob::Event add(lob::OrderId id, lob::Side side, lob::Price price, lob::Quantity qty) {
  lob::Event event;
  event.type = lob::EventType::Add;
  event.order_id = id;
  event.side = side;
  event.price = price;
  event.qty = qty;
  return event;
}

lob::Event cancel(lob::OrderId id) {
  lob::Event event;
  event.type = lob::EventType::Cancel;
  event.order_id = id;
  return event;
}

lob::Event modify(lob::OrderId id, lob::Price price, lob::Quantity qty) {
  lob::Event event;
  event.type = lob::EventType::Modify;
  event.order_id = id;
  event.new_price = price;
  event.new_qty = qty;
  return event;
}

std::vector<lob::Event> generate_history(std::uint64_t seed, std::size_t count) {
  Rng rng(seed);
  std::vector<lob::Event> events;
  std::vector<lob::OrderId> known_ids;
  lob::OrderId next_id = 1;
  events.reserve(count);

  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t operation = rng.uniform(12);
    const lob::Side side = (rng.next() & 1U) == 0U ? lob::Side::Bid : lob::Side::Ask;
    const lob::Price price = 95 + static_cast<lob::Price>(rng.uniform(11));
    const lob::Quantity qty = 1 + static_cast<lob::Quantity>(rng.uniform(32));

    if (operation < 5 || known_ids.empty()) {
      const lob::OrderId id = next_id++;
      known_ids.push_back(id);
      events.push_back(add(id, side, price, qty));
    } else {
      const lob::OrderId id = known_ids[rng.uniform(known_ids.size())];
      if (operation < 7) {
        events.push_back(cancel(id));
      } else if (operation < 9) {
        events.push_back(modify(id, price, qty));
      } else if (operation == 9) {
        events.push_back(modify(id, 0, qty));
      } else if (operation == 10) {
        events.push_back(add(id, side, price, qty));
      } else {
        events.push_back(cancel(next_id + 1000000));
      }
    }
  }
  return events;
}

std::vector<lob::Event> generate_reject_history(std::uint64_t seed, std::size_t count) {
  // Invalid-input storm: every rejection path (duplicate ID, invalid
  // price/quantity, unknown cancel/modify target, invalid modify) is compared
  // event-by-event against the reference engine. Prices stay far from the
  // touch and inside the ladder's valid domain so all three backends agree
  // with the reference on every error code. Valid adds use fresh IDs and
  // never cross, so they always succeed and keep the live-ID pool exact.
  Rng rng(seed);
  std::vector<lob::Event> events;
  std::vector<lob::OrderId> live_ids;
  lob::OrderId next_id = 1;
  events.reserve(count);

  const auto far_price = [&rng](lob::Side side) {
    const lob::Price base = side == lob::Side::Bid ? 99000 : 101000;
    const lob::Price offset = static_cast<lob::Price>(rng.uniform(64));
    return side == lob::Side::Bid ? base - offset : base + offset;
  };

  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t operation = rng.uniform(10);
    const lob::Side side = (rng.next() & 1U) == 0U ? lob::Side::Bid : lob::Side::Ask;
    const lob::Quantity qty = 1 + static_cast<lob::Quantity>(rng.uniform(32));

    if (live_ids.empty() || operation < 3) {
      const lob::OrderId id = next_id++;
      live_ids.push_back(id);
      events.push_back(add(id, side, far_price(side), qty));
    } else if (operation == 3) {
      events.push_back(add(next_id++, side, 0, qty));
    } else if (operation == 4) {
      events.push_back(add(next_id++, side, far_price(side), 0));
    } else if (operation == 5) {
      events.push_back(add(live_ids[rng.uniform(live_ids.size())], side, far_price(side), qty));
    } else if (operation == 6) {
      events.push_back(cancel(next_id + 1000000));
    } else if (operation == 7) {
      const std::size_t victim = rng.uniform(live_ids.size());
      events.push_back(cancel(live_ids[victim]));
      live_ids[victim] = live_ids.back();
      live_ids.pop_back();
    } else if (operation == 8) {
      events.push_back(modify(live_ids[rng.uniform(live_ids.size())], 0, qty));
    } else {
      if ((rng.next() & 1U) == 0U) {
        events.push_back(modify(next_id + 1000000, far_price(side), qty));
      } else {
        events.push_back(modify(live_ids[rng.uniform(live_ids.size())], far_price(side), 0));
      }
    }
  }
  return events;
}

void compare_trades(const std::vector<lob::Trade>& actual,
                    const std::vector<lob::Trade>& expected) {
  CHECK_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CHECK_EQ(actual[index].resting_order_id, expected[index].resting_order_id);
    CHECK_EQ(actual[index].incoming_order_id, expected[index].incoming_order_id);
    CHECK_EQ(actual[index].price, expected[index].price);
    CHECK_EQ(actual[index].qty, expected[index].qty);
    CHECK_EQ(static_cast<int>(actual[index].aggressor_side),
             static_cast<int>(expected[index].aggressor_side));
  }
}

void compare_orders(const std::vector<lob::OrderSnapshot>& actual,
                    const std::vector<lob::OrderSnapshot>& expected) {
  CHECK_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CHECK_EQ(actual[index].order_id, expected[index].order_id);
    CHECK_EQ(static_cast<int>(actual[index].side), static_cast<int>(expected[index].side));
    CHECK_EQ(actual[index].price, expected[index].price);
    CHECK_EQ(actual[index].qty, expected[index].qty);
  }
}

void compare_stats(const lob::EngineStats& actual, const lob::EngineStats& expected) {
  CHECK_EQ(actual.add_requests, expected.add_requests);
  CHECK_EQ(actual.cancel_requests, expected.cancel_requests);
  CHECK_EQ(actual.modify_requests, expected.modify_requests);
  CHECK_EQ(actual.rejected_requests, expected.rejected_requests);
  CHECK_EQ(actual.trades, expected.trades);
  CHECK_EQ(actual.traded_qty, expected.traded_qty);
}

template <typename Engine>
void compare_after_event(const Engine& actual, const reference_lob::OrderBook& expected) {
  compare_orders(actual.orders(lob::Side::Bid), expected.orders(lob::Side::Bid));
  compare_orders(actual.orders(lob::Side::Ask), expected.orders(lob::Side::Ask));
  CHECK_EQ(actual.live_order_count(), expected.live_order_count());
  compare_stats(actual.stats(), expected.stats());
}

template <typename Engine>
void run_differential_history(const std::vector<lob::Event>& events) {
  Engine actual;
  actual.reserve_orders(events.size());
  reference_lob::OrderBook expected;

  for (const lob::Event& event : events) {
    std::vector<lob::Trade> actual_trades;
    std::vector<lob::Trade> expected_trades;
    const lob::BookError actual_error = actual.process(event, actual_trades);
    const lob::BookError expected_error = expected.process(event, expected_trades);
    CHECK_EQ(static_cast<int>(actual_error), static_cast<int>(expected_error));
    compare_trades(actual_trades, expected_trades);
    compare_after_event(actual, expected);
  }
}

}  // namespace

TEST_CASE(differential_engine_matches_reference_across_fixed_histories) {
  for (std::uint64_t seed = 1; seed <= 256; ++seed) {
    run_differential_history<lob::OrderBook>(generate_history(seed, 750));
  }
}

TEST_CASE(pooled_engine_matches_reference_across_fixed_histories) {
  for (std::uint64_t seed = 1; seed <= 256; ++seed) {
    run_differential_history<lob::PooledOrderBook>(generate_history(seed, 750));
  }
}

TEST_CASE(reject_histories_match_reference_on_all_backends) {
  for (std::uint64_t seed = 1; seed <= 64; ++seed) {
    const std::vector<lob::Event> events = generate_reject_history(seed, 300);
    run_differential_history<lob::OrderBook>(events);
    run_differential_history<lob::PooledOrderBook>(events);
    run_differential_history<lob::LadderOrderBook>(events);
  }
}

TEST_CASE(pooled_reject_heavy_capacity_path_rejects_only_when_full) {
  // Mirrors the benchmark reject-heavy setup: a tight reservation is exactly
  // filled, then further non-crossing adds must fail with CapacityExceeded
  // while invalid inputs keep their usual errors and the book stays intact.
  lob::PooledOrderBook book;
  book.reserve_orders(8);
  std::vector<lob::Trade> trades;

  for (lob::OrderId id = 1; id <= 8; ++id) {
    const lob::Side side = (id & 1U) == 0U ? lob::Side::Bid : lob::Side::Ask;
    const lob::Price price = side == lob::Side::Bid ? 99000 : 101000;
    CHECK_EQ(static_cast<int>(book.add_order(id, side, price, 1, trades)),
             static_cast<int>(lob::BookError::None));
  }
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(8));

  for (lob::OrderId id = 9; id <= 24; ++id) {
    CHECK_EQ(static_cast<int>(book.add_order(id, lob::Side::Bid, 99000, 1, trades)),
             static_cast<int>(lob::BookError::CapacityExceeded));
  }
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(8));
  CHECK_TRUE(trades.empty());
  CHECK_EQ(book.stats().rejected_requests, static_cast<std::size_t>(16));

  // Invalid inputs at full capacity keep their specific errors.
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 99000, 1, trades)),
           static_cast<int>(lob::BookError::DuplicateOrderId));
  CHECK_EQ(static_cast<int>(book.add_order(100, lob::Side::Bid, 0, 1, trades)),
           static_cast<int>(lob::BookError::InvalidPrice));
  CHECK_EQ(static_cast<int>(book.add_order(101, lob::Side::Bid, 99000, 0, trades)),
           static_cast<int>(lob::BookError::InvalidQuantity));
  CHECK_EQ(static_cast<int>(book.cancel_order(1000000)),
           static_cast<int>(lob::BookError::UnknownOrderId));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(8));

  // A crossing add still recycles a slot instead of rejecting.
  CHECK_EQ(static_cast<int>(book.add_order(200, lob::Side::Ask, 99000, 8, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), static_cast<std::size_t>(4));
}

// The pooled capacity contract rejects a resting add only when no slot can be
// obtained: there is no free slot now and the incoming order crosses no opposing
// quantity to free one. A crossing add is accepted even at full capacity because
// matching fully consumes each crossed order and recycles its slot.

TEST_CASE(pooled_capacity_rejects_only_noncrossing_resting_add_at_full_capacity) {
  lob::PooledOrderBook book;
  book.reserve_orders(1);
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));

  // Full capacity, no opposing order to cross: a resting add must be rejected and
  // the book left untouched.
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 101, 1, trades)),
           static_cast<int>(lob::BookError::CapacityExceeded));
  CHECK_TRUE(trades.empty());
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  CHECK_EQ(book.top_of_book(lob::Side::Bid)->qty, 1);
  CHECK_EQ(book.stats().rejected_requests, static_cast<std::size_t>(1));

  // Freeing a slot restores the ability to add a resting order.
  CHECK_EQ(static_cast<int>(book.cancel_order(1)), static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
}

TEST_CASE(pooled_crossing_add_recycles_slot_and_succeeds_at_full_capacity) {
  lob::PooledOrderBook book;
  book.reserve_orders(1);
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));

  // No free slot, but the incoming ask fully crosses the resting bid, which
  // releases its slot. The add must not be rejected.
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), static_cast<std::size_t>(1));
  CHECK_EQ(trades[0].qty, 5);
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(0));

  // The recycled slot is now available for a fresh resting add.
  CHECK_EQ(static_cast<int>(book.add_order(3, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
}

TEST_CASE(pooled_crossing_add_with_residual_rest_succeeds_at_full_capacity) {
  lob::PooledOrderBook book;
  book.reserve_orders(1);
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));

  // Incoming ask exceeds the resting bid: the bid is fully consumed (slot freed)
  // and the leftover quantity rests as a new ask in that recycled slot.
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 100, 8, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), static_cast<std::size_t>(1));
  CHECK_EQ(trades[0].qty, 5);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  const auto ask_top = book.top_of_book(lob::Side::Ask);
  CHECK_TRUE(ask_top.has_value());
  CHECK_EQ(ask_top->qty, 3);
}

TEST_CASE(pooled_partial_crossing_at_full_capacity_keeps_resting_order) {
  lob::PooledOrderBook book;
  book.reserve_orders(1);
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));

  // Incoming ask is smaller than the resting bid: it is fully absorbed without
  // freeing a slot and without leaving a resting residual, so no rejection fires.
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 100, 3, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), static_cast<std::size_t>(1));
  CHECK_EQ(trades[0].qty, 3);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  CHECK_EQ(book.top_of_book(lob::Side::Bid)->qty, 2);
}

TEST_CASE(ladder_engine_matches_reference_across_fixed_histories) {
  for (std::uint64_t seed = 1; seed <= 256; ++seed) {
    run_differential_history<lob::LadderOrderBook>(generate_history(seed, 750));
  }
}

TEST_CASE(ladder_rejects_prices_outside_its_configured_range) {
  lob::LadderOrderBook book(10, 100);
  std::vector<lob::Trade> trades;
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 101, 1, trades)),
           static_cast<int>(lob::BookError::PriceOutOfRange));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 9, 1, trades)),
           static_cast<int>(lob::BookError::PriceOutOfRange));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(0));
}
