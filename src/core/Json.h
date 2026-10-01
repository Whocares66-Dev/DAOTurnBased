// Just enough JSON for the machine's events and state, and for a test
// server's flat arguments (eluvian-agent's MCP tools, 2026-09-30): strings
// escaped as JSON wants, and a field read out of a flat object. No library:
// src/core takes nothing it does not need.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tb
{

// text as a JSON string's body: quotes, backslashes and control characters
// escaped; the rest, UTF-8 included, as it is.
void AppendJsonString(std::string &out, std::string_view text);

// A field of a flat JSON object ({"key": "text", "n": 3, "on": true}): a
// string's text, unescaped, or a number's or true's or false's own text.
// Nothing for a missing key, a nested value, or text that is not JSON.
[[nodiscard]] std::optional<std::string> JsonField(std::string_view json, std::string_view key);

// A field's text as a whole number, all of it: digits only, so 42.5, 42e2,
// -1, "42 or so" and what does not fit are nothing, where from_chars alone
// took the 42 off the front (the review's TB-4, 2026-09-30: a seed that
// was not the one asked for).
[[nodiscard]] std::optional<std::uint64_t> WholeNumber(std::string_view text);

} // namespace tb
