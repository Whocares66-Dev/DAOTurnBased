#include "core/Facing.h"

namespace tb
{

bool TurnsToFace(CommandType type)
{
    return type == CommandType::Attack || type == CommandType::UseAbility;
}

} // namespace tb
