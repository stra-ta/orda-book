// SPDX-License-Identifier: MIT
#include "binary_event_codec.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace lob {
namespace {

constexpr std::array<char, 8> kMagic{'O', 'R', 'D', 'A', 'B', 'I', 'N', '\x01'};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kRecordBytes = 96;

void put_u32(std::array<std::uint8_t, kHeaderBytes>& bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void put_u64(std::array<std::uint8_t, kRecordBytes>& bytes, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

std::uint32_t get_u32(const std::array<std::uint8_t, kHeaderBytes>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

std::uint64_t get_u64(const std::array<std::uint8_t, kRecordBytes>& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

std::int64_t decode_i64(const std::array<std::uint8_t, kRecordBytes>& bytes, std::size_t offset) {
  const std::uint64_t encoded = get_u64(bytes, offset);
  std::int64_t decoded = 0;
  static_assert(sizeof(decoded) == sizeof(encoded));
  std::memcpy(&decoded, &encoded, sizeof(decoded));
  return decoded;
}

void encode_i64(std::array<std::uint8_t, kRecordBytes>& bytes, std::size_t offset,
                std::int64_t value) {
  std::uint64_t encoded = 0;
  static_assert(sizeof(encoded) == sizeof(value));
  std::memcpy(&encoded, &value, sizeof(encoded));
  put_u64(bytes, offset, encoded);
}

std::array<std::uint8_t, kHeaderBytes> make_header() {
  std::array<std::uint8_t, kHeaderBytes> header{};
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    header[index] = static_cast<std::uint8_t>(kMagic[index]);
  }
  put_u32(header, 8, kBinaryEventFormatVersion);
  put_u32(header, 12, static_cast<std::uint32_t>(kRecordBytes));
  return header;
}

std::array<std::uint8_t, kRecordBytes> encode_event(const Event& event) {
  std::array<std::uint8_t, kRecordBytes> record{};
  put_u64(record, 0, static_cast<std::uint64_t>(event.type));
  put_u64(record, 8, event.order_id);
  put_u64(record, 16, static_cast<std::uint64_t>(event.side == Side::Ask));
  encode_i64(record, 24, event.price);
  encode_i64(record, 32, event.qty);
  put_u64(record, 40, static_cast<std::uint64_t>(event.order_type));
  put_u64(record, 48, static_cast<std::uint64_t>(event.time_in_force));
  put_u64(record, 56, event.post_only ? 1U : 0U);
  encode_i64(record, 64, event.new_price);
  encode_i64(record, 72, event.new_qty);
  put_u64(record, 80, static_cast<std::uint64_t>(event.line_number));
  return record;
}

Event decode_event(const std::array<std::uint8_t, kRecordBytes>& record, std::size_t record_number,
                   std::string& error) {
  Event event;
  const std::uint64_t raw_type = get_u64(record, 0);
  const std::uint64_t raw_side = get_u64(record, 16);
  const std::uint64_t raw_order_type = get_u64(record, 40);
  const std::uint64_t raw_tif = get_u64(record, 48);
  if (raw_type > static_cast<std::uint64_t>(EventType::Modify) || raw_side > 1U ||
      raw_order_type > static_cast<std::uint64_t>(OrderType::Market) ||
      raw_tif > static_cast<std::uint64_t>(TimeInForce::FillOrKill)) {
    error = "invalid enum in binary event record " + std::to_string(record_number);
    return event;
  }
  event.type = static_cast<EventType>(raw_type);
  event.order_id = get_u64(record, 8);
  event.side = raw_side == 0 ? Side::Bid : Side::Ask;
  event.price = decode_i64(record, 24);
  event.qty = decode_i64(record, 32);
  event.order_type = static_cast<OrderType>(raw_order_type);
  event.time_in_force = static_cast<TimeInForce>(raw_tif);
  const std::uint64_t raw_post_only = get_u64(record, 56);
  if (raw_post_only > 1U) {
    error = "invalid post-only flag in binary event record " + std::to_string(record_number);
    return event;
  }
  event.post_only = raw_post_only != 0;
  event.new_price = decode_i64(record, 64);
  event.new_qty = decode_i64(record, 72);
  const std::uint64_t raw_line_number = get_u64(record, 80);
  if (raw_line_number > std::numeric_limits<std::size_t>::max()) {
    error = "line number overflows size_t in binary event record " + std::to_string(record_number);
    return event;
  }
  event.line_number = static_cast<std::size_t>(raw_line_number);
  return event;
}

ParseResult fail_binary(std::size_t record_number, std::string message) {
  ParseResult result;
  result.ok = false;
  result.error_line = record_number;
  result.error_message = std::move(message);
  return result;
}

ParseResult parse_binary_impl(std::istream& input, const EventCallback* callback) {
  std::array<std::uint8_t, kHeaderBytes> header{};
  input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
  if (input.gcount() != static_cast<std::streamsize>(header.size())) {
    return fail_binary(0, "truncated binary event header");
  }
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    if (header[index] != static_cast<std::uint8_t>(kMagic[index])) {
      return fail_binary(0, "invalid binary event magic");
    }
  }
  if (get_u32(header, 8) != kBinaryEventFormatVersion || get_u32(header, 12) != kRecordBytes) {
    return fail_binary(0, "unsupported binary event format");
  }

  ParseResult result;
  std::array<std::uint8_t, kRecordBytes> record{};
  std::size_t record_number = 0;
  while (input.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()))) {
    ++record_number;
    std::string error;
    Event event = decode_event(record, record_number, error);
    if (!error.empty()) {
      return fail_binary(record_number, std::move(error));
    }
    if (callback != nullptr) {
      (*callback)(event);
    } else {
      result.events.push_back(event);
    }
  }
  if (!input.eof()) {
    return fail_binary(record_number + 1U, "truncated binary event record");
  }
  return result;
}

}  // namespace

bool write_binary_event_file(const std::string& path, const std::vector<Event>& events,
                             std::string& error_message) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error_message = "failed to open binary output file";
    return false;
  }
  const auto header = make_header();
  output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
  for (const Event& event : events) {
    const auto record = encode_event(event);
    output.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
  }
  if (!output) {
    error_message = "failed while writing binary event file";
    return false;
  }
  return true;
}

ParseResult parse_binary_event_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return fail_binary(0, "failed to open binary event file");
  }
  return parse_binary_impl(input, nullptr);
}

ParseResult for_each_binary_event_file(const std::string& path, const EventCallback& callback) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return fail_binary(0, "failed to open binary event file");
  }
  return parse_binary_impl(input, &callback);
}

}  // namespace lob
