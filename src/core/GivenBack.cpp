#include "core/GivenBack.h"

#include <format>

namespace tb
{

namespace
{

// "attack", "ability", or "action" for anything else that costs one.
std::wstring_view Named(CommandType type)
{
    return type == CommandType::Attack ? L"attack" : type == CommandType::UseAbility ? L"ability" : L"action";
}

} // namespace

std::wstring_view WhyGivenBack(std::string_view why)
{
    // By a part of the engine's name for it, each part in one name only: a
    // line that carries more than the name (a log's) is read the same.
    if (why.find("PATH_ACTION_REQUIRED") != std::string_view::npos)
    {
        return L"A door is in the way";
    }
    if (why.find("MELEE_RING") != std::string_view::npos)
    {
        return L"No room at the target";
    }
    if (why.find("INVALID_PATH") != std::string_view::npos)
    {
        return L"No way through";
    }
    if (why.find("LINE_OF_SIGHT") != std::string_view::npos)
    {
        return L"No line of sight";
    }
    return L"Nothing happened";
}

std::wstring GivenBackLine(std::wstring_view words, std::wstring_view name, CommandType type)
{
    const std::wstring_view what = Named(type);
    return std::format(L"{}: {}'s {} is given back", words, name, what);
}

std::wstring_view WhyNoAttack(NoAttack why)
{
    switch (why)
    {
    case NoAttack::NoPlace:
        return L"No room at the target";
    case NoAttack::NoPath:
        return L"No way through";
    case NoAttack::NoLine:
        return L"No line of sight";
    case NoAttack::None:
        break;
    }
    return L"Nothing happened";
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
    return std::format(L"{}: {}'s {} is refused", words, name, Named(type));
}

std::wstring NotMadeLine(std::wstring_view words, std::wstring_view name, CommandType type)
{
    return std::format(L"{}: {}'s {} is not made", words, name, Named(type));
}

} // namespace tb
