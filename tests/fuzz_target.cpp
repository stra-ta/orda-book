#include "ladder_order_book.hpp"
#include "order_book.hpp"
#include "pooled_order_book.hpp"
#include "reference_order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

lob::Event decode(const std::uint8_t* bytes, std::size_t width) {
  const auto value = [bytes, width](std::size_t offset) {
    return static_cast<std::uint64_t>(bytes[offset % width]);
  };
  lob::Event event;
  event.type = static_cast<lob::EventType>(value(0) % 3U);
  event.order_id = 1U + value(1) % 32U;
  event.side = (value(2) & 1U) == 0U ? lob::Side::Bid : lob::Side::Ask;
  // This target intentionally keeps prices in the ladder's valid domain.
  // Invalid-price behavior is covered by the abuse suite.
  event.price = 90 + static_cast<lob::Price>(value(3) % 21U);
  event.qty = value(4) % 8U == 0U ? 0 : 1 + static_cast<lob::Quantity>(value(4) % 32U);
  event.new_price = 90 + static_cast<lob::Price>(value(5) % 21U);
  event.new_qty = value(6) % 8U == 0 ? 0 : 1 + static_cast<lob::Quantity>(value(6) % 32U);
  event.order_type = (value(7) & 1U) == 0U ? lob::OrderType::Limit : lob::OrderType::Market;
  event.time_in_force = static_cast<lob::TimeInForce>(value(8) % 3U);
  event.post_only = event.order_type == lob::OrderType::Limit && (value(9) & 1U) != 0;
  return event;
}

bool same_orders(const std::vector<lob::OrderSnapshot>& actual,
                 const std::vector<lob::OrderSnapshot>& expected) {
  if (actual.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual[index].order_id != expected[index].order_id ||
        actual[index].side != expected[index].side ||
        actual[index].price != expected[index].price ||
        actual[index].qty != expected[index].qty) {
      return false;
    }
  }
  return true;
}

bool same_trades(const std::vector<lob::Trade>& actual,
                 const std::vector<lob::Trade>& expected) {
  if (actual.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual[index].resting_order_id != expected[index].resting_order_id ||
        actual[index].incoming_order_id != expected[index].incoming_order_id ||
        actual[index].price != expected[index].price || actual[index].qty != expected[index].qty ||
        actual[index].aggressor_side != expected[index].aggressor_side) {
      return false;
    }
  }
  return true;
}

bool same_stats(const lob::EngineStats& actual, const lob::EngineStats& expected) {
  return actual.add_requests == expected.add_requests &&
         actual.cancel_requests == expected.cancel_requests &&
         actual.modify_requests == expected.modify_requests &&
         actual.rejected_requests == expected.rejected_requests &&
         actual.trades == expected.trades && actual.traded_qty == expected.traded_qty;
}

template <typename Engine>
bool same_engine(const Engine& actual, const reference_lob::OrderBook& expected) {
  return same_orders(actual.orders(lob::Side::Bid), expected.orders(lob::Side::Bid)) &&
         same_orders(actual.orders(lob::Side::Ask), expected.orders(lob::Side::Ask)) &&
         actual.live_order_count() == expected.live_order_count() &&
         same_stats(actual.stats(), expected.stats());
}

template <typename Engine>
bool process_and_compare(Engine& actual, const lob::Event& event,
                         const reference_lob::OrderBook& expected,
                         std::vector<lob::Trade>& actual_trades,
                         const std::vector<lob::Trade>& expected_trades,
                         lob::BookError expected_error) {
  const lob::BookError actual_error = actual.process(event, actual_trades);
  return actual_error == expected_error && same_trades(actual_trades, expected_trades) &&
         same_engine(actual, expected);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }

  lob::OrderBook actual;
  actual.reserve_orders(size + 1U);
  lob::PooledOrderBook pooled;
  pooled.reserve_orders(size + 1U);
  lob::LadderOrderBook ladder(1, 200000);
  ladder.reserve_orders(size + 1U);
  reference_lob::OrderBook expected;
  for (std::size_t offset = 0; offset < size; offset += 7U) {
    const lob::Event event = decode(data + offset, size - offset);
    std::vector<lob::Trade> actual_trades;
    std::vector<lob::Trade> pooled_trades;
    std::vector<lob::Trade> ladder_trades;
    std::vector<lob::Trade> expected_trades;
    const lob::BookError expected_error = expected.process(event, expected_trades);
    if (!process_and_compare(actual, event, expected, actual_trades, expected_trades,
                             expected_error) ||
        !process_and_compare(pooled, event, expected, pooled_trades, expected_trades,
                             expected_error) ||
        !process_and_compare(ladder, event, expected, ladder_trades, expected_trades,
                             expected_error)) {
      __builtin_trap();
    }
  }
  return 0;
}
