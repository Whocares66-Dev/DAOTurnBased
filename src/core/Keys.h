// A key chord as the settings file writes it ("Ctrl+T", "Shift+F10", "F9"),
// and the Windows virtual-key code it stands for. The codes are plain
// numbers (winuser.h's VK_*), so this needs no Windows header. The plugin
// gives the chord to the game as one of its own keys where the game has a
// code for it (ScanCode), and polls it with GetAsyncKeyState where not.
#pragma once

#include <cstddef>
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

// The key's DirectInput scan code, which is the game's own word for a key
// in its bindings (DAOLib's dao/core/InputCode.h, read 2026-10-01: the
// game's defaults give pause 0x39 and quicksave 0x3F, Space and F5). For
// the keys DAOLib names, which are those the game's own bindings use:
// letters, digits, F1 to F12, and Space, Tab, Enter, Backspace, Insert,
// Delete, Home, End, PageUp, PageDown and the arrows. 0 for any other (the
// number pad, Pause, a key given by its code): whether the game's bindings
// take those has not been seen. The plugin's static_asserts keep these
// equal to DAOLib's.
[[nodiscard]] constexpr std::uint32_t ScanCode(std::uint8_t key)
{
    // Letters go by the keyboard's rows.
    constexpr std::string_view rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    constexpr std::uint32_t firsts[] = {0x10, 0x1E, 0x2C};
    for (std::size_t row = 0; key != 0 && row < 3; ++row)
    {
        if (const std::size_t at = rows[row].find(static_cast<char>(key)); at != std::string_view::npos)
        {
            return firsts[row] + static_cast<std::uint32_t>(at);
        }
    }
    if (key >= '1' && key <= '9')
    {
        return 0x02U + (key - '1');
    }
    if (key >= 0x70 && key <= 0x79) // F1 to F10
    {
        return 0x3BU + (key - 0x70);
    }
    switch (key)
    {
    case '0':
        return 0x0B;
    case 0x7A: // F11
        return 0x57;
    case 0x7B: // F12
        return 0x58;
    case 0x20: // Space
        return 0x39;
    case 0x09: // Tab
        return 0x0F;
    case 0x0D: // Enter
        return 0x1C;
    case 0x08: // Backspace
        return 0x0E;
    case 0x2D: // Insert
        return 0xD2;
    case 0x2E: // Delete
        return 0xD3;
    case 0x24: // Home
        return 0xC7;
    case 0x23: // End
        return 0xCF;
    case 0x21: // PageUp
        return 0xC9;
    case 0x22: // PageDown
        return 0xD1;
    case 0x26: // Up
        return 0xC8;
    case 0x28: // Down
        return 0xD0;
    case 0x25: // Left
        return 0xCB;
    case 0x27: // Right
        return 0xCD;
    default:
        return 0;
    }
}

} // namespace tb
