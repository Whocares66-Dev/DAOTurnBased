#include "core/Bar.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace tb
{

namespace
{

void Field(std::wstring &out, std::wstring_view text)
{
    out += kBarField;
    // A name with a separator in it would split where it should not.
    for (const wchar_t c : text)
    {
        if (c >= 0x20)
        {
            out += c;
        }
    }
}

void Field(std::wstring &out, long long number)
{
    out += kBarField;
    out += std::to_wstring(number);
}

void Field(std::wstring &out, bool flag)
{
    out += kBarField;
    out += flag ? L'1' : L'0';
}

void Meters(std::wstring &out, float meters)
{
    out += kBarField;
    out += std::format(L"{:.1f}", std::max(0.0f, meters));
}

} // namespace

std::wstring EncodeBar(const TurnView &view)
{
    return EncodeBar(view, PortraitBook{});
}

std::wstring EncodeBar(const TurnView &view, const PortraitBook &portraits)
{
    std::wstring out = std::to_wstring(kBarVersion);
    Field(out, view.active);
    Field(out, static_cast<long long>(view.round));
    Field(out, static_cast<long long>(view.current));
    Field(out, view.playerTurn);
    Field(out, static_cast<long long>(view.actionsLeft));
    Meters(out, view.movementLeft);
    Meters(out, view.movementMax);
    for (const CardView &card : view.upcoming)
    {
        out += kBarCard;
        out += std::to_wstring(card.id);
        Field(out, static_cast<long long>(card.side));
        Field(out, static_cast<long long>(card.rank));
        Field(out, static_cast<long long>(std::lround(std::clamp(card.health, 0.0f, 1.0f) * 100.0f)));
        Field(out, card.down);
        Field(out, card.current);
        Field(out, card.yours);
        Field(out, static_cast<long long>(card.round));
        Field(out, std::wstring_view{card.name});
        Field(out, std::wstring_view{card.tag});
        Field(out, std::wstring_view{TextureName(portraits.Of(card.id))});
    }
    return out;
}

} // namespace tb
