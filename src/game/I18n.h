// The catalog, chosen and read once (core/I18n.h, dev/I18N.md): the ini's
// `[interface] language`, or with `auto` the game's own language (DragonAge.ini's Locale),
// names a file in Translations\ beside the plugin's dll. English needs none.
// At load, before the options' section and the keys' labels are added, whose
// names the game takes as they are given.
#pragma once

#include <string>
#include <string_view>

namespace tb::game
{

// `language` is the setting's text: "auto", "en-US", "es-ES", ... and
// `pluginPath` the plugin's dll (LoaderInfo::pluginPath). Said in the log
// either way; any failure leaves the words in English.
void LoadLanguage(std::string_view language, const std::wstring &pluginPath);

} // namespace tb::game
