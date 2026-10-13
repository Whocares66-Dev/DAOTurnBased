// A creature's sheet (core/Sheet.h): what the record would show, composed
// from what the game layer reads, and the string the movie takes apart.
#include "core/Bar.h"
#include "core/Sheet.h"
#include "core/Text.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace tb;

namespace
{

// The game's properties table as it reads in English (properties.gda and
// core_en-us.tlk, 2026-10-13): the rows the sheet uses, their precisions and
// UIMappings, and a few it does not.
std::vector<PropertyRow> GameTable()
{
    return {
        {.id = 1, .label = L"Strength", .precision = 0, .uiMapping = 0},
        {.id = 2, .label = L"Dexterity", .precision = 0, .uiMapping = 0},
        {.id = 3, .label = L"Willpower", .precision = 0, .uiMapping = 0},
        {.id = 4, .label = L"Magic", .precision = 0, .uiMapping = 0},
        {.id = 5, .label = L"Cunning", .precision = 0, .uiMapping = 0},
        {.id = 6, .label = L"Constitution", .precision = 0, .uiMapping = 0},
        {.id = 7, .label = L"Health", .precision = 0, .uiMapping = 0},
        {.id = 8, .label = L"Mana Stamina", .precision = 0, .uiMapping = 0},
        {.id = 9, .label = L"Attack", .precision = 0, .uiMapping = 0},
        {.id = 10, .label = L"Defense", .precision = 0, .uiMapping = 0},
        {.id = 11, .label = L"Armor", .precision = 0, .uiMapping = 0},
        {.id = 13, .label = L"Spellpower", .precision = 0, .uiMapping = 0},
        {.id = 15, .label = L"Level", .precision = 0, .uiMapping = 0},
        {.id = 21, .label = L"Melee critical chance: <percent/>", .precision = 2, .uiMapping = 2},
        {.id = 23, .label = L"Ranged critical chance: <percent/>", .precision = 2, .uiMapping = 2},
        {.id = 32, .label = L"Mental resistance check", .precision = 0, .uiMapping = 1},
        {.id = 33, .label = L"Physical resistance check", .precision = 0, .uiMapping = 1},
        {.id = 39, .label = L"Damage bonus", .precision = 0, .uiMapping = 2},
        {.id = 42, .label = L"Fire resistance", .precision = 0, .uiMapping = 3},
        {.id = 43, .label = L"Cold resistance", .precision = 0, .uiMapping = 3},
        {.id = 44, .label = L"Electrical resistance", .precision = 0, .uiMapping = 3},
        {.id = 45, .label = L"Nature resistance", .precision = 0, .uiMapping = 3},
        {.id = 46, .label = L"Spirit resistance", .precision = 0, .uiMapping = 3},
        {.id = 49, .label = L"Damage (offhand)", .precision = 1, .uiMapping = 0},
        {.id = 50, .label = L"Damage (main hand)", .precision = 1, .uiMapping = 0},
        {.id = 53, .label = L"Armor penetration", .precision = 0, .uiMapping = 0},
    };
}

// Lyna, a rogue and a duelist at level 28 with a dagger in each hand: the
// record's picture in the issue, its numbers.
// A property's value on the creature, on its row of the table.
void Set(SheetFacts &facts, std::int32_t id, float total, float modifier = 0.0f)
{
    const auto row = std::ranges::find(facts.table, id, &PropertyRow::id);
    REQUIRE(row != facts.table.end());
    row->total = total;
    row->modifier = modifier;
}

// What is left of a depletable property, and its whole.
void SetLeft(SheetFacts &facts, std::int32_t id, float current, float total)
{
    const auto row = std::ranges::find(facts.table, id, &PropertyRow::id);
    REQUIRE(row != facts.table.end());
    row->current = current;
    row->total = total;
}

SheetFacts Lyna()
{
    SheetFacts facts;
    facts.id = 0x1234;
    facts.name = L"Lyna";
    facts.classes = {{.name = L"Rogue", .icon = L"classico_rogue"}, {.name = L"Duelist", .icon = L"classico_duelist"}};
    facts.currentClass = L"Rogue";
    facts.table = GameTable();
    Set(facts, 1, 40, 4);
    Set(facts, 2, 69, 10);
    Set(facts, 3, 27);
    Set(facts, 4, 18, -2);
    Set(facts, 5, 41);
    Set(facts, 6, 31);
    Set(facts, 9, 85, 5);
    Set(facts, 10, 45);
    Set(facts, 11, 12, 3);
    Set(facts, 15, 28);
    SetLeft(facts, 7, 399.6f, 430);
    SetLeft(facts, 8, 233, 283);
    facts.energyWord = L"Stamina";
    facts.critWord = L"Critical chance: <percent/>";
    facts.penetrationWord = L"Armor penetration";
    Set(facts, 21, 4.5f);
    Set(facts, 23, 3.0f);
    Set(facts, 32, 28);
    Set(facts, 33, 55);
    Set(facts, 42, 60);
    Set(facts, 45, 10);
    Set(facts, 46, -10);
    Set(facts, 49, 8.3f);
    Set(facts, 50, 12.5f, 1);
    Set(facts, 53, 2);
    facts.mainHand = {.kind = WeaponKind::Melee, .crit = 1.5f, .penetration = 7};
    facts.offHand = {.kind = WeaponKind::Melee, .crit = 0.5f, .penetration = 5};
    return facts;
}

// A copy: the sheet is often a temporary.
SheetSection Section(const CreatureSheet &sheet, std::wstring_view title)
{
    const auto found = std::ranges::find(sheet.sections, title, &SheetSection::title);
    INFO("the section " << Utf8(title));
    REQUIRE(found != sheet.sections.end());
    return *found;
}

std::vector<std::wstring> Labels(const SheetSection &section)
{
    std::vector<std::wstring> out;
    out.reserve(section.rows.size());
    for (const SheetRow &row : section.rows)
    {
        out.push_back(row.label);
    }
    return out;
}

std::vector<std::wstring> Values(const SheetSection &section)
{
    std::vector<std::wstring> out;
    out.reserve(section.rows.size());
    for (const SheetRow &row : section.rows)
    {
        out.push_back(row.value);
    }
    return out;
}

std::vector<std::wstring> Split(const std::wstring &text, wchar_t on)
{
    std::vector<std::wstring> parts(1);
    for (const wchar_t c : text)
    {
        if (c == on)
        {
            parts.emplace_back();
        }
        else
        {
            parts.back() += c;
        }
    }
    return parts;
}

} // namespace

