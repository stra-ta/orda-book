#include "order_book.hpp"
#include "ladder_order_book.hpp"
#include "pooled_order_book.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <vector>

namespace {

lob::LevelSnapshot require_top(const lob::OrderBook& book, lob::Side side) {
  const auto top = book.top_of_book(side);
  CHECK_TRUE(top.has_value());
  return *top;
}

}  // namespace

TEST_CASE(add_without_cross_sets_top_of_book) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 10, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 105, 7, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), static_cast<std::size_t>(0));

  const lob::LevelSnapshot best_bid = require_top(book, lob::Side::Bid);
  const lob::LevelSnapshot best_ask = require_top(book, lob::Side::Ask);
  CHECK_EQ(best_bid.price, 100);
  CHECK_EQ(best_bid.qty, 10);
  CHECK_EQ(best_ask.price, 105);
  CHECK_EQ(best_ask.qty, 7);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(2));
}

TEST_CASE(immediate_cross_partial_fill_leaves_resting_residual) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 100, 10, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 101, 6, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(trades.size(), static_cast<std::size_t>(1));
  CHECK_EQ(trades[0].resting_order_id, static_cast<lob::OrderId>(1));
  CHECK_EQ(trades[0].incoming_order_id, static_cast<lob::OrderId>(2));
  CHECK_EQ(trades[0].price, 100);
  CHECK_EQ(trades[0].qty, 6);

  const lob::LevelSnapshot best_ask = require_top(book, lob::Side::Ask);
  CHECK_EQ(best_ask.price, 100);
  CHECK_EQ(best_ask.qty, 4);
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
}

TEST_CASE(price_time_priority_is_fifo_within_level) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 99, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 99, 7, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(3, lob::Side::Ask, 99, 6, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(trades.size(), static_cast<std::size_t>(2));
  CHECK_EQ(trades[0].resting_order_id, static_cast<lob::OrderId>(1));
  CHECK_EQ(trades[0].qty, 5);
  CHECK_EQ(trades[1].resting_order_id, static_cast<lob::OrderId>(2));
  CHECK_EQ(trades[1].qty, 1);

  const lob::LevelSnapshot best_bid = require_top(book, lob::Side::Bid);
  CHECK_EQ(best_bid.price, 99);
  CHECK_EQ(best_bid.qty, 6);
  CHECK_EQ(best_bid.order_count, static_cast<std::size_t>(1));
}

TEST_CASE(aggressive_order_sweeps_multiple_price_levels) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 101, 7, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(3, lob::Side::Bid, 101, 9, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(trades.size(), static_cast<std::size_t>(2));
  CHECK_EQ(trades[0].price, 100);
  CHECK_EQ(trades[0].qty, 5);
  CHECK_EQ(trades[1].price, 101);
  CHECK_EQ(trades[1].qty, 4);

  const lob::LevelSnapshot best_ask = require_top(book, lob::Side::Ask);
  CHECK_EQ(best_ask.price, 101);
  CHECK_EQ(best_ask.qty, 3);
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
}

TEST_CASE(cancel_existing_and_missing_orders) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(10, lob::Side::Bid, 98, 4, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.cancel_order(10)), static_cast<int>(lob::BookError::None));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(0));
  CHECK_EQ(static_cast<int>(book.cancel_order(10)), static_cast<int>(lob::BookError::UnknownOrderId));
}

TEST_CASE(modify_is_cancel_replace_and_loses_priority) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 99, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Bid, 99, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.modify_order(1, 99, 4, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(3, lob::Side::Ask, 99, 9, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(trades.size(), static_cast<std::size_t>(2));
  CHECK_EQ(trades[0].resting_order_id, static_cast<lob::OrderId>(2));
  CHECK_EQ(trades[0].qty, 5);
  CHECK_EQ(trades[1].resting_order_id, static_cast<lob::OrderId>(1));
  CHECK_EQ(trades[1].qty, 4);

  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
}

TEST_CASE(modify_can_turn_resting_order_into_aggressor) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(10, lob::Side::Ask, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(20, lob::Side::Bid, 95, 8, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.modify_order(20, 101, 8, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(trades.size(), static_cast<std::size_t>(1));
  CHECK_EQ(trades[0].resting_order_id, static_cast<lob::OrderId>(10));
  CHECK_EQ(trades[0].incoming_order_id, static_cast<lob::OrderId>(20));
  CHECK_EQ(trades[0].qty, 5);
  CHECK_EQ(trades[0].price, 100);

  const lob::LevelSnapshot best_bid = require_top(book, lob::Side::Bid);
  CHECK_EQ(best_bid.price, 101);
  CHECK_EQ(best_bid.qty, 3);
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
}

TEST_CASE(duplicate_ids_and_invalid_inputs_are_rejected) {
  lob::OrderBook book;
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 101, 1, trades)),
           static_cast<int>(lob::BookError::DuplicateOrderId));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 0, 1, trades)),
           static_cast<int>(lob::BookError::InvalidPrice));
  CHECK_EQ(static_cast<int>(book.add_order(3, lob::Side::Ask, 101, 0, trades)),
           static_cast<int>(lob::BookError::InvalidQuantity));
  CHECK_EQ(static_cast<int>(book.modify_order(1, 0, 3, trades)),
           static_cast<int>(lob::BookError::InvalidPrice));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  const lob::LevelSnapshot best_bid = require_top(book, lob::Side::Bid);
  CHECK_EQ(best_bid.price, 100);
  CHECK_EQ(best_bid.qty, 1);

  CHECK_EQ(static_cast<int>(book.modify_order(1, 101, 0, trades)),
           static_cast<int>(lob::BookError::InvalidQuantity));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  CHECK_EQ(require_top(book, lob::Side::Bid).price, 100);
}

