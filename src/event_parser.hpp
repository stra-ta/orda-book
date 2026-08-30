#pragma once

#include "types.hpp"

#include <istream>
#include <string>

namespace lob {

ParseResult parse_event_stream(std::istream& input);
ParseResult parse_event_file(const std::string& path);
ParseResult for_each_event_stream(std::istream& input, const EventCallback& callback);
ParseResult for_each_event_file(const std::string& path, const EventCallback& callback);

}  // namespace lob
