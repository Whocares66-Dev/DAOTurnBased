#include "game/I18n.h"

#include "core/I18n.h"

#include "dao/Strings.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace tb::game
{
namespace
{

// The game's own language, as a locale name ("es-es", "de-de"): the Locale of
// DragonAge.ini, the setting that picks the game's own talk tables (DAOLib's
// dao::strings::Locale), "en-us" where there is none.
std::string GameLanguage()
{
    std::string out;
    for (const wchar_t c : dao::strings::Locale())
    {
        out += c < 0x80 ? static_cast<char>(c) : '?';
    }
    return out;
}

std::vector<std::string> CatalogsIn(const std::filesystem::path &folder)
{
    std::vector<std::string> names;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(folder, error))
    {
        if (entry.is_regular_file(error) && entry.path().extension() == L".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

} // namespace

void LoadLanguage(std::string_view language, const std::wstring &pluginPath)
{
    const bool automatic = language.empty() || language == "auto";
    const std::string wanted = automatic ? GameLanguage() : std::string{language};
    const std::filesystem::path folder = std::filesystem::path{pluginPath}.parent_path() / L"Translations";
    const std::string locale = tb::i18n::PickLocale(wanted, CatalogsIn(folder));
    if (locale.empty() || locale == "en-US")
    {
        // A game in a language with no catalog is no fault of the player's, and not worth a warning.
        if (automatic || locale == "en-US")
        {
            spdlog::info("words in English (language {} asked for: {})", automatic ? "auto" : std::string{language},
                         wanted.empty() ? "none" : wanted);
        }
        else
        {
            spdlog::warn("no catalog for language {} in {} -- words in English", language, folder.string());
        }
        return;
    }

    const std::filesystem::path file = folder / (locale + ".json");
    std::ifstream in(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (!in && text.empty())
    {
        spdlog::warn("{} cannot be read -- words in English", file.string());
        return;
    }
    std::vector<std::string> notes;
    auto catalog = tb::i18n::ParseCatalog(text, notes);
    for (const std::string &note : notes)
    {
        spdlog::warn("{}: {}", file.filename().string(), note);
    }
    spdlog::info("words in {} ({}): {} lines translated", locale, file.filename().string(), catalog.size());
    tb::i18n::Use(std::move(catalog));
}

} // namespace tb::game
