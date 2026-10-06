// The turn bar's view as its movie reads it (dev/UI.md, "The turn order
// bar"): the movie asks with ExternalInterface.call("TB_GetView") and gets
// one string back (src/game/Bar.cpp answers). ActionScript 2 has no JSON,
// but String.split takes this apart with no escaping: cards are apart by
// U+001E and fields by U+001F, control characters no name has (and any a
// name did have are dropped). Numbers are plain decimals, the same in every
// locale.
//
//   the head:  version, active, round, current's id, player's turn,
//              movement left, movement at most, the bar of time (what is
//              left of it, the whole, a turn's own seconds), then what
//              later versions added (Bar.cpp says each where it writes it),
//              then the row (version 9): how many turns to come stand
//              before it, how many it shows, how many come after; and last
//              (version 10) a line for the caption to say for a while, an
//              action given back and why (core/GivenBack.h), empty for none.
//              Version 11 (issue 50) took two fields out, the actions left
//              and whether a turn is a bar of time: it always is.
//   each card: id, side (0 party, 1 ally, 2 enemy), rank, health (0-100),
//              down, current, yours (the player commands it), round, name,
//              tag, portrait (its texture's name, "portrait_player0.dds",
//              core/Portraits.h; empty for none yet), mana or stamina
//              (0-100), mana (1 for a mage's, 0 for stamina)
//
// The cards are every turn to come, the row or not (version 9; a row's
// worth before): the movie draws those of the row, and reads the rest for
// what it says of a creature that has no card just now (a pending action's
// words, the current turn's own). Portraits are got for the row's alone.
//
// Flags are 0 or 1, movement is meters with one decimal. The version goes up
// when the fields change, and a movie that sees another stays blank.
#pragma once

#include "core/BarWindow.h"
#include "core/Portraits.h"
#include "core/TurnMachine.h"

#include <string>

namespace tb
{

inline constexpr wchar_t kBarCard = 0x1E;
inline constexpr wchar_t kBarField = 0x1F;
inline constexpr int kBarVersion = 11;

// The portraits and the row come from the game's side, the rest from the
// machine.
[[nodiscard]] std::wstring EncodeBar(const TurnView &view, const PortraitBook &portraits, const BarSpan &row);
// With no portraits, the row at the list's beginning.
[[nodiscard]] std::wstring EncodeBar(const TurnView &view);

} // namespace tb
