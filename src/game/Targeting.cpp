#include "game/Targeting.h"

#include "core/Rules.h"
#include "core/TurnMachine.h"

#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/Messages.h"
#include "dao/Script.h"

namespace tb::game
{

void Targeting::Install(const TurnMachine &machine, std::mutex &mutex)
{
    if (installed_)
    {
        return;
    }
    installed_ = true;
    dao::messages::InterceptMessage<dao::messages::PerformAbilityMessage>(
        [this, &machine, &mutex](void *, const dao::messages::PerformAbilityMessage &message) {
            bool theirs = false;
            {
                const std::lock_guard lock(mutex);
                const Combatant *current = machine.Active() ? machine.Order().Current() : nullptr;
                theirs = current != nullptr && current->id == message.caster;
            }
            // Out of the caster's turn the gate refuses what it would do,
            // as before.
            if (!theirs)
            {
                return false;
            }
            const auto table = dao::script::Table::AbilitiesTalents;
            const std::int32_t targetType = dao::gda::GetInt(table, message.ability, "TargetType").value_or(0);
            const std::int32_t useType = dao::gda::GetInt(table, message.ability, "UseType").value_or(0);
            if (!AsksForTarget(targetType, useType))
            {
                return false;
            }
            auto *app = dao::CClientExoApp::Get();
            auto *modes = app != nullptr ? app->GameModes() : nullptr;
            auto *explore = modes != nullptr ? modes->Explore() : nullptr;
            if (explore == nullptr)
            {
                return false; // not in explore or combat mode: as the game has it
            }
            explore->RequestTarget(message.ability, message.caster);
            if (logged_ < 3)
            {
                ++logged_;
                spdlog::info("ability {} (targets {:#x}) for {:#010x}: asking for its target", message.ability,
                             targetType, message.caster);
            }
            return true;
        });
    spdlog::info("talents chosen in a turn-based fight ask for their target");
}

} // namespace tb::game