TEST_CASE("a number is written to the table's precision, as the record writes it", "[sheet]")
{
    CHECK(FormatStat(40.0f, 0) == L"40");
    CHECK(FormatStat(39.6f, 0) == L"40");
    CHECK(FormatStat(4.5f, 2) == L"4.50");
    CHECK(FormatStat(12.46f, 1) == L"12.5");
    CHECK(FormatStat(-10.0f, 0) == L"-10");
    // A negative that rounds away is 0, not "-0".
    CHECK(FormatStat(-0.2f, 0) == L"0");
}

TEST_CASE("a property's words lose the game's placeholder for its value", "[sheet]")
{
    CHECK(StatLabel(L"Melee critical chance: <percent/>") == L"Melee critical chance");
    CHECK(StatLabel(L"Strength") == L"Strength");
    CHECK(StatLabel(L"").empty());
}

TEST_CASE("the head has the name, the table's word for level, and the classes by ability", "[sheet]")
{
    const CreatureSheet sheet = BuildSheet(Lyna());
    CHECK(sheet.id == 0x1234u);
    CHECK(sheet.name == L"Lyna");
    CHECK(sheet.level == L"Level 28");
    REQUIRE(sheet.classes.size() == 2);
    CHECK(sheet.classes[0].name == L"Rogue");
    CHECK(sheet.classes[1].icon == L"classico_duelist");
}

TEST_CASE("the stats come first: health and mana or stamina, what is left of the whole, in one column", "[sheet]")
{
    const CreatureSheet sheet = BuildSheet(Lyna());
    REQUIRE_FALSE(sheet.sections.empty());
    const SheetSection &stats = sheet.sections.front();
    CHECK(stats.title == L"Stats");
    CHECK(Labels(stats) == std::vector<std::wstring>{L"Health", L"Stamina"});
    CHECK(Values(stats) == std::vector<std::wstring>{L"400 / 430", L"233 / 283"});
    CHECK(stats.left == 2);
    CHECK(sheet.sections[1].title == L"Attributes");
}

TEST_CASE("a creature with no class by ability has its current class's name, and no icon", "[sheet]")
{
    SheetFacts ogre = Lyna();
    ogre.classes.clear();
    ogre.currentClass = L"Tank Monster";
    const CreatureSheet sheet = BuildSheet(ogre);
    REQUIRE(sheet.classes.size() == 1);
    CHECK(sheet.classes[0].name == L"Tank Monster");
    CHECK(sheet.classes[0].icon.empty());
    ogre.currentClass.clear();
    CHECK(BuildSheet(ogre).classes.empty());
}

TEST_CASE("the attributes are the record's six, Strength, Dexterity and Magic over Constitution, Cunning and "
          "Willpower, coloured by what raises or lowers them",
          "[sheet]")
{
    const SheetSection attributes = Section(BuildSheet(Lyna()), L"Attributes");
    CHECK(Labels(attributes) ==
          std::vector<std::wstring>{L"Strength", L"Dexterity", L"Magic", L"Constitution", L"Cunning", L"Willpower"});
    CHECK(Values(attributes) == std::vector<std::wstring>{L"40", L"69", L"18", L"31", L"41", L"27"});
    CHECK(attributes.left == 3);
    CHECK(attributes.rows[0].tone == Tone::Raised);
    CHECK(attributes.rows[2].tone == Tone::Lowered);
    CHECK(attributes.rows[5].tone == Tone::Plain);
}

