// What the player is told when an order of a party member's comes to
// nothing and is given back (issue 21). The engine refuses some orders only
// once they are under way: a spell at a target behind a shut door, an attack
// with no room left round its target, a walk it finds no path for. Nothing
// was done, so the machine gives the action back
// (TurnMachine::OnCommandFailed), and said why only in a floaty over the
// character: a moment's word, which a player may miss in a fight and a tool
// that drives the game does not see, and the same "No way through" for a
// door, for no room and for no path. In the playthrough (issue 3,
// 2026-10-03) Sten's attack was refused eighteen times in one fight at a
// doorway, six of his turns ending with nothing spent, and Morrigan's spell
// thirteen times at a shut door: each try is a decision in turns.
//
// So the bar's caption says it too, for TurnMachine::kNoticeMs or until the
// player's next order is let through: the reason in a few words, whose it
// was, and that it is given back.
//
// The reasons are the engine's command results as DAOLib names them
// (dao::commands::ResultName, script.ldf's COMMAND_FAILED_*; the game layer
// passes the name, since core names no dao:: type), those that did nothing
// (src/game/Gate.cpp, DidNothing):
//   COMMAND_FAILED_PATH_ACTION_REQUIRED    a door or the like on the way, which a walker must act on to go on
//   COMMAND_FAILED_NO_SPACE_IN_MELEE_RING  no place left round the target to strike it from
//   COMMAND_FAILED_INVALID_PATH            no path there
//   COMMAND_FAILED_NO_LINE_OF_SIGHT        the target is not in sight
//   COMMAND_FAILED_INVALID_DATA            nothing to say why
// and the machine's own for an action that was over at once and did
// nothing, which has no reason either.
//
// Three of them the engine can be asked for before the order is let through
// (NoAttack, core/Types.h; issue 27): no place, no path, no line for a shot.
// Asked and answered no, the attack is refused before a step, in the words
// the failure under way would have had, and the caption says it is refused:
// nothing was taken, so nothing is given back.
#pragma once

#include "core/Types.h"

#include <string>
#include <string_view>

namespace tb
{

// Why, in a few words: what floats over the character, and what the
// caption's line begins with.
[[nodiscard]] std::wstring_view WhyGivenBack(std::string_view why);

// The caption's line, by those words: "No room at the target: Sten's
// attack is given back". An attack and an ability are named; anything else
// is "action".
[[nodiscard]] std::wstring GivenBackLine(std::wstring_view words, std::wstring_view name, CommandType type);

// An attack the engine has no way to make, asked before it is let through:
// the words, the same as a failure's for the same reason; the engine's
// answer in plain words, for the log, the refusal's event and the state; and
// the caption's line, "No room at the target: Sten's attack is refused".
[[nodiscard]] std::wstring_view WhyNoAttack(NoAttack why);
[[nodiscard]] std::string_view NoAttackNote(NoAttack why);
[[nodiscard]] std::wstring RefusedLine(std::wstring_view words, std::wstring_view name, CommandType type);

// And for an attack kept from the turn before, asked the same as its turn
// comes and answered no: "No room at the target: Sten's attack is not made".
// It was no order of this turn's to refuse, and what this turn paid for it
// is back in its bar with no word needed: the bar shows it whole.
[[nodiscard]] std::wstring NotMadeLine(std::wstring_view words, std::wstring_view name, CommandType type);

} // namespace tb
