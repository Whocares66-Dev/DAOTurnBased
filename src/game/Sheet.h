// A creature's sheet as the engine has it now (core/Sheet.h; issue 112):
// its properties, the words and precisions of the game's properties table,
// its classes by the classes table, and what each hand holds. Read when the
// turn bar's movie asks for a card's (game/Bar.h, TB_Inspect), on the
// scripts' thread, as the frame callback is.
#pragma once

#include "core/Sheet.h"
#include "core/Types.h"

#include <string>

namespace tb::game
{

// What core's BuildSheet is made from. `name` and `mage` are the bar's card's
// (the machine's view has them already). An id of kNoObject for a creature
// the engine does not have. `fresh` is the first read since the pointer came
// to the card: the game's script works out the creature's damage per hand
// then (dao::damage::PerHand; issue 113), as it does when the character
// record opens.
[[nodiscard]] SheetFacts ReadSheet(ObjectId id, const std::wstring &name, bool mage, bool fresh);

} // namespace tb::game
