#include "core/GivenBack.h"

#include <format>

namespace tb
{

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
    const std::wstring_view what = type == CommandType::Attack       ? L"attack"
                                   : type == CommandType::UseAbility ? L"ability"
                                                                     : L"action";
    return std::format(L"{}: {}'s {} is given back", words, name, what);
}

} // namespace tb
