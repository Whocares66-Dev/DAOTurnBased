#include "core/Sheet.h"

#include "core/Bar.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace tb
{

namespace
{

// The properties the sheet names, by their rows of the properties table
// (2da_constants_h.nss, PROPERTY_*; the record's CoreStats.PROP_*).
constexpr std::int32_t kStrength = 1;
constexpr std::int32_t kDexterity = 2;
constexpr std::int32_t kWillpower = 3;
constexpr std::int32_t kMagic = 4;
constexpr std::int32_t kCunning = 5;
constexpr std::int32_t kConstitution = 6;
constexpr std::int32_t kHealth = 7;
constexpr std::int32_t kManaStamina = 8;
constexpr std::int32_t kAttack = 9;
constexpr std::int32_t kDefense = 10;
constexpr std::int32_t kArmor = 11;
constexpr std::int32_t kSpellpower = 13;
constexpr std::int32_t kLevel = 15;
constexpr std::int32_t kMeleeCrit = 21;
constexpr std::int32_t kRangedCrit = 23;
constexpr std::int32_t kOffHandDamage = 49;
constexpr std::int32_t kMainHandDamage = 50;
constexpr std::int32_t kPenetration = 53;
// The record's UIMapping for each kind of resistance (CharacterRecord's
// RESISTANCE_UIMAPPING and ELEMENTAL_RESISTANCE_UIMAPPING), which it lists
// in that order.
constexpr int kResistanceChecks = 1;
constexpr int kElementalResistances = 3;

constexpr std::wstring_view kPercent = L"<percent/>";

const PropertyRow *RowOf(const SheetFacts &facts, std::int32_t id)
{
    const auto found = std::ranges::find(facts.table, id, &PropertyRow::id);
    return found != facts.table.end() ? &*found : nullptr;
}

float TotalOf(const SheetFacts &facts, std::int32_t id)
{
    const PropertyRow *row = RowOf(facts, id);
    return row != nullptr ? row->total : 0.0f;
}

// The sign of a value as it is written: the record compares the modifier
// after formatting it, so a hundredth that rounds away is no colour.
Tone ToneOf(float value, int precision)
{
    const double scaled = std::round(static_cast<double>(value) * std::pow(10.0, std::max(0, precision)));
    return scaled > 0.0 ? Tone::Raised : scaled < 0.0 ? Tone::Lowered : Tone::Plain;
}

// A property as the record writes most: its total, coloured by its modifier.
void AddStat(std::vector<SheetRow> &rows, const SheetFacts &facts, std::int32_t id)
{
    if (const PropertyRow *row = RowOf(facts, id))
    {
        rows.push_back(SheetRow{.label = StatLabel(row->label),
                                .value = FormatStat(row->total, row->precision),
                                .tone = ToneOf(row->modifier, row->precision)});
    }
}

// A property with what a hand's weapon adds to it, as the record's
// GetCritText and GetArmorPenetrationText reckon it for each hand, in the item
// card's words for it.
void AddForHand(std::vector<SheetRow> &rows, const SheetFacts &facts, std::int32_t id, float added,
                std::wstring_view words, std::wstring_view unit)
{
    if (const PropertyRow *row = RowOf(facts, id))
    {
        rows.push_back(SheetRow{.label = StatLabel(words),
                                .value = FormatStat(row->total + added, row->precision) + std::wstring{unit},
                                .tone = Tone::Plain});
    }
}

SheetSection Attributes(const SheetFacts &facts)
{
    // The record's six, three to a column, in the user's order (2026-10-13:
    // "Strength, Constitution / Dexterity, Cunning / Magic, Willpower", row by
    // row), where the record lists them by their numbers.
    SheetSection section{.title = L"Attributes", .left = 3, .rows = {}};
    for (const std::int32_t id : {kStrength, kDexterity, kMagic, kConstitution, kCunning, kWillpower})
    {
        AddStat(section.rows, facts, id);
    }
    return section;
}

// What it strikes with: each hand's damage, critical chance and armor
// penetration, the main hand's in the left column and the off hand's beside
// them, then its attack (the user, 2026-10-13: "Damage (main) | Damage (off),
// Critical chance (main) | Critical chance (off), Armor penetration (main) |
// Armor penetration (off), Attack"). Until then one part, Combat, had the
// two hands' figures as one value ("6.00% / 5.00%") and the defenses in its
// right column.
SheetSection Offense(const SheetFacts &facts)
{
    // The off hand's where it holds a weapon. The record goes by its damage
    // (UpdateHandDamageTooltip), which is there only once the game's script
    // has reckoned it (game/Sheet.cpp), so going by it would hide a second
    // weapon's figures where the script has not.
    const bool dual = facts.offHand.kind != WeaponKind::None;
    // By the main hand's weapon, for both hands as the record has it: none
    // for a staff, whose magic the record gives no critical chance; the
    // ranged one for a bow or a crossbow; the melee one for the rest and for
    // an empty hand.
    const bool crits = facts.mainHand.kind != WeaponKind::Magic;
    const std::int32_t crit = facts.mainHand.kind == WeaponKind::Ranged ? kRangedCrit : kMeleeCrit;
    std::vector<SheetRow> main;
    std::vector<SheetRow> off;
    // Damage only where the game's script has a figure for it (the user,
    // 2026-10-13: "if there's no damage number to pull for enemies, we can
    // skip that for them"): the script reckons it for anyone as the sheet is
    // first asked for (issue 113), and a module whose script does not pass
    // the character record's event on leaves an enemy's at 0. The script
    // marks a hand with no weapon below 0 (-99, -97), which the table's floor
    // may make 0: a figure not above 0 is none.
    if (TotalOf(facts, kMainHandDamage) > 0.0f)
    {
        AddStat(main, facts, kMainHandDamage);
    }
    if (crits)
    {
        AddForHand(main, facts, crit, facts.mainHand.crit, facts.critWord, L"%");
    }
    AddForHand(main, facts, kPenetration, facts.mainHand.penetration, facts.penetrationWord, L"");
    if (dual)
    {
        if (TotalOf(facts, kOffHandDamage) > 0.0f)
        {
            AddStat(off, facts, kOffHandDamage);
        }
        if (crits)
        {
            AddForHand(off, facts, crit, facts.offHand.crit, facts.critWord, L"%");
        }
        AddForHand(off, facts, kPenetration, facts.offHand.penetration, facts.penetrationWord, L"");
    }
    // A mage's is spellpower, where the record shows it in attack's place.
    AddStat(main, facts, facts.mage ? kSpellpower : kAttack);
    SheetSection section{.title = L"Offense", .left = main.size(), .rows = std::move(main)};
    section.rows.insert(section.rows.end(), off.begin(), off.end());
    return section;
}

// What it stands behind, in one column.
SheetSection Defense(const SheetFacts &facts)
{
    SheetSection section{.title = L"Defense", .left = 0, .rows = {}};
    AddStat(section.rows, facts, kArmor);
    AddStat(section.rows, facts, kDefense);
    section.left = section.rows.size();
    return section;
}

SheetSection Resistances(const SheetFacts &facts)
{
    SheetSection section{.title = L"Resistances", .left = 0, .rows = {}};
    for (const int mapping : {kResistanceChecks, kElementalResistances})
    {
        for (const PropertyRow &row : facts.table)
        {
            if (row.uiMapping != mapping)
            {
                continue;
            }
            // A check is a plain number; an element's resistance a share,
            // green above nothing and red below (COLOR_POLICY_NONE and
            // COLOR_POLICY_ELEMENTAL_RESISTANCE).
            const bool element = mapping == kElementalResistances;
            section.rows.push_back(SheetRow{.label = StatLabel(row.label),
                                            .value = FormatStat(row.total, row.precision) + (element ? L"%" : L""),
                                            .tone = element ? ToneOf(row.total, row.precision) : Tone::Plain});
        }
        // The checks in the left column, the elements in the right, as the
        // record has them apart (the user, 2026-10-13: "mental / physical
        // resistance should be one column, elemental resistances should be
        // their own column").
        if (mapping == kResistanceChecks)
        {
            section.left = section.rows.size();
        }
    }
    return section;
}

// What is left of a depletable property and its whole, as the record writes
// them ("430 / 430": SharedLibrary.CoreStats, HealthText_mc and
// EnergyText_mc).
void AddDepletable(std::vector<SheetRow> &rows, const SheetFacts &facts, std::int32_t id, std::wstring_view words)
{
    if (const PropertyRow *row = RowOf(facts, id))
    {
        rows.push_back(SheetRow{.label = std::wstring{words},
                                .value = FormatStat(row->current, row->precision) + L" / " +
                                         FormatStat(row->total, row->precision),
                                .tone = Tone::Plain});
    }
}

// Health and mana or stamina, their own part before the attributes (the
// user, 2026-10-13: "put health / stamina in their own section, Stats, before
// attributes"), in one column ("put health / stamina in one column").
SheetSection Stats(const SheetFacts &facts)
{
    SheetSection section{.title = L"Stats", .left = 0, .rows = {}};
    if (const PropertyRow *row = RowOf(facts, kHealth))
    {
        AddDepletable(section.rows, facts, kHealth, StatLabel(row->label));
    }
    // Property 8 is the table's "Mana Stamina"; the record calls it by the
    // creature's class (CreatureUtils.LabelForProperty).
    AddDepletable(section.rows, facts, kManaStamina, facts.energyWord);
    section.left = section.rows.size();
    return section;
}

// A record's fields, apart by U+001F; as the bar's own, nothing in one may
// split it.
void AddRecord(std::wstring &out, const std::vector<std::wstring> &fields)
{
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (i > 0)
        {
            out += kBarField;
        }
        for (const wchar_t c : fields[i])
        {
            if (c >= 0x20)
            {
                out += c;
            }
        }
    }
}

} // namespace