TEST_CASE("a modifier that rounds away at the table's precision is no colour", "[sheet]")
{
    SheetFacts facts = Lyna();
    Set(facts, 1, 40, 0.3f);
    Set(facts, 2, 69, -0.4f);
    const SheetSection attributes = Section(BuildSheet(facts), L"Attributes");
    CHECK(attributes.rows[0].tone == Tone::Plain);
    CHECK(attributes.rows[1].tone == Tone::Plain);
}

TEST_CASE("two weapons: each hand's damage, critical chance and armor penetration side by side, then attack", "[sheet]")
{
    const SheetSection offense = Section(BuildSheet(Lyna()), L"Offense");
    CHECK(Labels(offense) == std::vector<std::wstring>{L"Damage (main hand)", L"Critical chance", L"Armor penetration",
                                                       L"Attack", L"Damage (offhand)", L"Critical chance",
                                                       L"Armor penetration"});
    CHECK(Values(offense) == std::vector<std::wstring>{L"12.5", L"6.00%", L"9", L"85", L"8.3", L"5.00%", L"7"});
    // The main hand's and the attack on the left, the off hand's on the right.
    CHECK(offense.left == 4);
    CHECK(offense.rows[0].tone == Tone::Raised);
    CHECK(offense.rows[1].tone == Tone::Plain);
    CHECK(offense.rows[3].tone == Tone::Raised);
}

TEST_CASE("the defenses are a part of their own, in one column", "[sheet]")
{
    const SheetSection defense = Section(BuildSheet(Lyna()), L"Defense");
    CHECK(Labels(defense) == std::vector<std::wstring>{L"Armor", L"Defense"});
    CHECK(Values(defense) == std::vector<std::wstring>{L"12", L"45"});
    CHECK(defense.left == 2);
    CHECK(defense.rows[0].tone == Tone::Raised);
}

TEST_CASE("one weapon: one hand's of each, and nothing in the right column", "[sheet]")
{
    SheetFacts facts = Lyna();
    Set(facts, 49, 0);
    facts.offHand = {};
    const SheetSection offense = Section(BuildSheet(facts), L"Offense");
    CHECK(Labels(offense) ==
          std::vector<std::wstring>{L"Damage (main hand)", L"Critical chance", L"Armor penetration", L"Attack"});
    CHECK(Values(offense) == std::vector<std::wstring>{L"12.5", L"6.00%", L"9", L"85"});
    CHECK(offense.left == 4);
}

TEST_CASE("an enemy's damage, where the game's script has reckoned none, is left out, and its second weapon's figures "
          "stay",
          "[sheet]")
{
    SheetFacts cultist = Lyna();
    Set(cultist, 50, 0);
    Set(cultist, 49, 0);
    const SheetSection offense = Section(BuildSheet(cultist), L"Offense");
    CHECK(Labels(offense) == std::vector<std::wstring>{L"Critical chance", L"Armor penetration", L"Attack",
                                                       L"Critical chance", L"Armor penetration"});
    CHECK(Values(offense) == std::vector<std::wstring>{L"6.00%", L"9", L"85", L"5.00%", L"7"});
    CHECK(offense.left == 3);
}

TEST_CASE("the script's mark for a hand with no weapon to dual wield is no damage", "[sheet]")
{
    // RecalculateDisplayDamage writes -99 for an off hand not dual wielded
    // and -97 for an empty one (core_h.nss), where the table's floor leaves
    // it below 0.
    SheetFacts facts = Lyna();
    Set(facts, 49, -99);
    const SheetSection offense = Section(BuildSheet(facts), L"Offense");
    CHECK(Labels(offense) == std::vector<std::wstring>{L"Damage (main hand)", L"Critical chance", L"Armor penetration",
                                                       L"Attack", L"Critical chance", L"Armor penetration"});
}

TEST_CASE("a shield in the off hand is no second weapon", "[sheet]")
{
    SheetFacts guard = Lyna();
    Set(guard, 49, 0);
    guard.offHand = {.kind = WeaponKind::None, .crit = 0, .penetration = 0};
    const SheetSection offense = Section(BuildSheet(guard), L"Offense");
    CHECK(offense.rows.size() == offense.left);
}

