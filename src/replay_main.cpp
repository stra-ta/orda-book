#include "binary_event_codec.hpp"
#include "event_parser.hpp"
#include "order_book.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaximumTraceEvents = 500;

struct TraceFrame {
  std::size_t step{};
  std::optional<lob::Event> event;
  lob::BookError error{lob::BookError::None};
  std::vector<lob::Trade> trades;
  std::vector<lob::OrderSnapshot> bids;
  std::vector<lob::OrderSnapshot> asks;
};

void print_usage() {
  std::cerr << "usage: orda_replay <event_file> [--top] [--binary]\n"
               "       orda_replay --trace-json <event_file>\n";
}

void print_trade(const lob::Trade& trade) {
  std::cout << "TRADE resting=" << trade.resting_order_id << " incoming=" << trade.incoming_order_id
            << " price=" << trade.price << " qty=" << trade.qty
            << " aggressor=" << lob::to_string(trade.aggressor_side) << '\n';
}

std::string_view event_name(lob::EventType type) {
  switch (type) {
    case lob::EventType::Add:
      return "add";
    case lob::EventType::Cancel:
      return "cancel";
    case lob::EventType::Modify:
      return "modify";
  }
  return "unknown";
}

void print_trace_event(const lob::Event& event) {
  std::cout << "{\"kind\":\"" << event_name(event.type) << "\",\"orderId\":\""
            << event.order_id << '"';
  switch (event.type) {
    case lob::EventType::Add:
      std::cout << ",\"side\":\"" << lob::to_string(event.side) << "\",\"orderType\":\""
                << lob::to_string(event.order_type) << "\",\"timeInForce\":\""
                << lob::to_string(event.time_in_force) << "\",\"price\":\"" << event.price
                << "\",\"quantity\":\"" << event.qty << "\",\"postOnly\":"
                << (event.post_only ? "true" : "false");
      break;
    case lob::EventType::Modify:
      std::cout << ",\"price\":\"" << event.new_price << "\",\"quantity\":\""
                << event.new_qty << '"';
      break;
    case lob::EventType::Cancel:
      break;
  }
  std::cout << '}';
}

void print_trace_orders(const std::vector<lob::OrderSnapshot>& orders) {
  std::cout << '[';
  for (std::size_t index = 0; index < orders.size(); ++index) {
    if (index != 0) std::cout << ',';
    const lob::OrderSnapshot& order = orders[index];
    std::cout << "{\"orderId\":\"" << order.order_id << "\",\"price\":\"" << order.price
              << "\",\"quantity\":\"" << order.qty << "\"}";
  }
  std::cout << ']';
}

void print_trace_trades(const std::vector<lob::Trade>& trades) {
  std::cout << '[';
  for (std::size_t index = 0; index < trades.size(); ++index) {
    if (index != 0) std::cout << ',';
    const lob::Trade& trade = trades[index];
    std::cout << "{\"restingOrderId\":\"" << trade.resting_order_id
              << "\",\"incomingOrderId\":\"" << trade.incoming_order_id
              << "\",\"price\":\"" << trade.price << "\",\"quantity\":\"" << trade.qty
              << "\",\"aggressorSide\":\"" << lob::to_string(trade.aggressor_side) << "\"}";
  }
  std::cout << ']';
}

void print_trace(const std::vector<TraceFrame>& frames) {
  std::cout << "{\"schema\":\"orda-book/replay-trace/v1\",\"frames\":[";
  for (std::size_t index = 0; index < frames.size(); ++index) {
    if (index != 0) std::cout << ',';
    const TraceFrame& frame = frames[index];
    std::cout << "{\"step\":" << frame.step << ",\"event\":";
    if (frame.event) {
      print_trace_event(*frame.event);
    } else {
      std::cout << "null";
    }
    std::cout << ",\"accepted\":" << (frame.error == lob::BookError::None ? "true" : "false")
              << ",\"error\":\"" << lob::to_string(frame.error) << "\",\"trades\":";
    print_trace_trades(frame.trades);
    std::cout << ",\"bids\":";
    print_trace_orders(frame.bids);
    std::cout << ",\"asks\":";
    print_trace_orders(frame.asks);
    std::cout << '}';
  }
  std::cout << "]}\n";
}

}  // namespace

