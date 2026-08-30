#include "binary_event_codec.hpp"
#include "event_parser.hpp"
#include "order_book.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE(parser_accepts_plain_text_and_csv_lines) {
  std::istringstream input(
      "# comment\n"
      "ADD 1 BUY 100 10\n"
      "ADD,2,SELL,101,5\n"
      "MODIFY 1 102 7\n"
      "CANCEL 2\n");

  const lob::ParseResult parsed = lob::parse_event_stream(input);
  CHECK_TRUE(parsed.ok);
  CHECK_EQ(parsed.events.size(), static_cast<std::size_t>(4));
  CHECK_EQ(static_cast<int>(parsed.events[0].type), static_cast<int>(lob::EventType::Add));
  CHECK_EQ(static_cast<int>(parsed.events[1].type), static_cast<int>(lob::EventType::Add));
  CHECK_EQ(static_cast<int>(parsed.events[2].type), static_cast<int>(lob::EventType::Modify));
  CHECK_EQ(static_cast<int>(parsed.events[3].type), static_cast<int>(lob::EventType::Cancel));
}

TEST_CASE(replaying_event_stream_produces_expected_trades_and_book) {
  std::istringstream input(
      "ADD 1 BUY 100 10\n"
      "ADD 2 SELL 103 4\n"
      "ADD 3 SELL 100 6\n"
      "MODIFY 2 100 3\n"
      "CANCEL 1\n");

  const lob::ParseResult parsed = lob::parse_event_stream(input);
  CHECK_TRUE(parsed.ok);

  lob::OrderBook book;
  std::vector<lob::Trade> trades;
  for (const lob::Event& event : parsed.events) {
    CHECK_EQ(static_cast<int>(book.process(event, trades)), static_cast<int>(lob::BookError::None));
  }

  CHECK_EQ(trades.size(), static_cast<std::size_t>(2));
  CHECK_EQ(trades[0].price, 100);
  CHECK_EQ(trades[0].qty, 6);
  CHECK_EQ(trades[1].price, 100);
  CHECK_EQ(trades[1].qty, 3);
  CHECK_EQ(book.live_order_count(), static_cast<std::size_t>(0));
  CHECK_TRUE(!book.top_of_book(lob::Side::Bid).has_value());
  CHECK_TRUE(!book.top_of_book(lob::Side::Ask).has_value());
}

TEST_CASE(parser_rejects_unknown_commands) {
  std::istringstream input("NOPE 1 2 3\n");
  const lob::ParseResult parsed = lob::parse_event_stream(input);
  CHECK_TRUE(!parsed.ok);
  CHECK_EQ(parsed.error_line, static_cast<std::size_t>(1));
}

TEST_CASE(parser_stream_callback_does_not_accumulate_events) {
  std::istringstream input("ADD 1 BUY 100 10\nCANCEL 1\n");
  std::vector<lob::Event> observed;
  const lob::ParseResult parsed = lob::for_each_event_stream(
      input, [&observed](const lob::Event& event) { observed.push_back(event); });
  CHECK_TRUE(parsed.ok);
  CHECK_TRUE(parsed.events.empty());
  CHECK_EQ(observed.size(), static_cast<std::size_t>(2));
  CHECK_EQ(observed[0].order_id, static_cast<lob::OrderId>(1));
  CHECK_EQ(static_cast<int>(observed[1].type), static_cast<int>(lob::EventType::Cancel));
}

TEST_CASE(parser_accepts_market_ioc_fok_and_post_only_options) {
  std::istringstream input(
      "ADD 1 BUY MARKET 4\n"
      "ADD 2 SELL 101 5 FOK\n"
      "ADD 3 BUY 100 2 IOC\n"
      "ADD 4 SELL 102 3 POST_ONLY\n");
  const lob::ParseResult parsed = lob::parse_event_stream(input);
  CHECK_TRUE(parsed.ok);
  CHECK_EQ(parsed.events.size(), static_cast<std::size_t>(4));
  CHECK_EQ(static_cast<int>(parsed.events[0].order_type), static_cast<int>(lob::OrderType::Market));
  CHECK_EQ(static_cast<int>(parsed.events[0].time_in_force),
           static_cast<int>(lob::TimeInForce::ImmediateOrCancel));
  CHECK_EQ(static_cast<int>(parsed.events[1].time_in_force),
           static_cast<int>(lob::TimeInForce::FillOrKill));
  CHECK_EQ(static_cast<int>(parsed.events[2].time_in_force),
           static_cast<int>(lob::TimeInForce::ImmediateOrCancel));
  CHECK_TRUE(parsed.events[3].post_only);
}

TEST_CASE(binary_event_round_trip_preserves_events_and_streams) {
  const std::string path = "/tmp/orda-book-replay-test.bin";
  std::vector<lob::Event> events;
  lob::Event market;
  market.type = lob::EventType::Add;
  market.order_id = 7;
  market.side = lob::Side::Bid;
  market.order_type = lob::OrderType::Market;
  market.time_in_force = lob::TimeInForce::ImmediateOrCancel;
  market.qty = 4;
  market.line_number = 3;
  events.push_back(market);
  lob::Event modify;
  modify.type = lob::EventType::Modify;
  modify.order_id = 7;
  modify.new_price = -9;
  modify.new_qty = 11;
  modify.line_number = 4;
  events.push_back(modify);

  std::string error;
  CHECK_TRUE(lob::write_binary_event_file(path, events, error));
  CHECK_TRUE(error.empty());
  const lob::ParseResult parsed = lob::parse_binary_event_file(path);
  CHECK_TRUE(parsed.ok);
  CHECK_EQ(parsed.events.size(), events.size());
  for (std::size_t index = 0; index < events.size(); ++index) {
    CHECK_EQ(static_cast<int>(parsed.events[index].type), static_cast<int>(events[index].type));
    CHECK_EQ(parsed.events[index].order_id, events[index].order_id);
    CHECK_EQ(static_cast<int>(parsed.events[index].side), static_cast<int>(events[index].side));
    CHECK_EQ(parsed.events[index].price, events[index].price);
    CHECK_EQ(parsed.events[index].qty, events[index].qty);
    CHECK_EQ(static_cast<int>(parsed.events[index].order_type),
             static_cast<int>(events[index].order_type));
    CHECK_EQ(static_cast<int>(parsed.events[index].time_in_force),
             static_cast<int>(events[index].time_in_force));
    CHECK_EQ(parsed.events[index].post_only, events[index].post_only);
    CHECK_EQ(parsed.events[index].new_price, events[index].new_price);
    CHECK_EQ(parsed.events[index].new_qty, events[index].new_qty);
    CHECK_EQ(parsed.events[index].line_number, events[index].line_number);
  }

  std::vector<lob::Event> streamed;
  const lob::ParseResult streamed_result = lob::for_each_binary_event_file(
      path, [&streamed](const lob::Event& event) { streamed.push_back(event); });
  CHECK_TRUE(streamed_result.ok);
  CHECK_EQ(streamed.size(), events.size());
  CHECK_EQ(streamed[1].new_price, static_cast<lob::Price>(-9));
  std::remove(path.c_str());
}
