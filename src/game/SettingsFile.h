// turnbased.ini on disk: beside the logs, in Documents\BioWare\Dragon
// Age\daolib\ (DAOLib's dao::LogDirectory), which the player can write to,
// unlike the game's folder under Program Files. core/Settings.h reads and
// writes the text; this reads and writes the file.
#pragma once

#include "core/Settings.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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

// For a test server (Eluvian's turnbased_settings): the file as it is now
// (the defaults when there is none), and one setting written into it
// (core's WithValue), applied at the next reload. Answers the warnings the
// whole file then parses with.
std::string SettingsText();
std::vector<std::string> SaveSetting(std::string_view section, std::string_view key, std::string_view value);

} // namespace tb::game