int main(int argc, char** argv) {
  const bool trace_json = argc == 3 && std::string(argv[1]) == "--trace-json";
  if (trace_json) {
    const std::string path = argv[2];
    lob::OrderBook book;
    std::vector<lob::Trade> trades;
    std::vector<TraceFrame> frames;
    frames.push_back(TraceFrame{0, std::nullopt, lob::BookError::None, {}, {}, {}});
    std::size_t event_count = 0;
    bool trace_too_large = false;
    const auto consume = [&book, &trades, &frames, &event_count, &trace_too_large](const lob::Event& event) {
      if (trace_too_large) return;
      if (event_count == kMaximumTraceEvents) {
        trace_too_large = true;
        return;
      }
      const std::size_t first_new_trade = trades.size();
      const lob::BookError error = book.process(event, trades);
      // A rejected event is part of the story: the book is unchanged and no
      // trade is emitted, so the frame records the refusal and keeps going.
      TraceFrame frame{++event_count, event, error, {}, book.orders(lob::Side::Bid),
                       book.orders(lob::Side::Ask)};
      for (std::size_t index = first_new_trade; index < trades.size(); ++index) {
        frame.trades.push_back(trades[index]);
      }
      frames.push_back(std::move(frame));
    };
    const lob::ParseResult parsed = lob::for_each_event_file(path, consume);
    if (!parsed.ok) {
      std::cerr << "parse error";
      if (parsed.error_line != 0) std::cerr << " on line " << parsed.error_line;
      std::cerr << ": " << parsed.error_message << '\n';
      return 1;
    }
    if (trace_too_large) {
      std::cerr << "trace export is limited to " << kMaximumTraceEvents << " events\n";
      return 1;
    }
    print_trace(frames);
    return 0;
  }

  if (argc < 2 || argc > 4) {
    print_usage();
    return 1;
  }

  const std::string path = argv[1];
  bool top_only = false;
  bool binary = false;
  for (int index = 2; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--top") {
      top_only = true;
    } else if (option == "--binary") {
      binary = true;
    } else {
      print_usage();
      return 1;
    }
  }

  lob::OrderBook book;
  std::vector<lob::Trade> trades;
  std::size_t event_count = 0;
  bool engine_failed = false;
  std::size_t failed_line = 0;
  lob::BookError failed_error = lob::BookError::None;
  const auto consume = [&book, &trades, &event_count, &engine_failed, &failed_line,
                        &failed_error](const lob::Event& event) {
    if (engine_failed) {
      return;
    }
    const lob::BookError error = book.process(event, trades);
    if (error != lob::BookError::None) {
      engine_failed = true;
      failed_line = event.line_number;
      failed_error = error;
      return;
    }
    ++event_count;
  };
  const lob::ParseResult parsed = binary ? lob::for_each_binary_event_file(path, consume)
                                         : lob::for_each_event_file(path, consume);
  if (!parsed.ok) {
    std::cerr << "parse error";
    if (parsed.error_line != 0) {
      std::cerr << " on record/line " << parsed.error_line;
    }
    std::cerr << ": " << parsed.error_message << '\n';
    return 1;
  }
  if (engine_failed) {
    std::cerr << "engine error on line " << failed_line << ": " << lob::to_string(failed_error)
              << '\n';
    return 1;
  }

  for (const lob::Trade& trade : trades) {
    print_trade(trade);
  }

  std::cout << "EVENTS " << event_count << '\n';
  std::cout << "TRADES " << trades.size() << '\n';
  std::cout << "LIVE_ORDERS " << book.live_order_count() << '\n';
  std::cout << book.format_book(!top_only);

  return 0;
}
