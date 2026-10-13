#include "game/Sheet.h"

#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/Tlk.h"
#include "dao/core/GdaHash.h"
#include "dao/generated/ScriptApi.h"
#include "dao/generated/ScriptConstants.h"

#include <cstdint>
#include <vector>

namespace tb::game
{

namespace
{

namespace api = dao::script::api;
namespace k = dao::script::constants;

// The game's properties table, which the scripts have no constant for: its
// number is the character record's (_2DA.TABLE_PROPERTIES, 98), as DAOLib's
// dev/gda_tables.csv has it.
constexpr std::int32_t kPropertiesTable = 98;
// And the item stats' table (itemstatdefines.gda), which neither has: 109 in
// DAOLib's dev/gda_tables.csv.
constexpr std::int32_t kItemStats = 109;
constexpr std::int32_t kClassesTable = static_cast<std::int32_t>(dao::script::Table::RulesClasses);
constexpr std::int32_t kCurrentClass = 27; // PROPERTY_SIMPLE_CURRENT_CLASS
// The items table's Type of a weapon (the record's DAConstants).
constexpr std::int32_t kMeleeWeapon = 2;  // ITEM_TYPE_WEAPON_MELEE
constexpr std::int32_t kMagicWeapon = 5;  // ITEM_TYPE_WEAPON_MAGIC
constexpr std::int32_t kRangedWeapon = 6; // ITEM_TYPE_WEAPON_RANGED
// GetItemStat's (2da_constants_h.nss).
constexpr std::int32_t kItemCrit = 5;        // ITEM_STAT_CRIT_CHANCE_MODIFIER
constexpr std::int32_t kItemPenetration = 6; // ITEM_STAT_ARMOR_PENETRATION

std::int32_t Int(std::int32_t table, std::int32_t row, const char *column)
{
    return dao::gda::GetInt(table, row, dao::core::GdaColumnHash(column)).value_or(0);
}

std::wstring String(std::int32_t table, std::int32_t row, const char *column)
{
    return dao::gda::GetString(table, row, dao::core::GdaColumnHash(column)).value_or(std::wstring{});
}

// Words by a string reference, else the row's own label, as the record falls
// back to (CreatureUtils.LabelForProperty, ClassRanksIcon.Update).
std::wstring Words(std::int32_t strref, const std::wstring &fallback)
{
    std::wstring text = strref > 0 ? dao::tlk::String(static_cast<std::uint32_t>(strref)) : std::wstring{};
    return text.empty() ? fallback : text;
}

// Every row of a table by its own number: the table is every file whose name
// begins with its prefix (an M2DA), and a mod's or an add-on's rows have
// whatever numbers their makers gave them (Bar.cpp's portraits table has the
// same reading).
template <typename Read> void EachRow(std::int32_t table, Read read)
{
    const std::int32_t count = dao::gda::Rows(table).value_or(0);
    for (std::int32_t place = 0; place < count; ++place)
    {
        read(api::GetM2DARowIdFromRowIndex(table, place));
    }
}

struct ClassRow
{
    std::int32_t id = 0;
    std::int32_t ability = 0; // StartingAbility1: who has it is of the class
    SheetClass shown;
};

// Read once: the tables do not change in a game. Not before the game's
// tables are there, which a fight's card means they are.
struct Tables
{
    std::vector<PropertyRow> properties;
    std::vector<ClassRow> classes;
};

const Tables &GameTables()
{
    static Tables tables;
    if (!tables.properties.empty())
    {
        return tables;
    }
    EachRow(kPropertiesTable, [](std::int32_t row) {
        tables.properties.push_back(
            PropertyRow{.id = row,
                        .label = Words(Int(kPropertiesTable, row, "NameStrID"), String(kPropertiesTable, row, "Stat")),
                        .precision = Int(kPropertiesTable, row, "Precision"),
                        .uiMapping = Int(kPropertiesTable, row, "UIMapping")});
    });
    EachRow(kClassesTable, [](std::int32_t row) {
        tables.classes.push_back(ClassRow{.id = row,
                                          .ability = Int(kClassesTable, row, "StartingAbility1"),
                                          .shown = SheetClass{.name = Words(Int(kClassesTable, row, "NameStrRef"),
                                                                            String(kClassesTable, row, "Label")),
                                                              .icon = String(kClassesTable, row, "Icon")}});
    });
    std::size_t resistances = 0;
    for (const PropertyRow &row : tables.properties)
    {
        resistances += row.uiMapping == 1 || row.uiMapping == 3 ? 1 : 0;
    }
    spdlog::info("the creatures' sheets read the game's tables: {} properties, {} of them resistances, and {} "
                 "classes",
                 tables.properties.size(), resistances, tables.classes.size());
    return tables;
}

WeaponFacts Hand(ObjectId creature, std::int32_t slot)
{
    const ObjectId item = api::GetItemInEquipSlot(slot, creature);
    if (item == dao::kInvalidObject || api::IsObjectValid(item) == 0)
    {
        return {};
    }
    const std::int32_t type =
        dao::gda::GetInt(dao::script::Table::Items, api::GetBaseItemType(item), "Type").value_or(0);
    return WeaponFacts{.kind = type == kMeleeWeapon    ? WeaponKind::Melee
                               : type == kMagicWeapon  ? WeaponKind::Magic
                               : type == kRangedWeapon ? WeaponKind::Ranged
                                                       : WeaponKind::None,
                       .crit = api::GetItemStat(item, kItemCrit),
                       .penetration = api::GetItemStat(item, kItemPenetration)};
}

} // namespace

SheetFacts ReadSheet(ObjectId id, const std::wstring &name, bool mage)
{
    if (id == kNoObject || api::IsObjectValid(id) == 0)
    {
        return {};
    }
    const Tables &tables = GameTables();
    SheetFacts facts;
    facts.id = id;
    facts.name = name;
    facts.mage = mage;
    // The record's words for property 8 by class (its Localization's
    // LOC_MANA and LOC_STAMINA, talk table 325563 and 325562).
    constexpr std::uint32_t kManaWord = 325563;
    constexpr std::uint32_t kStaminaWord = 325562;
    facts.energyWord = dao::tlk::String(mage ? kManaWord : kStaminaWord);
    // The item card's words for a weapon's critical chance and armor
    // penetration: the item stats table's (ITEM_STAT_CRIT_CHANCE_MODIFIER and
    // ITEM_STAT_ARMOR_PENETRATION's rows), read as the properties' are.
    facts.critWord = Words(Int(kItemStats, kItemCrit, "NameStrRef"), String(kItemStats, kItemCrit, "Label"));
    facts.penetrationWord =
        Words(Int(kItemStats, kItemPenetration, "NameStrRef"), String(kItemStats, kItemPenetration, "Label"));
    facts.table = tables.properties;
    std::int32_t current = 0;
    for (PropertyRow &row : facts.table)
    {
        row.total = api::GetCreatureProperty(id, row.id, k::PROPERTY_VALUE_TOTAL);
        row.modifier = api::GetCreatureProperty(id, row.id, k::PROPERTY_VALUE_MODIFIER);
        row.current = api::GetCreatureProperty(id, row.id, k::PROPERTY_VALUE_CURRENT);
        current = row.id == kCurrentClass ? static_cast<std::int32_t>(row.total) : current;
    }
    for (const ClassRow &row : tables.classes)
    {
        if (row.ability != 0 && api::HasAbility(id, row.ability) != 0)
        {
            facts.classes.push_back(row.shown);
        }
        if (row.id == current)
        {
            facts.currentClass = row.shown.name;
        }
    }
    facts.mainHand = Hand(id, k::INVENTORY_SLOT_MAIN);
    facts.offHand = Hand(id, k::INVENTORY_SLOT_OFFHAND);
    return facts;
}

} // namespace tb::game
