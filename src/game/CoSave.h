// The mod's file in each of the game's saves, over DAOLib's dao::saves
// (<the save's folder>\turnbased.daolib): what belongs to the playthrough and
// not to the session or to turnbased.ini. So far one thing: whose turns their
// tactics play, a choice a companion (core/PartyTactics.h has the rule and the
// file's text; issue 46, the user, 2026-10-05: "This is also a good
// opportunity to exercise the cosave").
//
// Written each time the game has written a save, whatever is chosen and with
// nothing chosen too, so that a save made over another in the same slot never
// keeps the older one's file. Read when a playthrough comes into the world: a
// save's own choices, or none for a save that has no file (one from before
// this, or from a game without the mod), for a file this build does not read,
// and for a new game. What was chosen in the session before is gone then: it
// was the other playthrough's.
//
// DAOLib's own cautions (dao/Saves.h), which the mod is the first to meet in
// play: its two callbacks had not been seen running when it was written (the
// hooks under them had), a new game's among them; and one early quicksave
// wrote its files without the hooked write being called, so that such a save
// would not be heard, and would keep whatever file its slot had. The log says
// each write and each read, with the slot, so that a save with no line is
// seen for one (dev/TESTING.md, "The co-save").
#pragma once

#include "core/TurnMachine.h"

#include <mutex>

namespace tb::game
{

// Asks DAOLib to call as the game saves and as a playthrough comes into the
// world. Both come on the game's thread; each takes the machine's mutex for
// as long as the machine takes to answer, and not while the file is read or
// written. From DAOLib_Load.
void InstallCoSave(TurnMachine &machine, std::mutex &mutex);

} // namespace tb::game
