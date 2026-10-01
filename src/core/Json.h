// Just enough JSON for the machine's events and state, and for a test
// server's flat arguments (eluvian-agent's MCP tools, 2026-09-30): strings
// escaped as JSON wants, and a field read out of a flat object. No library:
// src/core takes nothing it does not need.
#pragma once

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

} // namespace tb