template <typename Engine>
void check_immediate_order_policies() {
  Engine book;
  book.reserve_orders(32);
  std::vector<lob::Trade> trades;

  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 100, 5, trades)),
           static_cast<int>(lob::BookError::None));

  lob::Event market;
  market.type = lob::EventType::Add;
  market.order_id = 2;
  market.side = lob::Side::Bid;
  market.order_type = lob::OrderType::Market;
  market.qty = 3;
  CHECK_EQ(static_cast<int>(book.process(market, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.back().qty, 3);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  CHECK_EQ(book.top_of_book(lob::Side::Ask)->qty, 2);
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());

  lob::Event ioc;
  ioc.type = lob::EventType::Add;
  ioc.order_id = 3;
  ioc.side = lob::Side::Bid;
  ioc.price = 101;
  ioc.qty = 5;
  ioc.time_in_force = lob::TimeInForce::ImmediateOrCancel;
  CHECK_EQ(static_cast<int>(book.process(ioc, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.back().qty, 2);
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
}

template <typename Engine>
void check_fill_or_kill_and_post_only() {
  Engine book;
  book.reserve_orders(32);
  std::vector<lob::Trade> trades;
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 100, 2, trades)),
           static_cast<int>(lob::BookError::None));

  lob::Event fok;
  fok.type = lob::EventType::Add;
  fok.order_id = 2;
  fok.side = lob::Side::Bid;
  fok.price = 101;
  fok.qty = 3;
  fok.time_in_force = lob::TimeInForce::FillOrKill;
  trades.clear();
  CHECK_EQ(static_cast<int>(book.process(fok, trades)),
           static_cast<int>(lob::BookError::InsufficientLiquidity));
  CHECK_EQ(book.top_of_book(lob::Side::Ask)->qty, 2);
  CHECK_TRUE(trades.empty());

  fok.order_id = 3;
  fok.qty = 2;
  CHECK_EQ(static_cast<int>(book.process(fok, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.back().qty, 2);

  lob::Event resting;
  resting.type = lob::EventType::Add;
  resting.order_id = 4;
  resting.side = lob::Side::Bid;
  resting.price = 99;
  resting.qty = 1;
  resting.post_only = true;
  CHECK_EQ(static_cast<int>(book.process(resting, trades)),
           static_cast<int>(lob::BookError::None));

  CHECK_EQ(static_cast<int>(book.add_order(6, lob::Side::Ask, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));

  lob::Event crossing = resting;
  crossing.order_id = 5;
  crossing.price = 100;
  CHECK_EQ(static_cast<int>(book.process(crossing, trades)),
           static_cast<int>(lob::BookError::WouldTakeLiquidity));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(2));
}

TEST_CASE(order_type_policies_match_across_all_backends) {
  check_immediate_order_policies<lob::OrderBook>();
  check_immediate_order_policies<lob::PooledOrderBook>();
  check_immediate_order_policies<lob::LadderOrderBook>();
  check_fill_or_kill_and_post_only<lob::OrderBook>();
  check_fill_or_kill_and_post_only<lob::PooledOrderBook>();
  check_fill_or_kill_and_post_only<lob::LadderOrderBook>();
}

template <typename Engine>
void check_order_id_reuse_after_fill() {
  Engine book;
  book.reserve_orders(8);
  std::vector<lob::Trade> trades;
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(2, lob::Side::Ask, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Ask, 101, 2, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  CHECK_EQ(book.top_of_book(lob::Side::Ask)->price, 101);
}

template <typename Engine>
void check_pathological_cancel_replace_chain() {
  Engine book;
  book.reserve_orders(8);
  std::vector<lob::Trade> trades;
  CHECK_EQ(static_cast<int>(book.add_order(1, lob::Side::Bid, 100, 1, trades)),
           static_cast<int>(lob::BookError::None));
  for (lob::Price price = 101; price < 1101; ++price) {
    CHECK_EQ(static_cast<int>(book.modify_order(1, price, price % 17 + 1, trades)),
             static_cast<int>(lob::BookError::None));
    CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(1));
  }
  CHECK_EQ(book.top_of_book(lob::Side::Bid)->price, static_cast<lob::Price>(1100));
}

template <typename Engine>
void check_mass_price_level_sweep() {
  Engine book;
  book.reserve_orders(512);
  std::vector<lob::Trade> trades;
  constexpr std::size_t kLevels = 256;
  for (std::size_t index = 0; index < kLevels; ++index) {
    CHECK_EQ(static_cast<int>(book.add_order(static_cast<lob::OrderId>(index + 1),
                                              lob::Side::Ask,
                                              static_cast<lob::Price>(100 + index), 1, trades)),
             static_cast<int>(lob::BookError::None));
  }
  CHECK_EQ(book.levels(lob::Side::Ask).size(), kLevels);
  trades.clear();
  CHECK_EQ(static_cast<int>(book.add_order(10000, lob::Side::Bid, 355, 256, trades)),
           static_cast<int>(lob::BookError::None));
  CHECK_EQ(trades.size(), kLevels);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(0));
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
}

TEST_CASE(order_lifecycle_boundaries_match_across_all_backends) {
  check_order_id_reuse_after_fill<lob::OrderBook>();
  check_order_id_reuse_after_fill<lob::PooledOrderBook>();
  check_order_id_reuse_after_fill<lob::LadderOrderBook>();
  check_pathological_cancel_replace_chain<lob::OrderBook>();
  check_pathological_cancel_replace_chain<lob::PooledOrderBook>();
  check_pathological_cancel_replace_chain<lob::LadderOrderBook>();
  check_mass_price_level_sweep<lob::OrderBook>();
  check_mass_price_level_sweep<lob::PooledOrderBook>();
  check_mass_price_level_sweep<lob::LadderOrderBook>();
}
