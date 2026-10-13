// A creature's sheet: what the turn bar shows under the pointer on a card
// (issue 112; dev/UI.md, "A creature's sheet"). The user, 2026-10-13: its
// portrait, name, level, class and specialisations, its attributes, what it
// strikes with (damage, attack, critical chance, armor penetration), what it
// stands behind (armor, defense) and its resistances, "read out the game
// values ourselves ... dynamically so works w/ mods".
//
// So every number is the engine's own, a creature property read as the
// game's character record reads it (characterrecord.gfx, read 2026-10-13
// with FFDec: SharedLibrary.CoreStats, CharacterRecord, CreatureUtils), and
// every word and every precision is the game's properties table's (table 98:
// NameStrID through the talk table, Precision), so that a mod's table or
// talk table is what is shown. Which rows are resistances is the table's
// too, its UIMapping (1 the resistance checks, 3 the elemental
// resistances), as the record finds them: a mod's sixth element has its
// row. What the record composes beyond a property is composed the same way
// here: a critical chance is the creature's melee or ranged one by the main
// hand's weapon (none for a staff's magic) with the weapon's own
// ITEM_STAT_CRIT_CHANCE_MODIFIER added, armor penetration the creature's
// AP_BONUS with the weapon's ITEM_STAT_ARMOR_PENETRATION, both for each
// hand where an off hand does damage; a mage's attack is its spellpower; a
// value is green where effects and equipment raise it and red where they
// lower it (the record's ColorTextForModifier), an elemental resistance by
// its sign.
//
// The classes are the record's too (UpdateClassIcons): every row of the
// classes table whose StartingAbility1 the creature has, the class first and
// then its specialisations, each with its icon. A creature with none (a
// monster's class is no ability) has its current class's name alone.
//
// The game layer reads (src/game/Sheet.cpp); this composes and encodes, for
// the movie to lay out (ui/turnorder: drawSheet), as core/Bar.h's string is
// made: records apart by U+001E, fields by U+001F.
//
//   the head:     version, id, name, level ("Level 12": the table's word)
//   the classes:  name, icon, name, icon, ... (an icon empty for none)
//   each section: title, how many of its rows stand in the left column,
//                 then label, value, tone (0 plain, 1 raised, 2 lowered)
//                 for each row
#pragma once

#include "core/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tb
{

inline constexpr int kSheetVersion = 1;

// A row of the game's properties table (properties.gda): its id, its words
// (NameStrID's text, or its Stat where it has none, as the record falls back
// to), its Precision and its UIMapping; and the property's value on the
// creature, its total, what effects and equipment add to it
// (PROPERTY_VALUE_MODIFIER) and, for health and mana or stamina, what is left
// of it (PROPERTY_VALUE_CURRENT).
struct PropertyRow
{
    std::int32_t id = 0;
    std::wstring label;
    int precision = 0;
    int uiMapping = 0;
    float total = 0.0f;
    float modifier = 0.0f;
    float current = 0.0f;
};

// What a hand holds, by its item's type in the items table (Type: 2 a melee
// weapon, 5 a magic one, a staff, 6 a ranged one), and the item's own
// critical chance and armor penetration (GetItemStat).
enum class WeaponKind : std::uint8_t
{
    None, // an empty hand, which the record counts as melee, or no weapon in it (a shield)
    Melee,
    Ranged,
    Magic,
};

struct WeaponFacts
{
    WeaponKind kind = WeaponKind::None;
    float crit = 0.0f;
    float penetration = 0.0f;
};

// A class as the classes table has it: its name and its icon (a texture in
// the game's interface atlases, classico_warrior).
struct SheetClass
{
    std::wstring name;
    std::wstring icon;
};

// What the game layer read of one creature.
struct SheetFacts
{
    ObjectId id = kNoObject;
    std::wstring name;
    bool mage = false;               // its core class a mage's (the bar's card says)
    std::wstring energyWord;         // "Mana" or "Stamina", as the record calls its property 8 by class
    // A hand's critical chance and armor penetration in the item card's words
    // (the item stats table's NameStrRef, rows 5 and 6: "Critical chance:
    // <percent/>" and "Armor penetration"), the melee and the ranged chance
    // alike (the user, 2026-10-13, of "Melee critical chance": "just
    // \"critical chance\" should be enough").
    std::wstring critWord;
    std::wstring penetrationWord;
    std::vector<SheetClass> classes; // every row whose starting ability it has, in the table's order
    std::wstring currentClass;       // its current class's name
    std::vector<PropertyRow> table;  // the properties table, in its order, with the creature's values
    WeaponFacts mainHand;
    WeaponFacts offHand;
};

enum class Tone : std::uint8_t
{
    Plain,
    Raised,
    Lowered,
};

struct SheetRow
{
    std::wstring label;
    std::wstring value;
    Tone tone = Tone::Plain;
};

// A section's rows stand in two columns, the first `left` of them in the
// left one.
struct SheetSection
{
    std::wstring title;
    std::size_t left = 0;
    std::vector<SheetRow> rows;
};

struct CreatureSheet
{
    ObjectId id = kNoObject;
    std::wstring name;
    std::wstring level;
    std::vector<SheetClass> classes;
    std::vector<SheetSection> sections;
};

// A number as the game's movies write a property (ExternalCommands.FormatFloat
// with the table's Precision): so many decimals, rounded.
[[nodiscard]] std::wstring FormatStat(float value, int precision);
// A property's words with the game's placeholder for its value taken out:
// "Melee critical chance: <percent/>" is "Melee critical chance".
[[nodiscard]] std::wstring StatLabel(std::wstring_view text);

[[nodiscard]] CreatureSheet BuildSheet(const SheetFacts &facts);
[[nodiscard]] std::wstring EncodeSheet(const CreatureSheet &sheet);

} // namespace tb
