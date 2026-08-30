// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace lob {

// The binary format is versioned, little-endian, and uses one fixed-width
// record per event so benchmark-sized replays do not need a text parser or a
// complete in-memory event vector.
constexpr std::uint32_t kBinaryEventFormatVersion = 1;

bool write_binary_event_file(const std::string& path, const std::vector<Event>& events,
                            std::string& error_message);
ParseResult parse_binary_event_file(const std::string& path);
ParseResult for_each_binary_event_file(const std::string& path, const EventCallback& callback);

}  // namespace lob
