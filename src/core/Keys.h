// A key chord as the settings file writes it ("Ctrl+T", "Shift+F10", "F9"),
// and the Windows virtual-key code it stands for. The codes are plain
// numbers (winuser.h's VK_*), so this needs no Windows header; src/game
// polls them with GetAsyncKeyState.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tb
{

struct KeyChord
{
    std::uint8_t key = 0; // a VK_* code; 0 is no key
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool operator==(const KeyChord &) const = default;
};

// "Ctrl+Shift+T" and the like: modifiers (Ctrl, Shift, Alt) then one key,
// joined by '+', case and spaces ignored. The key is a letter, a digit,
// F1-F24, NumPad0-9, or one of Space, Tab, Enter, Backspace, Insert, Delete,
// Home, End, PageUp, PageDown, Pause, ScrollLock, Up, Down, Left, Right,
// or any key by its VK_* code in two hex digits ("0xBA", the US ';' key).
// "None" is no key. Nothing for anything else.
[[nodiscard]] std::optional<KeyChord> ParseKeyChord(std::string_view text);

// The chord as ParseKeyChord reads it back.
[[nodiscard]] std::string FormatKeyChord(const KeyChord &chord);

} // namespace tb