std::wstring FormatStat(float value, int precision)
{
    std::wstring out = std::format(L"{:.{}f}", value, std::clamp(precision, 0, 6));
    // "-0" is how a rounded-away negative prints; the record shows 0.
    if (out.starts_with(L'-') && out.find_first_not_of(L"-0.") == std::wstring::npos)
    {
        out.erase(0, 1);
    }
    return out;
}

std::wstring StatLabel(std::wstring_view text)
{
    std::wstring out{text.substr(0, text.find(kPercent))};
    while (!out.empty() && (out.back() == L' ' || out.back() == L':'))
    {
        out.pop_back();
    }
    return out;
}

CreatureSheet BuildSheet(const SheetFacts &facts)
{
    CreatureSheet sheet{.id = facts.id, .name = facts.name, .level = {}, .classes = facts.classes, .sections = {}};
    if (const PropertyRow *row = RowOf(facts, kLevel))
    {
        sheet.level = StatLabel(row->label) + L" " + FormatStat(row->total, row->precision);
    }
    if (sheet.classes.empty() && !facts.currentClass.empty())
    {
        sheet.classes.push_back(SheetClass{.name = facts.currentClass, .icon = {}});
    }
    sheet.sections = {Stats(facts), Attributes(facts), Offense(facts), Defense(facts), Resistances(facts)};
    return sheet;
}

std::wstring EncodeSheet(const CreatureSheet &sheet)
{
    std::wstring out;
    AddRecord(out, {std::to_wstring(kSheetVersion), std::to_wstring(sheet.id), sheet.name, sheet.level});
    std::vector<std::wstring> classes;
    for (const SheetClass &each : sheet.classes)
    {
        classes.push_back(each.name);
        classes.push_back(each.icon);
    }
    out += kBarCard;
    AddRecord(out, classes);
    for (const SheetSection &section : sheet.sections)
    {
        std::vector<std::wstring> fields{section.title, std::to_wstring(section.left)};
        for (const SheetRow &row : section.rows)
        {
            fields.push_back(row.label);
            fields.push_back(row.value);
            fields.push_back(std::to_wstring(static_cast<int>(row.tone)));
        }
        out += kBarCard;
        AddRecord(out, fields);
    }
    return out;
}

} // namespace tb
