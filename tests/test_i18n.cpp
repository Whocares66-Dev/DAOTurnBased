#include "core/GivenBack.h"
#include "core/I18n.h"
#include "core/Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace tb;

namespace
{

// A catalog in force for a case, and English again after it.
struct Language
{
    explicit Language(const std::string &json)
    {
        std::vector<std::string> notes;
        i18n::Use(i18n::ParseCatalog(json, notes));
        CHECK(notes.empty());
    }
    ~Language()
    {
        try
        {
            i18n::Use({});
        }
        catch (...) // NOLINT(bugprone-empty-catch): a test's tidy-up has nowhere to say it
        {
        }
    }
    Language(const Language &) = delete;
    Language &operator=(const Language &) = delete;
};

} // namespace

TEST_CASE("with no catalog every line reads in English", "[i18n]")
{
    CHECK(i18n::Tr(L"Delay") == L"Delay");
    CHECK(i18n::TrFormat(L"{} waits", std::wstring{L"Alistair"}) == L"Alistair waits");
    CHECK(i18n::Words().empty());
}

TEST_CASE("a catalog's line is the translation; one it lacks, or leaves empty, is English", "[i18n]")
{
    const Language language(R"json({"Delay": "Retrasar", "End": "", "Casting": "Lanzando"})json");
    CHECK(i18n::Tr(L"Delay") == L"Retrasar");
    CHECK(i18n::Tr(L"End") == L"End");
    CHECK(i18n::Tr(L"Held") == L"Held");
    CHECK(i18n::Lines() == 2);
}

TEST_CASE("a catalog reads escapes, accents, Chinese and a byte order mark", "[i18n]")
{
    std::vector<std::string> notes;
    const auto catalog =
        i18n::ParseCatalog("\xEF\xBB\xBF{\"a\\\"b\": \"c\\n\\u00e9\", \"x\": \"\xE5\x9B\x9E\xE5\x90\x88\", "
                           "\"y\": \"\\ud83d\\ude00\"}",
                           notes);
    CHECK(notes.empty());
    CHECK(catalog.at(L"a\"b") == L"c\n\u00e9");
    CHECK(catalog.at(L"x") == L"\u56de\u5408");
    CHECK(catalog.at(L"y") == L"\U0001F600");
}

TEST_CASE("a catalog that is not one is said, and gives what it can", "[i18n]")
{
    std::vector<std::string> notes;
    CHECK(i18n::ParseCatalog("", notes).empty());
    CHECK(notes.size() == 1);

    notes.clear();
    CHECK(i18n::ParseCatalog("[1, 2]", notes).empty());
    CHECK(notes.size() == 1);

    notes.clear();
    CHECK(i18n::ParseCatalog(R"json({"a": "b", )json", notes).size() == 1);
    CHECK(!notes.empty());

    notes.clear();
    const auto some = i18n::ParseCatalog(R"json({"a": 3, "b": {"c": "d"}, "e": "f"})json", notes);
    CHECK(some.size() == 1);
    CHECK(some.at(L"e") == L"f");
    CHECK(notes.size() == 2);
}

TEST_CASE("a translation may reorder the fields of a line", "[i18n]")
{
    const Language language(R"json({"{0}: {1} of {2}": "x", "{} of {}": "{1} de {0}"})json");
    CHECK(i18n::TrFormat(L"{} of {}", 3, 5) == L"5 de 3");
}

TEST_CASE("a translation that does not format with its arguments reads in English", "[i18n]")
{
    const Language language(R"json({"{} waits": "{} {} espera", "{} loses the turn": "{ pierde"})json");
    CHECK(i18n::TrFormat(L"{} waits", std::wstring{L"Morrigan"}) == L"Morrigan waits");
    CHECK(i18n::TrFormat(L"{} loses the turn", std::wstring{L"Sten"}) == L"Sten loses the turn");
}

TEST_CASE("the lines of a given-back notice are said in the language, whole", "[i18n]")
{
    const Language language(R"json({
        "A door is in the way": "Hay una puerta en el camino",
        "attack": "ataque",
        "{}: {}'s {} is given back": "{0}: se devuelve ({2} de {1})"
    })json");
    CHECK(GivenBackLine(WhyGivenBack("PATH_ACTION_REQUIRED"), L"Morrigan", CommandType::Attack) ==
          L"Hay una puerta en el camino: se devuelve (ataque de Morrigan)");
    // What is logged, and what a test compares, stays English.
    CHECK(WhyGivenBack("PATH_ACTION_REQUIRED") == L"A door is in the way");
}

TEST_CASE("the movies are given the catalog as pairs", "[i18n]")
{
    const Language language(R"json({"Delay": "Retrasar"})json");
    CHECK(i18n::Words() == std::wstring{L"Delay\x1F"} + L"Retrasar\x1E");
}

TEST_CASE("a language asked for reads the catalog of its own name, its language, or none", "[i18n]")
{
    const std::vector<std::string> have{"en-US", "es-ES", "zh-CN"};
    CHECK(i18n::PickLocale("es-ES", have) == "es-ES");
    CHECK(i18n::PickLocale("ES-es", have) == "es-ES");
    CHECK(i18n::PickLocale("es-MX", have) == "es-ES");
    CHECK(i18n::PickLocale("es", have) == "es-ES");
    CHECK(i18n::PickLocale("zh-Hans-CN", have) == "zh-CN");
    CHECK(i18n::PickLocale("zh-Hant-TW", have).empty());
    CHECK(i18n::PickLocale("zh-TW", have).empty());
    CHECK(i18n::PickLocale("de-DE", have).empty());
    CHECK(i18n::PickLocale("", have).empty());
    CHECK(i18n::PickLocale("es-ES", {}).empty());
    CHECK(i18n::PickLocale("zh-TW", {"zh-CN", "zh-TW"}) == "zh-TW");
}

TEST_CASE("the language setting is auto until the file says one", "[i18n][settings]")
{
    CHECK(Settings{}.language == "auto");
    CHECK(ParseSettings(DefaultSettingsFile()).settings.language == "auto");
    const auto parsed = ParseSettings("[interface]\nlanguage = es-ES\n");
    CHECK(parsed.warnings.empty());
    CHECK(parsed.settings.language == "es-ES");
    CHECK(ParseSettings("[interface]\nlanguage =\n").settings.language == "auto");
}
