// The turn bar's view as its movie reads it (dev/UI.md, "The turn order
// bar"): the movie asks with ExternalInterface.call("TB_GetView") and gets
// one string back (src/game/Bar.cpp answers). ActionScript 2 has no JSON,
// but String.split takes this apart with no escaping: cards are apart by
// U+001E and fields by U+001F, control characters no name has (and any a
// name did have are dropped). Numbers are plain decimals, the same in every
// locale.
//
//   the head:  version, active, round, current's id, player's turn,
//              actions left, movement left, movement at most
//   each card: id, side (0 party, 1 ally, 2 enemy), rank, health (0-100),
//              down, current, yours (the player commands it), round, name,
//              tag, portrait (its texture's name, "portrait_player0.dds",
//              core/Portraits.h; empty for none yet), mana or stamina
//              (0-100), mana (1 for a mage's, 0 for stamina)
//
// Flags are 0 or 1, movement is meters with one decimal. The version goes up
// when the fields change, and a movie that sees another stays blank.
#pragma once

#include "core/Portraits.h"
#include "core/TurnMachine.h"

#include <string>

namespace tb
{

inline constexpr wchar_t kBarCard = 0x1E;
inline constexpr wchar_t kBarField = 0x1F;
inline constexpr int kBarVersion = 4;

// The portraits come from the game's side, the rest from the machine.
[[nodiscard]] std::wstring EncodeBar(const TurnView &view, const PortraitBook &portraits);
[[nodiscard]] std::wstring EncodeBar(const TurnView &view);

} // namespace tb
