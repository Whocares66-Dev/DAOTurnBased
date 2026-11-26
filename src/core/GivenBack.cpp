#include "core/GivenBack.h"

#include "core/I18n.h"

#include <format>

namespace tb
{

namespace
{

// "attack", "ability", or "action" for anything else that costs one.
std::wstring_view Named(CommandType type)
{
    return type == CommandType::Attack       ? N_(L"attack")
           : type == CommandType::UseAbility ? N_(L"ability")
                                             : N_(L"action");
}

} // namespace

std::wstring_view WhyGivenBack(std::string_view why)
{
    // By a part of the engine's name for it, each part in one name only: a
    // line that carries more than the name (a log's) is read the same.
    if (why.find("PATH_ACTION_REQUIRED") != std::string_view::npos)
    {
        return N_(L"A door is in the way");
    }
    if (why.find("MELEE_RING") != std::string_view::npos)
    {
        return N_(L"No room at the target");
    }
    if (why.find("INVALID_PATH") != std::string_view::npos)
    {
        return N_(L"No way through");
    }
    if (why.find("LINE_OF_SIGHT") != std::string_view::npos)
    {
        return N_(L"No line of sight");
    }
    return N_(L"Nothing happened");
}

std::wstring GivenBackLine(std::wstring_view words, std::wstring_view name, CommandType type)
{
    const std::wstring_view what = Named(type);
    return i18n::TrFormat(L"{}: {}'s {} is given back", i18n::Tr(words), name, i18n::Tr(what));
}

std::wstring_view WhyNoAttack(NoAttack why)
{
    switch (why)
    {
    case NoAttack::NoPlace:
        return N_(L"No room at the target");
    case NoAttack::NoPath:
        return N_(L"No way through");
    case NoAttack::NoLine:
        return N_(L"No line of sight");
    case NoAttack::None:
        break;
    }
    return N_(L"Nothing happened");
}

std::string_view NoAttackNote(NoAttack why)
{
    switch (why)
    {
    case NoAttack::NoPlace:
        return "the engine has no place at its target for it";
    case NoAttack::NoPath:
        return "the engine finds no path to its target";
    case NoAttack::NoLine:
        return "the engine finds no line of sight to its target on the way to it";
    case NoAttack::None:
        break;
    }
    return "no reason given";
}

std::wstring RefusedLine(std::wstring_view words, std::wstring_view name, CommandType type)
{
    return i18n::TrFormat(L"{}: {}'s {} is refused", i18n::Tr(words), name, i18n::Tr(Named(type)));
}

std::wstring NotMadeLine(std::wstring_view words, std::wstring_view name, CommandType type)
{
    return i18n::TrFormat(L"{}: {}'s {} is not made", i18n::Tr(words), name, i18n::Tr(Named(type)));
}

} // namespace tb
