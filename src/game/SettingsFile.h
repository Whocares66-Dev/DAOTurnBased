// turnbased.ini on disk: beside the logs, in Documents\BioWare\Dragon
// Age\daolib\ (DAOLib's dao::LogDirectory), which the player can write to,
// unlike the game's folder under Program Files. core/Settings.h reads and
// writes the text; this reads and writes the file.
#pragma once

#include "core/Settings.h"

#include <filesystem>

namespace tb::game
{

std::filesystem::path SettingsPath();

// The settings, the default file written first if there is none. Every
// warning goes to the log.
Settings LoadSettings();

// `enabled` written into the file, the rest as the player left it.
void SaveEnabled(bool enabled);

// True the first time it is asked after the file changed on disk (its
// time stamp), so an edit takes effect without restarting the game.
bool SettingsFileChanged();

} // namespace tb::game