TEST_CASE("a bow's critical chance is the ranged one, an empty hand's the melee one, both called critical chance",
          "[sheet]")
{
    SheetFacts archer = Lyna();
    Set(archer, 49, 0);
    archer.mainHand = {.kind = WeaponKind::Ranged, .crit = 2, .penetration = 0};
    const SheetSection shooting = Section(BuildSheet(archer), L"Offense");
    // In the item card's words, as the melee one is: the hand's weapon says which.
    CHECK(Labels(shooting)[1] == L"Critical chance");
    CHECK(Values(shooting)[1] == L"5.00%");

    SheetFacts bare = archer;
    bare.mainHand = {};
    const SheetSection fists = Section(BuildSheet(bare), L"Offense");
    CHECK(Labels(fists)[1] == L"Critical chance");
    CHECK(Values(fists)[1] == L"4.50%");
}

TEST_CASE("a mage's attack is its spellpower, and a staff has no critical chance", "[sheet]")
{
    SheetFacts mage = Lyna();
    mage.mage = true;
    Set(mage, 13, 52);
    Set(mage, 49, 0);
    mage.mainHand = {.kind = WeaponKind::Magic, .crit = 0, .penetration = 0};
    mage.offHand = {};
    const SheetSection offense = Section(BuildSheet(mage), L"Offense");
    CHECK(Labels(offense) == std::vector<std::wstring>{L"Damage (main hand)", L"Armor penetration", L"Spellpower"});
    CHECK(Values(offense)[2] == L"52");
    CHECK(offense.left == 3);
}

TEST_CASE("the resistances are the table's, checks in one column and elements in the other, by their UIMapping",
          "[sheet]")
{
    SheetFacts facts = Lyna();
    // A mod's sixth element: a row of the table like the game's own.
    facts.table.push_back({.id = 70,
                           .label = L"Blight resistance",
                           .precision = 0,
                           .uiMapping = 3,
                           .total = 25,
                           .modifier = 0,
                           .current = 0});
    const SheetSection resistances = Section(BuildSheet(facts), L"Resistances");
    CHECK(Labels(resistances) == std::vector<std::wstring>{L"Mental resistance check", L"Physical resistance check",
                                                           L"Fire resistance", L"Cold resistance",
                                                           L"Electrical resistance", L"Nature resistance",
                                                           L"Spirit resistance", L"Blight resistance"});
    CHECK(Values(resistances) ==
          std::vector<std::wstring>{L"28", L"55", L"60%", L"0%", L"0%", L"10%", L"-10%", L"25%"});
    // The checks in the left column, every element in the right.
    CHECK(resistances.left == 2);
    // A check is plain however high; an element green above nothing, red below.
    CHECK(resistances.rows[1].tone == Tone::Plain);
    CHECK(resistances.rows[2].tone == Tone::Raised);
    CHECK(resistances.rows[3].tone == Tone::Plain);
    CHECK(resistances.rows[6].tone == Tone::Lowered);
}

TEST_CASE("a property the table does not have is left out", "[sheet]")
{
    SheetFacts facts = Lyna();
    std::erase_if(facts.table, [](const PropertyRow &row) { return row.id == 53 || row.id == 15; });
    const CreatureSheet sheet = BuildSheet(facts);
    CHECK(sheet.level.empty());
    const auto labels = Labels(Section(sheet, L"Offense"));
    CHECK(std::ranges::find(labels, L"Armor penetration") == labels.end());
}

TEST_CASE("the sheet's string splits into the head, the classes and a record a section", "[sheet]")
{
    SheetFacts facts = Lyna();
    facts.name = L"Ly\x1Fna"; // a separator in a name is dropped
    const std::wstring text = EncodeSheet(BuildSheet(facts));
    const auto records = Split(text, kBarCard);
    REQUIRE(records.size() == 7);
    CHECK(Split(records[0], kBarField) ==
          std::vector<std::wstring>{std::to_wstring(kSheetVersion), std::to_wstring(0x1234), L"Lyna", L"Level 28"});
    CHECK(Split(records[1], kBarField) ==
          std::vector<std::wstring>{L"Rogue", L"classico_rogue", L"Duelist", L"classico_duelist"});
    CHECK(Split(records[2], kBarField) ==
          std::vector<std::wstring>{L"Stats", L"2", L"Health", L"400 / 430", L"0", L"Stamina", L"233 / 283", L"0"});
    const auto attributes = Split(records[3], kBarField);
    REQUIRE(attributes.size() == 2 + (3 * 6));
    CHECK(attributes[0] == L"Attributes");
    CHECK(attributes[1] == L"3");
    CHECK(attributes[2] == L"Strength");
    CHECK(attributes[3] == L"40");
    CHECK(attributes[4] == L"1");
    CHECK(attributes[10] == L"2"); // Magic, lowered
    CHECK(Split(records[4], kBarField)[0] == L"Offense");
    CHECK(Split(records[5], kBarField)[0] == L"Defense");
    CHECK(Split(records[6], kBarField)[0] == L"Resistances");
}
