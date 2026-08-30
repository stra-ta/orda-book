#include "binary_event_codec.hpp"
#include "event_parser.hpp"
#include "order_book.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
  std::cerr << "usage: orda_replay <event_file> [--top] [--binary]\n";
}

void print_trade(const lob::Trade& trade) {
  std::cout << "TRADE resting=" << trade.resting_order_id << " incoming=" << trade.incoming_order_id
            << " price=" << trade.price << " qty=" << trade.qty
            << " aggressor=" << lob::to_string(trade.aggressor_side) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
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
