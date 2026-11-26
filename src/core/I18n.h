#pragma once
// The mod's words in the player's language (dev/I18N.md). English is the
// source and the key, as gettext has it: `Tr(L"Delay")` is "Delay" until a
// catalog says otherwise, so a missing file, a missing entry or an empty one
// is English and never a blank or a key. A catalog is a flat JSON object
// from the English to the translation, one per language, read once at load
// (game/I18n.cpp). No DAOLib.
//
// What is marked is what tools/i18n.py extracts: `Tr(L"...")`,
// `TrFormat(L"...", ...)` and `N_(L"...")` here, a literal kept in a table or
// handed on and translated where it is shown; and `tr("...")` and
// `trf("...", ...)` in the movies' ActionScript, which get their words from
// `Words()` through the plugin. Names the game gives -- a creature's, an
// ability's, the sheet's stats -- come in the game's own language already
// and are not marked; nor are the log and the events, which stay English for
// a bug report.

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Marks a literal for extraction and leaves it as it is: translated where
// it is shown, so that what is logged and compared stays English.
#define N_(text) text

namespace tb::i18n
{

struct Hash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::wstring_view text) const noexcept
    {
        return std::hash<std::wstring_view>{}(text);
    }
};

using Catalog = std::unordered_map<std::wstring, std::wstring, Hash, std::equal_to<>>;

// A catalog's text (UTF-8 JSON). An entry whose value is empty is left out,
// so an untranslated line reads in English; one whose value is not a string
// is said in `notes` and left out. Text that is not a flat JSON object is
// said and gives nothing.
[[nodiscard]] Catalog ParseCatalog(std::string_view json, std::vector<std::string> &notes);

// The catalog in force from now on: at load, before anything is shown.
void Use(Catalog catalog);

// How many lines the catalog in force translates.
[[nodiscard]] std::size_t Lines();

// The line in the language in force; the English where the catalog has none.
// The view lives as long as the catalog, or is the argument.
[[nodiscard]] std::wstring_view Tr(std::wstring_view english);

// Formatted after translation, with std::format's fields, so a translation
// may reorder them: "{} of {}" may read "{1} de {0}". A translation that
// does not format with these arguments is passed over for the English,
// whose fields are checked when it is compiled.
template <typename... Args>
[[nodiscard]] std::wstring TrFormat(std::wformat_string<Args...> english, const Args &...args)
{
    const std::wstring_view translated = Tr(english.get());
    if (translated.data() == english.get().data())
    {
        return std::vformat(english.get(), std::make_wformat_args(args...));
    }
    try
    {
        return std::vformat(translated, std::make_wformat_args(args...));
    }
    catch (const std::format_error &)
    {
        return std::vformat(english.get(), std::make_wformat_args(args...));
    }
}

// The catalog as the movies read it, English and translation of each line
// in turn: the English, a unit separator (31), the translation, a record
// separator (30). Empty with no catalog.
[[nodiscard]] std::wstring Words();

// Which of the catalogs on disk (their names without ".json": "en-US",
// "es-ES") reads for the language asked for, a Windows locale name or the
// ini's: the same name in any case; else one of the same language ("es-MX"
// reads es-ES), Chinese by script (zh-Hans and zh-CN, zh-Hant and zh-TW);
// else nothing, and the English is read.
[[nodiscard]] std::string PickLocale(std::string_view wanted, const std::vector<std::string> &available);

} // namespace tb::i18n
