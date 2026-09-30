#include "game/Targeting.h"

#include "core/TurnMachine.h"

#include "dao/ClientExoApp.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/Messages.h"

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
            // The quickbar's "no target" is 0 (seen 2026-09-30, 13:24, three
            // talents; the game then asked for the target as it should).
            // What arrives when a talent goes straight to the selected
            // target is not seen yet: the first few are logged.
            if (seen_ < 8)
            {
                ++seen_;
                spdlog::info("ability message: {} for {:#010x} on {:#010x}", message.ability, message.caster,
                             message.target);
            }
            if (!enabled_ || message.target == 0 || message.target == dao::kInvalidObject ||
                message.target == message.caster)
            {
                return false; // no target named, or the caster itself: as the game has it
            }
            bool fighting = false;
            {
                const std::lock_guard lock(mutex);
                fighting = machine.Active();
            }
            if (!fighting)
            {
                return false;
            }
            taken_ = Taken{message.ability, message.caster};
            if (logged_ < 3)
            {
                ++logged_;
                spdlog::info(
                    "ability {} chosen for {:#010x} with {:#010x} as its target: asking for the target instead",
                    message.ability, message.caster, message.target);
            }
            return true;
        });
    spdlog::info("talents chosen in a turn-based fight ask for their target");
}

void Targeting::Serve()
{
    if (!taken_)
    {
        return;
    }
    const Taken taken = *taken_;
    taken_.reset();
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    void *mode = modes != nullptr ? modes->CurrentModeObject() : nullptr;
    if (mode == nullptr)
    {
        return;
    }
    auto message = dao::messages::Make<dao::messages::PerformAbilityMessage>();
    message.ability = taken.ability;
    message.caster = taken.caster;
    message.target = dao::kInvalidObject;
    if (!dao::messages::Send(mode, message))
    {
        spdlog::warn("ability {} sent again with no target: not handled", taken.ability);
    }
}

} // namespace tb::game
