#include "game/Bridge.h"

#include "game/Camera.h"
#include "game/Gate.h"

#include "core/Rules.h"
#include "core/Text.h"

#include "dao/Abilities.h"
#include "dao/Attack.h"
#include "dao/ClientExoApp.h"
#include "dao/Clock.h"
#include "dao/Conjure.h"
#include "dao/Effects.h"
#include "dao/GameModes.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/ScriptHooks.h"
#include "dao/SubActions.h"
#include "dao/Timers.h"
#include "dao/World.h"
#include "dao/core/CommandWord.h"
#include "dao/generated/ScriptApi.h"
#include "dao/generated/ScriptConstants.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <unordered_set>
#include <utility>

namespace tb::game
{

namespace api = dao::script::api;
namespace k = dao::script::constants;

// The core's numbers are the engine's.
static_assert(static_cast<int>(Mode::Explore) == static_cast<int>(dao::GameMode::Explore));
static_assert(static_cast<int>(Mode::Combat) == static_cast<int>(dao::GameMode::Combat));
static_assert(static_cast<int>(Mode::Dead) == static_cast<int>(dao::GameMode::Dead));
static_assert(static_cast<int>(Mode::Gui) == static_cast<int>(dao::GameMode::Gui));
static_assert(static_cast<int>(Mode::Dialog) == static_cast<int>(dao::GameMode::Dialog));
static_assert(static_cast<int>(Mode::Cutscene) == static_cast<int>(dao::GameMode::Cutscene));
static_assert(static_cast<int>(Mode::FlyCam) == static_cast<int>(dao::GameMode::FlyCam));
static_assert(static_cast<int>(Mode::Fixed) == static_cast<int>(dao::GameMode::Fixed));
static_assert(static_cast<int>(Mode::Pregame) == static_cast<int>(dao::GameMode::Pregame));
static_assert(static_cast<int>(Mode::Loading) == static_cast<int>(dao::GameMode::Loading));
static_assert(static_cast<int>(Mode::Movie) == static_cast<int>(dao::GameMode::Movie));
static_assert(static_cast<int>(Mode::CharGen) == static_cast<int>(dao::GameMode::CharGen));
static_assert(static_cast<int>(Mode::PartyPicker) == static_cast<int>(dao::GameMode::PartyPicker));
// Every command type, as DAOLib names it from the game's commands table.
static_assert(static_cast<int>(CommandType::Invalid) == static_cast<int>(dao::core::CommandType::Invalid));
static_assert(static_cast<int>(CommandType::Attack) == static_cast<int>(dao::core::CommandType::Attack));
static_assert(static_cast<int>(CommandType::Appear) == static_cast<int>(dao::core::CommandType::Appear));
static_assert(static_cast<int>(CommandType::Disappear) == static_cast<int>(dao::core::CommandType::Disappear));
static_assert(static_cast<int>(CommandType::ForceFollowObject) ==
              static_cast<int>(dao::core::CommandType::ForceFollowObject));
static_assert(static_cast<int>(CommandType::RandomWalk) == static_cast<int>(dao::core::CommandType::RandomWalk));
static_assert(static_cast<int>(CommandType::DoFunction) == static_cast<int>(dao::core::CommandType::DoFunction));
static_assert(static_cast<int>(CommandType::GiveItem) == static_cast<int>(dao::core::CommandType::GiveItem));
static_assert(static_cast<int>(CommandType::TakeItem) == static_cast<int>(dao::core::CommandType::TakeItem));
static_assert(static_cast<int>(CommandType::JumpToObject) == static_cast<int>(dao::core::CommandType::JumpToObject));
static_assert(static_cast<int>(CommandType::JumpToLocation) ==
              static_cast<int>(dao::core::CommandType::JumpToLocation));
static_assert(static_cast<int>(CommandType::SpeakString) == static_cast<int>(dao::core::CommandType::SpeakString));
static_assert(static_cast<int>(CommandType::SpeakStrref) == static_cast<int>(dao::core::CommandType::SpeakStrref));
static_assert(static_cast<int>(CommandType::Wait) == static_cast<int>(dao::core::CommandType::Wait));
static_assert(static_cast<int>(CommandType::PlayAnimation) == static_cast<int>(dao::core::CommandType::PlayAnimation));
static_assert(static_cast<int>(CommandType::PlaySound) == static_cast<int>(dao::core::CommandType::PlaySound));
static_assert(static_cast<int>(CommandType::MoveAwayFromObject) ==
              static_cast<int>(dao::core::CommandType::MoveAwayFromObject));
static_assert(static_cast<int>(CommandType::MoveAwayFromPoint) ==
              static_cast<int>(dao::core::CommandType::MoveAwayFromPoint));
static_assert(static_cast<int>(CommandType::StartConversation) ==
              static_cast<int>(dao::core::CommandType::StartConversation));
static_assert(static_cast<int>(CommandType::Examine) == static_cast<int>(dao::core::CommandType::Examine));
static_assert(static_cast<int>(CommandType::MoveToLocation) ==
              static_cast<int>(dao::core::CommandType::MoveToLocation));
static_assert(static_cast<int>(CommandType::MoveToObject) == static_cast<int>(dao::core::CommandType::MoveToObject));
static_assert(static_cast<int>(CommandType::MoveToMultipleLocations) ==
              static_cast<int>(dao::core::CommandType::MoveToMultipleLocations));
static_assert(static_cast<int>(CommandType::Lock) == static_cast<int>(dao::core::CommandType::Lock));
static_assert(static_cast<int>(CommandType::Unlock) == static_cast<int>(dao::core::CommandType::Unlock));
static_assert(static_cast<int>(CommandType::UseAbility) == static_cast<int>(dao::core::CommandType::UseAbility));
static_assert(static_cast<int>(CommandType::UseItem) == static_cast<int>(dao::core::CommandType::UseItem));
static_assert(static_cast<int>(CommandType::DropItem) == static_cast<int>(dao::core::CommandType::DropItem));
static_assert(static_cast<int>(CommandType::PickupItem) == static_cast<int>(dao::core::CommandType::PickupItem));
static_assert(static_cast<int>(CommandType::EquipItem) == static_cast<int>(dao::core::CommandType::EquipItem));
static_assert(static_cast<int>(CommandType::UnequipItem) == static_cast<int>(dao::core::CommandType::UnequipItem));
static_assert(static_cast<int>(CommandType::UseObject) == static_cast<int>(dao::core::CommandType::UseObject));
static_assert(static_cast<int>(CommandType::ExecuteEffect) == static_cast<int>(dao::core::CommandType::ExecuteEffect));
static_assert(static_cast<int>(CommandType::Drive) == static_cast<int>(dao::core::CommandType::Drive));
static_assert(static_cast<int>(CommandType::SellItem) == static_cast<int>(dao::core::CommandType::SellItem));
static_assert(static_cast<int>(CommandType::BuyItem) == static_cast<int>(dao::core::CommandType::BuyItem));
static_assert(static_cast<int>(CommandType::DoEvent) == static_cast<int>(dao::core::CommandType::DoEvent));
static_assert(static_cast<int>(CommandType::Interact) == static_cast<int>(dao::core::CommandType::Interact));
static_assert(static_cast<int>(CommandType::Deathblow) == static_cast<int>(dao::core::CommandType::Deathblow));
static_assert(static_cast<int>(CommandType::SheatheWeapons) ==
              static_cast<int>(dao::core::CommandType::SheatheWeapons));
static_assert(static_cast<int>(CommandType::SwitchWeaponSets) ==
              static_cast<int>(dao::core::CommandType::SwitchWeaponSets));
static_assert(static_cast<int>(CommandType::Turn) == static_cast<int>(dao::core::CommandType::Turn));
static_assert(static_cast<int>(CommandType::Fly) == static_cast<int>(dao::core::CommandType::Fly));
static_assert(kNoObject == dao::kInvalidObject);
// The two kinds of effect by which a mode keeps its user at a command
// (core/Rules.h, KeepsItsCreatureAtACommand; issue 51).
static_assert(kEffectSinging == static_cast<std::int32_t>(dao::script::EffectType::Singing));
static_assert(kEffectFeignDeath == static_cast<std::int32_t>(dao::script::EffectType::FeignDeath));

namespace
{

// The scripts' MAX_CREATURES_IN_COMBAT (rules_h.nss): how many hostiles the
// game's own "who is fighting" asks for.
constexpr std::int32_t kMaxCreaturesInCombat = 24;
// EVENT_TYPE_DEBUG_KICKSTART_AI (events_h.nss): rules_core runs
// AI_DetermineCombatRound on it.
constexpr std::int32_t kKickstartAi = 9998;
// A charged cast's conjuring when it is given at last: a moment for the
// gesture, the turns it charged having been its casting time.
constexpr float kChargedConjureSeconds = 1.0f;
// An attack command's approach, by the type of the sub-action it queues
// first (DAOLib's reading of the exe, 2026-10-04, dao/Attack.h and dao/
// Conjure.h): 4 for a blow, a walk to the attacker's place on its target's
// melee ring, and 15 for a shot (a bow, a crossbow, a staff), a walk to
// within range and sight. Each ends at once when its creature is in reach
// already; the attack itself comes after (50, and the swing, 1).
constexpr std::uint32_t kMeleeApproach = 4;
constexpr std::uint32_t kRangedApproach = 15;
// And at its turn to face its target, which an attack's command and an
// ability's come to after the approach (sub-action 39, through
// CSSubActionHandler::TurnToward: core/Facing.h has DAOLib's reading).
constexpr std::uint32_t kTurnToFace = 39;

TurnToFace AsCore(const dao::attack::FacingTurn &turn)
{
    return TurnToFace{turn.degrees, turn.done, turn.stalls, turn.waits};
}

// One of the fight's creatures, if its current command is an attack or an
// ability aimed at another creature and is at its turn to face it: the turn
// as the engine has it now (DAOLib's dao::attack::TurnTo, its issue 32: the
// point the engine turns to, the angle left by its own VectorToAngle and
// Angle::Wrap, its own test's "done", "stalls", the dead case, and "waits",
// a turn the engine will begin by itself), for the machine to decide on
// (core: TurnMachine::WatchTurnsToFace; issue 40). Asked
// here, at the turn itself, where the engine asks it; the rule asked as the
// command was given spun a creature 159 degrees for a command that began
// with a walk (core/Facing.h). Every frame for everyone of the fight, so two
// reads of the engine's own memory first and nothing else for a creature
// that is not there: the sub-action its command is at, then the command.
// Nothing for a command aimed at a place or at its own creature, or where
// DAOLib has no answer (either of the two no creature).
std::optional<TurnLook> TurnLookOf(ObjectId id)
{
    if (dao::commands::RunningSubAction(id) != kTurnToFace)
    {
        return std::nullopt;
    }
    auto *app = dao::CClientExoApp::Get();
    const dao::CCObject *object = app != nullptr ? app->GetObjectById(id) : nullptr;
    const dao::Command *command = object != nullptr ? object->CurrentCommand() : nullptr;
    if (command == nullptr)
    {
        return std::nullopt;
    }
    const auto type = static_cast<CommandType>(static_cast<int>(command->Type()));
    const std::optional<ObjectId> target = command->Target();
    if (!TurnsToFace(type) || !target || *target == dao::kInvalidObject || *target == id)
    {
        return std::nullopt;
    }
    const std::optional<dao::attack::FacingTurn> turn = dao::attack::TurnTo(id, *target);
    if (!turn)
    {
        return std::nullopt;
    }
    return TurnLook{id, *target, type, command->Sequence(), AsCore(*turn)};
}

// A creature faced exactly at whom its command is aimed, by the engine
// (DAOLib's dao::attack::FaceExactly: the creature's facing by the engine's
// own writer, the facing its movement controller wants by the engine's
// SetDesiredFacing, and the controller's own, all three, on an idle
// controller only), and the engine asked again: what the machine says in the
// log is what it answers (core: TurnMachine::Faced). The mod set a creature's
// facing by itself first, with the scripts' SetOrientation, which writes the
// creature's alone and leaves the controller's behind (read in the
// executable 2026-10-05: 0x00655340 to 0x00694CE0; the scripts' SetFacing,
// SetFacingPosition and SetFacingObject only ask the controller, through
// 0x00655170 to 0x00A638B0, and begin nothing in the dead case); dev/DESIGN.md
// has what came of that in play.
FacedLook FaceTarget(ObjectId creature, ObjectId target, CommandType type)
{
    FacedLook faced;
    faced.who = creature;
    faced.target = target;
    faced.type = type;
    const std::optional<dao::attack::FacingTurn> turn = dao::attack::TurnTo(creature, target);
    if (!turn)
    {
        return faced; // not taken, and no answer after either
    }
    faced.stood = std::abs(turn->degrees);
    faced.taken = dao::attack::FaceExactly(creature, turn->toward);
    if (const std::optional<dao::attack::FacingTurn> now = dao::attack::TurnTo(creature, target))
    {
        faced.now = AsCore(*now);
    }
    // Where the engine's aim is not where the target stands, for a play
    // test's log: a blow at a big creature, whose aim is a point of its
    // melee ring, the case the mod's own measure got wrong.
    const dao::Vector at = api::GetPosition(target);
    if (const float apart = std::hypot(turn->point.x - at.x, turn->point.y - at.y); apart > 0.05f)
    {
        spdlog::debug("{:#x} is turned to a point {:.2f} m from where {:#x} stands: the engine's own aim for it, "
                      "not its target's place (issue 40)",
                      creature, apart, target);
    }
    return faced;
}

// The game's mode as the core names it; none yet (no game modes) is
// Unknown, as before the world (DAOLib 3c8cccf made it an optional).
Mode ModeOf(const dao::CClientExoApp &app)
{
    const auto mode = app.Mode();
    return mode ? static_cast<Mode>(*mode) : Mode::Unknown;
}

bool OutOfWorld(Mode mode)
{
    return mode == Mode::Loading || mode == Mode::Pregame || mode == Mode::CharGen || mode == Mode::Unknown;
}

// Mana or stamina, as the party bar's right arc shows it: the one
// depletable property both are (PROPERTY_DEPLETABLE_MANA_STAMINA).
float Energy(ObjectId id)
{
    const float current = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_MANA_STAMINA, k::PROPERTY_VALUE_CURRENT);
    const float total = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_MANA_STAMINA, k::PROPERTY_VALUE_TOTAL);
    return total > 0.0f ? std::clamp(current / total, 0.0f, 1.0f) : 0.0f;
}

// A mage's is mana: its core class, as core_h's GetCreatureCoreClass reads
// it (the current class, or the classes table's BaseClass for a
// specialisation's), is CLASS_WIZARD.
bool Mage(ObjectId id)
{
    constexpr std::int32_t kPropertyCurrentClass = 27; // PROPERTY_SIMPLE_CURRENT_CLASS
    constexpr std::int32_t kClassWizard = 2;           // CLASS_WIZARD
    const auto current =
        static_cast<std::int32_t>(api::GetCreatureProperty(id, kPropertyCurrentClass, k::PROPERTY_VALUE_TOTAL));
    const std::int32_t base = dao::gda::GetInt(dao::script::Table::RulesClasses, current, "BaseClass").value_or(0);
    return (base != 0 ? base : current) == kClassWizard;
}

float Health(ObjectId id)
{
    const float current = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_HEALTH, k::PROPERTY_VALUE_CURRENT);
    const float total = api::GetCreatureProperty(id, k::PROPERTY_DEPLETABLE_HEALTH, k::PROPERTY_VALUE_TOTAL);
    return total > 0.0f ? std::clamp(current / total, 0.0f, 1.0f) : 1.0f;
}

bool Gone(ObjectId id)
{
    return api::IsObjectValid(id) == 0 || api::IsDead(id) != 0;
}

// Whether the game has made a creature inactive (core:
// ActorObservation::inactive; issue 44): the engine's own word, the scripts'
// GetObjectActive, zero for an object their SetObjectActive has disabled.
// script.ldf: "In a 'disabled' state, the object will not appear on the
// client visually, and will receive no AI updates. GameEffects will remain on
// the object but all commands will be cleared upon disabling", and of the
// getter, "Messages will continue to queue up so you are able to assign
// actions before enabling the object". The Broodmother's tentacles go under
// so (orz550_broodmother_h.nss: WR_SetObjectActive(oTentacle, FALSE) as one
// burrows, and as each leaves the fight in stasis, cut to two thirds and then
// one third of its health: orz550cr_bmother_tentacle.nss), and the party's
// own are made so as they leave the active party (module_core.nss,
// EVENT_TYPE_PARTYMEMBER_DROPPED).
bool Inactive(ObjectId id)
{
    return api::GetObjectActive(id) == 0;
}

// Not there for a kept action to be given at, or turned on: dead or gone
// from the area, or made inactive by the game since the action was kept
// (issue 44). The game's own scripts stop the party attacking a tentacle as
// it goes under (orz550_broodmother_h.nss: WR_ClearAllCommands for every
// party member whose attack target it is, before WR_SetObjectActive(...,
// FALSE)), and what the mod keeps for a creature is outside the engine's
// queue, where that does not reach it. In the Broodmother's fight
// (2026-10-05) a Pinning Shot kept at 05:14:42 was given at 05:14:57 at a
// tentacle under ground since 05:14:51: the pin landed on an object out of
// the world, and its command sat in that object's queue, never begun, for
// eleven turns. So an inactive target is as a fallen one: a plain attack is
// turned on the nearest enemy in reach, anything else is not given and
// nothing of it is spent (ReleaseCast). Not yet seen in play.
bool NotThere(ObjectId id)
{
    return Gone(id) || Inactive(id);
}

// The same in words, for the lines that say why a kept action was turned or
// not given: which of the two it was is what a play test's log is read for.
const char *WhyNotThere(ObjectId id)
{
    return Gone(id) ? "is dead or gone" : "is inactive, out of the world";
}

// What is queued on the acting creature while it has no current command,
// said at debug: two lines, for two things the logs do not yet tell.
//
// A queue the engine is not running, by its commands' types: at once for an
// inactive creature, whose turn is over within the frame, and for any other
// once it has stood so more than a second, which no log of play has shown
// outside one fight (in the playthrough's logs to 2026-10-05 every "still at
// a invalid" is of the Broodmother's: a tentacle under ground, issue 44).
// Said again as what is queued changes. What sat on that tentacle is not read
// yet, and this is to read it. By the log it came after the tentacle went
// under (0 queued at 05:14:52, in stasis; then a party member's kept Pinning
// Shot was given at 05:14:57 and "is held (stunned, pinned, knocked down)"
// was read of the tentacle at 05:14:59), and neither the pin's end (the
// machine's, 05:15:24) nor ClearAllCommands, eleven times, took it off.
//
// And how long a command the engine did begin had waited, where that was two
// frames or more: the machine takes two frames with nothing for "its command
// is over", and "busy" counts the queue so that these are not such frames
// (core: ActorObservation::busy). Whether it needs to has not been measured;
// a fight with none of these lines says it does not.
void SayQueueNotRunning(ObjectId id, CommandType current, bool inactive)
{
    using Clock = std::chrono::steady_clock;
    static ObjectId who = dao::kInvalidObject;
    static Clock::time_point since;
    static int frames = 0;
    static bool active = false;
    static std::string said;
    const auto ms = [](Clock::duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); };
    const std::int32_t queued = current == CommandType::Invalid ? api::GetCommandQueueSize(id) : 0;
    const Clock::time_point now = Clock::now();
    if (who != dao::kInvalidObject && (id != who || queued <= 0))
    {
        // Over for the one it was counted of: at its command now, or what was
        // queued is gone, or the turn is another's.
        if (frames >= 2 && active && id == who && current != CommandType::Invalid)
        {
            spdlog::debug("{:#x} stood {} frames ({} ms) with no current command and something queued before the "
                          "engine began its {}",
                          who, frames, ms(now - since), Name(current));
        }
        who = dao::kInvalidObject;
    }
    if (queued <= 0)
    {
        return;
    }
    if (who == dao::kInvalidObject)
    {
        who = id;
        since = now;
        frames = 0;
        said.clear();
    }
    ++frames;
    active = !inactive;
    if ((!inactive && now - since <= std::chrono::seconds{1}) || !spdlog::should_log(spdlog::level::debug))
    {
        return;
    }
    // (Index 0 is "the command at the top of the queue, but not being
    // executed yet": script.ldf, GetCommandByIndex.)
    constexpr std::int32_t kMost = 8;
    std::string types;
    for (std::int32_t i = 0; i < std::min(queued, kMost); ++i)
    {
        const dao::script::OwnedCommand command = api::GetCommandByIndex(id, i);
        types += types.empty() ? "" : ", ";
        types += command ? Name(static_cast<CommandType>(api::GetCommandType(*command.get()))) : "none read";
    }
    if (queued > kMost)
    {
        types += ", ...";
    }
    if (types == said)
    {
        return;
    }
    said = types;
    spdlog::debug("{:#x} has no current command and {} queued that the engine is not running ({}): {}", id, queued,
                  inactive ? "it is inactive" : std::format("{} ms so", ms(now - since)), types);
}

// One a kept attack might be turned on, its own target fallen, as the game
// has it now (core/Rules.h, Foe; issue 22):
// - there and alive;
// - hostile to the attacker by the game's own word: the fight's sides are
//   the party's view, and one fighting beside the party need not be an
//   enemy's enemy;
// - seen: not in stealth (the effect the scripts' IsStealthy asks for; its
//   AI could not have chosen it either), and with a clear line from the
//   attacker (the scripts' CheckLineOfSightObject's question, DAOLib's
//   dao::world::LineOfSight: walls and doors block). A shot with no line is
//   not loosed (issue 11), and a blow at one behind a door is a walk round;
// - how far, and whether in the attacker's reach: the engine's own answer
//   for the pair (DAOLib's dao::attack::ApproachOf, what the gate judges an
//   attack's walk by, so "in reach" here is a walk of none there). And
//   whether the engine's attack will find room on its ring (the approach's
//   findsRoom, DAOLib issue 19): in reach is by the ring as it stands, and
//   in a crowd a blow that does not come in its first update meets the ring
//   filled a frame on, with no place left (issue 27). A kept attack is
//   asked of the engine again as it is given (the gate; core's
//   KeptAttackNotMade), and one turned on a foe it finds no room at would
//   be not made there, where another in reach might have had it: so it is
//   turned only on one it will land on.
// Not yet seen in play.
Foe Measure(ObjectId attacker, ObjectId other)
{
    Foe foe;
    foe.id = other;
    foe.standing = !NotThere(other); // nor one the game has made inactive: nobody is there to strike
    if (!foe.standing)
    {
        return foe;
    }
    foe.hostile = api::IsObjectHostile(attacker, other) != 0;
    foe.meters = api::GetDistanceBetween(attacker, other);
    const std::optional<dao::attack::Approach> approach = dao::attack::ApproachOf(attacker, other);
    foe.inReach = approach && approach->inReach && approach->findsRoom;
    if (approach && approach->inReach && !approach->findsRoom)
    {
        // Said apart: the line of each one measured reads it as out of reach.
        spdlog::debug("{:#010x}: {:#x} is in its reach, and the engine's attack would find no room at it: not one "
                      "to turn a kept attack on",
                      attacker, other);
    }
    foe.seen = api::GetHasEffects(other, k::EFFECT_TYPE_STEALTH) == 0 &&
               static_cast<bool>(dao::world::LineOfSight(attacker, other));
    return foe;
}

// Which of those the machine named a kept attack is turned on (core's
// NearestInReach decides), each as it was measured said at debug: why one
// was passed over is read there.
ObjectId TurnedOn(ObjectId attacker, const std::vector<ObjectId> &others)
{
    std::vector<Foe> foes;
    foes.reserve(others.size());
    for (const ObjectId other : others)
    {
        const Foe &foe = foes.emplace_back(Measure(attacker, other));
        spdlog::debug("{:#010x}: its kept attack on {:#x}? {}{:.1f} m away, {} its reach, {}, {}", attacker, other,
                      foe.standing ? "" : "not there; ", foe.meters, foe.inReach ? "in" : "out of",
                      foe.hostile ? "hostile" : "not hostile", foe.seen ? "seen" : "not seen");
    }
    return NearestInReach(foes);
}

// An attack given as a script gives one: it comes back through the gate.
bool GiveAttack(ObjectId attacker, ObjectId target)
{
    const dao::script::OwnedCommand attack = api::CommandAttack(target);
    if (!attack)
    {
        return false;
    }
    api::AddCommand(attacker, *attack.get(), 1);
    return true;
}

} // namespace

Observation Bridge::Observe(const Needs &needs, std::uint32_t nowMs)
{
    Observation obs;
    obs.nowMs = nowMs;
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady())
    {
        return obs; // Mode::Unknown: no world yet
    }
    obs.mode = ModeOf(*app);
    obs.paused = app->IsPaused();
    if (OutOfWorld(obs.mode))
    {
        // Ids are the save's: another game can reuse them.
        known_.clear();
        keptVoid_.clear();
        faced_.clear();
        party_.clear();
        leaderStands_ = true;
        return obs;
    }
    obs.keptVoid = std::exchange(keptVoid_, {});
    if (needs.scan)
    {
        obs.scan = Scan(needs.inFight);
    }
    if (needs.actor != kNoObject)
    {
        obs.actor = Actor(needs.actor);
    }
    for (const ObjectId id : needs.statuses)
    {
        obs.statuses.push_back(Status(id));
    }
    obs.faced = std::exchange(faced_, {});
    for (const ObjectId id : needs.turning)
    {
        if (const std::optional<TurnLook> look = TurnLookOf(id))
        {
            obs.turns.push_back(*look);
        }
    }
    return obs;
}

namespace
{
// The party as the game has it (the scripts' GetPartyList), each member once
// and in the list's order, with whether it lives and whether the game has it
// in combat: what core says where the party is from (core/Rules.h,
// PartyMember; issue 42). Asked afresh wherever the party is asked about:
// who lives changes inside a fight.
std::vector<PartyMember> ReadParty(ObjectId hero)
{
    std::vector<PartyMember> party;
    const dao::script::Array list = api::GetPartyList(hero);
    for (std::size_t i = 0; i < list.Size(); ++i)
    {
        const ObjectId id = list.Object(i);
        if (api::IsObjectValid(id) != 0 && std::ranges::find(party, id, &PartyMember::id) == party.end())
        {
            party.push_back(PartyMember{id, api::IsDead(id) == 0, api::GetCombatState(id) != 0});
        }
    }
    return party;
}

// The party's leader as the game names it; the hero if it names none.
// GetPartyLeader answers the party's leader field, the one conversations take
// for PLAYER, and only SetPartyLeader changes that: death does not, and
// neither does the player selecting someone else (DAOLib's session, which
// read the engine for it, 2026-10-04). GetMainControlled is the creature the
// player controls. Both answer as designed, and neither says where the party
// is: in the Ortan Thaig fight of issue 42, asked by script with the hero
// dead and the last of the party standing 83 m from her body, GetPartyLeader
// answered the hero (IsDead 1) and GetMainControlled the survivor. So the
// leader is not asked anything here: who stands for the party is core's to
// say, of the leader, the one controlled and who lives (StandsForTheParty).
ObjectId Leader(ObjectId hero)
{
    const ObjectId leader = api::GetPartyLeader();
    return api::IsObjectValid(leader) != 0 ? leader : hero;
}

// The one the player acts as, where the player is looking: the creature
// controlled (GetMainControlled) while it lives, else who stands for the
// party (core: ActsForThePlayer); with nobody living, the hero all the same;
// kNoObject with no hero. A word to the player floats over it (Say), and the
// camera stays with it on the turn of a creature it has no place to stand
// round (CameraDirector::TurnTo).
ObjectId PlayersOwn()
{
    const ObjectId hero = api::GetHero();
    if (api::IsObjectValid(hero) == 0)
    {
        return kNoObject;
    }
    const ObjectId own = ActsForThePlayer(ReadParty(hero), Leader(hero), api::GetMainControlled());
    return own != kNoObject ? own : hero;
}

// How far a creature is from the party: its distance to each living member,
// the nearest of them core's to say (MetersFromTheParty). Infinity with
// nobody living.
float FromTheParty(ObjectId id, const std::vector<ObjectId> &living)
{
    std::vector<float> meters;
    meters.reserve(living.size());
    for (const ObjectId member : living)
    {
        meters.push_back(api::GetDistanceBetween(member, id));
    }
    return MetersFromTheParty(meters);
}

// Whether a creature in combat near the party is one the player can see
// fighting (core: Combatant::engaged), by the game's own answers. Not by
// distance: Howe's men fighting guards rooms away were in the turn order,
// and the camera went to rooms drawn black (the user, 2026-09-30, whose own
// rule was to leave out those in areas that are not loaded).
// - It or a party member is the other's attack target.
// - Else, it stands in a room the game shows (dao::world::RoomOf, DAOLib
//   1d99f54). An area is made of rooms, and the game hides every room no
//   party member has yet seen, its creatures inactive, even through an
//   open door: in lib-agent's play test the room behind a door stayed
//   black, door open, until the hero walked in. What is seen goes by the
//   party, not the camera, so the mod's camera going there shows nothing.
//   A line of sight alone, the first rule here, is clear through that
//   door. The room's `seen` is read from the function that hides rooms,
//   not yet watched in play.
// - Where the area has no room for it, a party member's line of sight to
//   it (CheckLineOfSightObject's question: walls and doors block).
// And how far it is from the nearest party member: seen is not enough out
// of doors, where every room in the radius is shown and a battle of wolves
// and darkspawn 45 m off joined the party's (issue 6). What these make of
// it is core's (InThePartysFight).
// A party member is a living one in all of this (issue 42): a fallen one's
// attack target is the one it had, its line of sight is from where it lies,
// and nobody is near the party for being near its body.
Sighting Sight(ObjectId id, const std::vector<ObjectId> &living, const std::optional<dao::world::Room> &room)
{
    Sighting sighting;
    const ObjectId target = api::GetAttackTarget(id);
    for (const ObjectId member : living)
    {
        if (target == member || api::GetAttackTarget(member) == id)
        {
            sighting.fightsParty = true;
        }
    }
    sighting.meters = FromTheParty(id, living);
    sighting.seen = room ? room->Shown() : std::ranges::any_of(living, [&](ObjectId member) {
        return static_cast<bool>(dao::world::LineOfSight(member, id));
    });
    return sighting;
}

// What the engine's perception says of a creature that is not the party's
// (core: Perception; issue 29): whether a party member perceives it, whether
// it perceives a party member, and whether it perceives anyone hostile to
// it. The engine's own lists, asked through the scripts' calls: IsPerceiving
// (A, B) is whether B is on A's perception list, "a cheap check ... it does
// not perform expensive line of sight checking" (script.ldf), and
// IsPerceivingHostiles is the test the game's own scripts take a creature
// out of combat by (combat_h.nss, Combat_HandleCreatureDisappear). What a
// creature has noticed is the engine's to say; the mod keeps no distance or
// line of sight of its own for it. Not yet seen in play.
// Asked of every member, fallen or not, where distances are the living's
// alone (issue 42): these are the engine's own lists, and it drops the fallen
// from them (in that issue's fight the golem in whose turn the third member
// fell, 3.5 m from her, read as perceiving no party member as she did).
Perception Perceived(ObjectId id, const std::vector<ObjectId> &party)
{
    Perception perception;
    perception.byParty =
        std::ranges::any_of(party, [&](ObjectId member) { return api::IsPerceiving(member, id) != 0; });
    perception.ofParty =
        std::ranges::any_of(party, [&](ObjectId member) { return api::IsPerceiving(id, member) != 0; });
    perception.ofFoes = api::IsPerceivingHostiles(id) != 0;
    // And the game's own word for its having noticed an enemy: its combat
    // state, which the scripts set as it perceives a hostile and run its
    // combat AI by (rules_core.nss).
    perception.inCombat = api::GetCombatState(id) != 0;
    return perception;
}
} // namespace

// Who is fighting near the party, by the rules that bring a creature into a
// fight; and, of those the machine has in the fight already (inFight), the
// living ones those rules no longer find, each with what the engine's
// perception says of it, which is what keeps it in the fight or does not
// (core: StaysInTheFight; issue 29).
//
// The user, 2026-10-04, of issues 42 and 43: "If you run far enough away
// from the enemies, the fight should end. That's what rtwp does. In that
// case the enemies should go back to normal. Getting close to them should
// retrigger a fight." Both ends of that are here.
// - Leaving is the engine's perception and no distance of the mod's (issue
//   29). With the whole party run off, an enemy of the fight is found by
//   none of the rules below (it is out of combat, or beyond combat_radius of
//   every living member), is read all the same with its perception, and has
//   left once neither it nor the party perceives the other; when the last
//   has, the fight is over with them alive. One member still within reach of
//   them is a party that has not run off, and the fight goes on.
// - Coming back begins one. The rules that bring a creature in ask where
//   the party is of every living member (issue 42), so any one of them back
//   within combat_radius of hostiles in combat, or in sight of ones that
//   have not noticed with nobody else to fight, is a fight again, whoever
//   leads and wherever the leader stands. Asked of the leader alone, as it
//   was, a companion sent back by itself found nobody.
// What the enemies left behind do meanwhile is not the scan's (issue 43).
std::vector<Combatant> Bridge::Scan(const std::vector<ObjectId> &inFight)
{
    std::vector<Combatant> out;
    const ObjectId hero = api::GetHero();
    if (api::IsObjectValid(hero) == 0)
    {
        return out;
    }
    std::unordered_set<ObjectId> seen;

    const std::vector<PartyMember> party = ReadParty(hero);
    std::vector<ObjectId> members;
    members.reserve(party.size());
    for (const PartyMember &member : party)
    {
        seen.insert(member.id);
        members.push_back(member.id);
        out.push_back(Describe(member.id, Side::Party, member.id == hero));
    }
    party_ = members; // for the acting creature's perception, read every frame (Actor)
    // Where the party is: where its living members are (core/Rules.h, "Where
    // the party is"; issue 42). Everything below that is a distance, a line
    // of sight or a list of who is near is asked of each of them, and what
    // must be asked of one creature is asked of the one who stands for the
    // party, which is the leader only while the leader lives. Until
    // 2026-10-04 all of it was the leader's: the nearest hostiles were the
    // nearest to the leader, combat_radius was measured from the leader and
    // the party's combat was the leader's combat state. Twice that evening
    // in Ortan Thaig no enemy beside a party member was found: with the
    // leader fallen 83 m from the last one standing, and, from the same save
    // with nobody dead, with the leader standing 70 m from the three others.
    //
    // Who the two are, read in the engine by DAOLib's session, 2026-10-04:
    // GetPartyLeader answers the party's leader field (the one conversations
    // take for PLAYER), which only SetPartyLeader changes, not death and not
    // the player selecting someone else; GetMainControlled answers the
    // creature the player controls. Both answer as designed. So the leader
    // is a fixed one of the party, in most of the game the hero wherever the
    // player has gone, and who stands for the party is chosen among the
    // living (the leader first, so that a scan with the leader alive begins
    // as it did); who the player acts as is the one controlled (Say).
    const ObjectId leader = Leader(hero);
    const ObjectId controlled = api::GetMainControlled();
    const std::vector<ObjectId> living = LivingMembers(party, leader, controlled);
    // With nobody living there is nobody to measure from and nobody is found
    // near (the game's mode goes to Dead, and the machine ends the fight by
    // that): the hero then, for what death does not change, whose side a
    // creature is on.
    const ObjectId stands = living.empty() ? hero : living.front();
    if (!living.empty())
    {
        SayWhoStands(stands, leader, controlled);
    }
    std::size_t outside = 0;
    // Those the player cannot see fighting, each with its room as the game
    // has it (seen: 0 never, 1 in view, 2 before; load: 0 loading, 1
    // loaded, 2 not), for a play test to set beside what the screen shows.
    std::string rooms;
    // The rooms are gone by only where they can be: the party's own room,
    // which the game shows if it shows any, must read as shown. An area
    // with rooms where the game never marks one seen would read every
    // creature as hidden, and no fight there would ever be in turns
    // (lib-agent's caution, 2026-10-01: not seen, and this would say). A
    // party member's line of sight is the rule there, as where an area has
    // no rooms. The party's own room is the room of the one who stands for
    // it: a living member's, which the game shows for its standing there.
    const std::optional<dao::world::Room> ours = settings_.joinBySight ? dao::world::RoomOf(stands) : std::nullopt;
    const bool byRooms = ours && ours->Shown();
    // Said when it changes, at info: which rule an area goes by is read
    // out of a player's own log. Outdoors (the Dalish forest, 2026-10-01,
    // by Eluvian's inspect) every creature's room, the party's own too,
    // reads in view and not loaded, so sight is the rule there.
    if (settings_.joinBySight && byRooms_ != byRooms)
    {
        byRooms_ = byRooms;
        if (byRooms)
        {
            spdlog::info("who is in a fight here goes by the rooms the party has seen");
        }
        else if (ours)
        {
            spdlog::info("who is in a fight here goes by sight: the party's own room {} (where {:#x} stands for it) "
                         "reads seen {} load {}, not shown",
                         ours->number, stands, ours->seen, ours->load);
        }
        else
        {
            spdlog::info("who is in a fight here goes by sight: the area has no rooms");
        }
    }
    const auto add = [&](ObjectId id, Side side, bool unaware = false) {
        Combatant c = Describe(id, side, false);
        // What it has noticed, and whether the party has noticed it, by the
        // engine's perception: a hostile that perceives nobody to fight has
        // its turn skipped (core: Unaware). Said at debug for one that
        // perceives nobody, each scan: a play test reads there when it
        // noticed.
        c.perception = Perceived(id, members);
        if (!c.perception.ofParty && !c.perception.ofFoes)
        {
            spdlog::debug("{:#x} perceives no party member and nobody hostile to it, and is {} combat; the party "
                          "{} it",
                          id, c.perception.inCombat ? "in" : "not in",
                          c.perception.byParty ? "perceives" : "does not perceive");
        }
        // One drawn in though it has not noticed is in the party's fight by
        // the rule that drew it in (seen, within the join radius).
        if (settings_.joinBySight && !unaware)
        {
            const std::optional<dao::world::Room> room = byRooms ? dao::world::RoomOf(id) : std::nullopt;
            const Sighting sighting = Sight(id, living, room);
            c.engaged = InThePartysFight(sighting, settings_);
            if (!c.engaged)
            {
                ++outside;
                if (spdlog::should_log(spdlog::level::debug))
                {
                    // Seen and still outside: fighting someone else, too far
                    // off to be the party's.
                    rooms +=
                        sighting.seen
                            ? std::format(" {:#x} seen, {:.0f} m off, not fighting the party", id, sighting.meters)
                        : room ? std::format(" {:#x} room {} seen {} load {}", id, room->number, room->seen, room->load)
                               : std::format(" {:#x} no room, out of sight", id);
                }
            }
        }
        out.push_back(std::move(c));
    };
    // The game's own question for who is fighting the party (UT_Surrender,
    // utility_h.nss): the nearest living hostiles, those in combat. Asked
    // round each living member, the one who stands for the party first, and
    // each creature taken once: the engine answers with the nearest
    // kMaxCreaturesInCombat to the creature it is asked of, so asked of one
    // alone it names nobody beside another who stands far from that one, or
    // behind a crowd of two dozen nearer to it. Asked of each, more than that
    // many can be near the party; none is turned away for the count, since
    // each is among the nearest to someone. Whose side a creature is on stays
    // one creature's word, as it was the leader's: one named by another
    // member alone is taken as the one who stands for the party has it (a
    // member turned against the party would name its friends).
    const auto nearTheLiving = [&](std::int32_t hostile) {
        std::vector<ObjectId> found;
        for (const ObjectId member : living)
        {
            const dao::script::Array nearest = api::GetNearestObjectByHostility(
                member, hostile, k::OBJECT_TYPE_CREATURE, kMaxCreaturesInCombat, 1, 0, 0);
            for (std::size_t i = 0; i < nearest.Size(); ++i)
            {
                const ObjectId id = nearest.Object(i);
                if (std::ranges::find(found, id) != found.end() ||
                    (member != stands && (api::IsObjectHostile(stands, id) != 0) != (hostile != 0)))
                {
                    continue;
                }
                found.push_back(id);
            }
        }
        return found;
    };
    // combat_radius is how far from the party a fight takes a creature in
    // from: from the nearest of its living members (core: WithinReach). It
    // takes nobody out any more: one in the fight that this no longer finds
    // is read further down, and stays by the engine's perception (issue 29).
    const auto fighting = [&](ObjectId id) {
        return api::IsObjectValid(id) != 0 && !seen.contains(id) && api::GetCombatState(id) != 0 &&
               WithinReach(FromTheParty(id, living), settings_);
    };
    const std::vector<ObjectId> hostiles = nearTheLiving(1);
    for (const ObjectId id : hostiles)
    {
        if (fighting(id))
        {
            seen.insert(id);
            add(id, Side::Enemy);
        }
    }
    // And a hostile that has not noticed the party, with the party in combat
    // and nobody in its fight to fight (core: FightsUnawares; issues 20 and
    // 24): seen and within the join radius it is in the fight, and stays in
    // it once drawn in. Sight is a party member's line here whatever the
    // rooms say: a room the game shows may hold a hostile behind a pillar,
    // and this one is not fighting anyone to be seen by.
    // The party's combat is any living member's (core: PartyIsInCombat): a
    // fallen leader's combat state is 0 with the rest of the party fighting.
    const bool partyInCombat = PartyIsInCombat(party);
    const bool someoneToFight =
        std::ranges::any_of(out, [](const Combatant &c) { return c.side == Side::Enemy && c.engaged; });
    const auto unawares = [&](ObjectId id) {
        if (!partyInCombat || api::IsObjectValid(id) == 0 || seen.contains(id) || api::GetCombatState(id) != 0 ||
            api::IsDead(id) != 0 || !WithinReach(FromTheParty(id, living), settings_))
        {
            return false;
        }
        const bool drawnIn = unawares_.contains(id);
        // The lines of sight are asked only where the answer hangs on them.
        Sighting sighting;
        if (!drawnIn && !someoneToFight)
        {
            sighting = Sight(id, living, std::nullopt);
        }
        return FightsUnawares(partyInCombat, someoneToFight, drawnIn, sighting, settings_);
    };
    std::unordered_set<ObjectId> drawn;
    for (const ObjectId id : hostiles)
    {
        if (!unawares(id))
        {
            continue;
        }
        seen.insert(id);
        add(id, Side::Enemy, true);
        if (!unawares_.contains(id))
        {
            spdlog::info("{} ({:#x}) has not noticed the party and is in sight within {:.0f} m, with nobody else to "
                         "fight: in the fight",
                         Utf8(Remember(id).name), id, settings_.joinRadius);
        }
        drawn.insert(id);
    }
    unawares_ = std::move(drawn); // who is in the fight unawares now: each said once, and kept in while it is
    // Creatures in combat that are not hostile to the party fight beside it.
    const std::vector<ObjectId> others = nearTheLiving(0);
    for (const ObjectId id : others)
    {
        if (fighting(id))
        {
            seen.insert(id);
            add(id, Side::Ally);
        }
    }
    // Those the machine has in the fight that nothing above found (issue
    // 29): out of combat, or further than combat_radius from every living
    // member of the party.
    // Until 2026-10-04 that was the end of them: left out of the scan, they
    // were out of the fight four seconds later, and stood where they were as
    // any creature outside the order does, whoever they were chasing. The
    // game itself keeps a fight by perception, not by a distance
    // (combat_h.nss, Combat_HandleCreatureDisappear), so each is read with
    // the engine's perception between it and the party, and core decides
    // whether it stays (StaysInTheFight). Its side is the game's word as of
    // now: one that has surrendered is no enemy. Not yet seen in play.
    std::size_t afar = 0;
    for (const ObjectId id : inFight)
    {
        if (seen.contains(id) || api::IsObjectValid(id) == 0 || api::IsDead(id) != 0)
        {
            continue;
        }
        seen.insert(id);
        Combatant c = Describe(id, api::IsObjectHostile(stands, id) != 0 ? Side::Enemy : Side::Ally, false);
        c.fighting = false;
        c.perception = Perceived(id, members);
        ++afar;
        // (Inactive, said here: in the Broodmother's fight the four tentacles
        // under ground, in combat 14 to 16 m from the party, each read "not
        // found fighting", and nothing in the line said why. That the
        // engine's own question for who is near does not answer with an
        // object its scripts have disabled is the reading; this is to show
        // it. Issue 44.)
        spdlog::debug("{:#x} is in the fight and not found fighting (combat state {}, {:.0f} m from the nearest "
                      "living party member{}): the party {} it; it perceives {}, {}",
                      id, api::GetCombatState(id), FromTheParty(id, living), Inactive(id) ? ", inactive" : "",
                      c.perception.byParty ? "perceives" : "does not perceive",
                      c.perception.ofParty ? "a party member" : "no party member",
                      c.perception.ofFoes ? "someone hostile to it" : "nobody hostile to it");
        out.push_back(std::move(c));
    }
    // What was measured from, with the counts: how many of the party live,
    // who stands for it, and how many living creatures the engine named round
    // its living members, each once (not, as until 2026-10-04, round its
    // leader).
    spdlog::debug("scan: {} in the party, {} living ({}); {} hostiles and {} others near the living; {} fighting, {} "
                  "of them not the party{}{}; {} of the fight not found fighting",
                  party.size(), living.size(),
                  living.empty() ? std::string{"nobody stands for it"} : std::format("{:#x} stands for it", stands),
                  hostiles.size(), others.size(), out.size() - afar, outside, rooms.empty() ? "" : ":", rooms, afar);
    return out;
}

// Whether the leader stands for the party, said at info as it changes: a
// play test's log is read for whom a scan measured from, and with the leader
// down that is someone else (issue 42). Who it then is moves with the
// player's control, which the mod itself moves with each party member's turn,
// so each scan's own line says who (at debug), and this one only that the
// leader no longer does, and does again.
void Bridge::SayWhoStands(ObjectId stands, ObjectId leader, ObjectId controlled)
{
    const bool leaderStands = stands == leader;
    if (leaderStands == leaderStands_)
    {
        return;
    }
    leaderStands_ = leaderStands;
    if (leaderStands)
    {
        spdlog::info("the party's leader {} ({:#x}) stands for the party again", Utf8(Remember(leader).name), leader);
        return;
    }
    spdlog::info("the party's leader {} ({:#x}) is {}: {} ({:#x}) stands for the party, {}",
                 Utf8(Remember(leader).name), leader, api::IsDead(leader) != 0 ? "down" : "not of the party here",
                 Utf8(Remember(stands).name), stands,
                 stands == controlled ? "the one the player controls" : "the first of it living");
}

namespace
{

// An effect's engine parameters: the second list of parameters by index an
// effect carries, 0x54 into it, beside the scripts' own at its start.
//
// Read in the executable 2026-10-05 (issue 49). The scripts write a movement
// rate effect's potency with SetEffectEngineFloat (EffectModifyMovementSpeed,
// effects_h.nss), not SetEffectFloat, and the four SetEffectEngine* commands
// (one body, 0x00660650) call the same setters as SetEffectFloat and its kin
// on the effect plus 0x54, where those call them on the effect itself. An
// effect's copy constructor (0x00A65130) copies both lists, so a copy out of
// GetEffects has the engine's. No script command reads the engine's list:
// GetEffectFloat (0x00651270) reads the scripts' own, which a movement rate
// effect leaves empty. So `api::GetEffectFloat(effect, EFFECT_FLOAT_POTENCY)`,
// which this file asked until then, answered 0.0 for every Haste, salve and
// slow, a potency that is no rate and is left out (core/Rules.h,
// MovementRate): no creature's pace ever followed its effects, and the line
// that would have said one did was never written (a Swift Salve, Haste and a
// caltrop trap in two staged fights, the trap's effect seen on its creature
// by Eluvian). The engine's own reads of the potency are the same getter on
// the effect plus 0x54 (0x0079F9F3, 0x007A062A).
//
// The layout is DAOLib's to keep (dao/Structures.h, CGameEffect, whose other
// fields it reads the same in every build): asked for in
// dev/DAOLIB_NEEDS.md, and this goes when DAOLib has it. Not yet seen in
// play.
const dao::CDataArrays &EngineParameters(const dao::CGameEffect &effect)
{
    constexpr std::size_t kEngineParameters = 0x54;
    return *reinterpret_cast<const dao::CDataArrays *>(reinterpret_cast<const std::byte *>(&effect) +
                                                       kEngineParameters);
}

// Haste and slows, as they stand on a creature now: each movement rate
// effect's potency, by the effects' two types (core/Types.h,
// MovementEffects). What they come to is core's to say (core/Rules.h,
// MovementRate, Pace), and the machine says what was read as each turn
// begins, and as a pace changes within one (TurnMachine::SayPace,
// FollowPace). Read with the look at everyone (Status), not with the scan:
// two script calls a creature each half second, as each action ends and as
// each turn begins. From the scripts' thread; DAOLib's probe has checked
// GetEffects in play.
MovementEffects MovementEffectsOn(ObjectId id)
{
    const auto potencies = [id](std::int32_t type) {
        std::vector<float> found;
        const dao::script::Array effects = api::GetEffects(id, type);
        for (std::size_t i = 0; i < effects.Size(); ++i)
        {
            if (const dao::CGameEffect *effect = effects.Effect(i))
            {
                found.push_back(EngineParameters(*effect).Float(k::EFFECT_FLOAT_POTENCY));
            }
        }
        return found;
    };
    MovementEffects on;
    on.rates = potencies(k::EFFECT_TYPE_MOVEMENT_RATE);
    on.debuffs = potencies(k::EFFECT_TYPE_MOVEMENT_RATE_DEBUFF);
    return on;
}

// A skill's rank, 0 to 4: the highest of its four abilities the creature
// has. The skills' abilities are numbered by rank from the first
// (ABILITY_SKILL_COMBAT_TACTICS_1 100110 to _4 100113, ABILITY_SKILL_
// SURVIVAL_1 100051 to _4 100054; 2da_constants_h.nss, read 2026-10-02).
constexpr std::int32_t kCombatTactics1 = 100110;
constexpr std::int32_t kSurvival1 = 100051;

int SkillRank(ObjectId id, std::int32_t first)
{
    constexpr int kTopRank = 4;
    for (int rank = kTopRank; rank >= 1; --rank)
    {
        if (api::HasAbility(id, first + rank - 1) != 0)
        {
            return rank;
        }
    }
    return 0;
}

} // namespace

const Bridge::Known &Bridge::Remember(ObjectId id)
{
    const auto found = known_.find(id);
    if (found != known_.end())
    {
        return found->second;
    }
    // Its pace comes with its appearance, which does not change: its combat
    // speed in the appearance table. Not the scripts' movement speed
    // property, which is the flanking angle in the shipped table (60 for
    // everyone in the first session, 2026-09-30). Haste and slows are
    // effects, which come and go: read with every look at everyone (Status).
    const float combatSpeed =
        dao::gda::GetFloat(dao::script::Table::Appearance, api::GetAppearanceType(id), "Game_CbtSpeed").value_or(0.0f);
    // Its skills' ranks (with a turn a bar of time, Combat Tactics' time
    // and rollover; Survival's initiative), which a fight does not change.
    Known known{api::GetName(id),
                api::GetTag(id),
                api::GetCreatureRank(id),
                SpeedFactor(combatSpeed),
                SkillRank(id, kCombatTactics1),
                SkillRank(id, kSurvival1)};
    // Once per creature: what the rules will read of it, to check against
    // the game.
    spdlog::info("{:#010x} {} ({}): rank {}, dexterity {}, combat speed {:.2f} m/s, {:.2f} a person's, Combat "
                 "Tactics {}, Survival {}",
                 id, Utf8(known.name), Utf8(known.tag), known.rank,
                 api::GetCreatureProperty(id, k::PROPERTY_ATTRIBUTE_DEXTERITY, k::PROPERTY_VALUE_TOTAL), combatSpeed,
                 known.speed, known.tactics, known.survival);
    return known_.emplace(id, std::move(known)).first->second;
}

Combatant Bridge::Describe(ObjectId id, Side side, bool hero)
{
    const Known &known = Remember(id);
    Combatant c;
    c.id = id;
    c.side = side;
    c.hero = hero;
    c.name = known.name;
    c.tag = known.tag;
    c.rank = known.rank;
    c.tactics = known.tactics;
    c.survival = known.survival;
    c.dexterity = api::GetCreatureProperty(id, k::PROPERTY_ATTRIBUTE_DEXTERITY, k::PROPERTY_VALUE_TOTAL);
    // Its build's pace, remembered. Its Haste and slows are not a scan's to
    // read (Status; core: CombatantStatus::movement, which the machine keeps
    // through a scan as it keeps the creature's mana or stamina): a scan
    // comes as a turn begins and now and then, and with a turn a bar of time
    // the pace is the creature's as it is now (issue 49).
    c.speed = known.speed;
    c.health = Health(id);
    c.down = api::IsDead(id) != 0;
    return c;
}

namespace
{

// What the engine's command for an effect is made of, said at debug as it
// changes, for the creature whose turn it is: asked by DAOLib's session (its
// issue 18, the mod's 30), so that a play test shows a knockback's sequence
// and how far ahead a hold's time can be known. The sub-actions left on the
// command, the running one first, each with its type and its duration as that
// is set: none for an untimed one, and for a timed one until it begins (an
// animation, 37, takes its length only then). Said again when a type or a
// duration changes or a delay ends, not as the time elapsed moves, which it
// does every frame. And the command's own parameters, the first of each kind
// (past the last the engine gives 0, 0.0 and no object): which effect the
// command runs is thought to be among them, and is not read yet (DAOLib's
// dao/SubActions.h).
void SayEffectCommand(ObjectId id, const dao::Command &command, const std::vector<dao::commands::SubAction> &subs)
{
    if (!spdlog::should_log(spdlog::level::debug))
    {
        return;
    }
    std::string key;
    std::string said;
    for (const dao::commands::SubAction &sub : subs)
    {
        key += std::format("{}:{}:{};", sub.type, sub.durationMs, sub.delayMs > 0);
        said += said.empty() ? "" : ", ";
        said += std::format("{} ({}{})", sub.type,
                            sub.durationMs > 0 ? std::format("{} of {} ms", sub.elapsedMs, sub.durationMs)
                                               : std::string{"untimed, or not begun"},
                            sub.delayMs > 0 ? std::format(", after {} ms", sub.delayMs) : std::string{});
    }
    std::string parameters = "none";
    if (const dao::CDataArrays *given = command.Parameters())
    {
        parameters = std::format("ints [{}, {}, {}, {}, {}, {}, {}, {}], objects [{:#x}, {:#x}, {:#x}, {:#x}], "
                                 "floats [{:.3f}, {:.3f}, {:.3f}, {:.3f}]",
                                 given->Integer(0), given->Integer(1), given->Integer(2), given->Integer(3),
                                 given->Integer(4), given->Integer(5), given->Integer(6), given->Integer(7),
                                 given->Object(0), given->Object(1), given->Object(2), given->Object(3),
                                 given->Float(0), given->Float(1), given->Float(2), given->Float(3));
    }
    key += parameters;
    static ObjectId lastWho = dao::kInvalidObject;
    static std::string lastKey;
    if (id == lastWho && key == lastKey)
    {
        return;
    }
    lastWho = id;
    lastKey = std::move(key);
    spdlog::debug("{:#x} is at an effect's command: sub-actions {}; its parameters: {}", id,
                  said.empty() ? "none left" : said, parameters);
}

// The creature whose turn it is, found at a long step the engine times
// (issue 28): said at debug once a step, by who, which sub-action and its
// length, so that a log shows which of the game's actions are long by the
// engine's own count before the limit's line ever comes (four seconds and
// more: a person's death blow is 2.0 to 2.3 s, an ogre's 12.5).
void SayLongStep(ObjectId id, CommandType command, const dao::commands::SubAction &running)
{
    constexpr std::int32_t kLong = 4000;
    static ObjectId lastWho = dao::kInvalidObject;
    static std::uint32_t lastType = 0;
    static std::int32_t lastLength = 0;
    if (running.durationMs < kLong)
    {
        return;
    }
    if (id == lastWho && running.type == lastType && running.durationMs == lastLength)
    {
        return;
    }
    lastWho = id;
    lastType = running.type;
    lastLength = running.durationMs;
    spdlog::debug("{:#x}'s {} is at sub-action {}, which the engine times: {} ms long, {} ms of it done{} (issue 28)",
                  id, Name(command), running.type, running.durationMs, running.elapsedMs,
                  running.delayMs > 0 ? std::format(", {} ms still to wait before it runs", running.delayMs)
                                      : std::string{});
}

// The creature whose turn it is, found at its own mode's command (core:
// ActorObservation::ownMode; issue 51): said once as it is found so, and
// again when it is found so after it was not, or of another mode: on each of
// its turns while the mode is on, the frame looking at another creature
// between. Which mode by its ability, and which of its effects keeps the
// creature there, by its kind. Nothing on the screen says it yet, and what
// the machine then does with the turn (waits for the player, or spends it on
// the mode) is read against this line in a play test's log. Not yet seen in
// play.
void SayOwnMode(ObjectId id, std::int32_t mode, std::int32_t effectType)
{
    static ObjectId lastWho = dao::kInvalidObject;
    static std::int32_t lastMode = kNoMode;
    if (id == lastWho && mode == lastMode)
    {
        return;
    }
    lastWho = id;
    lastMode = mode;
    if (mode == kNoMode)
    {
        return;
    }
    spdlog::info("{:#x} is at its own mode's command, not held: its ability {} ({}) is a mode and is on, and the "
                 "effect of type {} ({}) that it put on itself by it keeps it there",
                 id, mode, Utf8(dao::abilities::Name(mode).value_or(std::wstring{L"no name read"})), effectType,
                 dao::script::Name(static_cast<dao::script::EffectType>(effectType)));
}

} // namespace

ActorObservation Bridge::Actor(ObjectId id)
{
    ActorObservation actor;
    actor.id = id;
    actor.exists = api::IsObjectValid(id) != 0;
    if (!actor.exists)
    {
        return actor;
    }
    actor.down = api::IsDead(id) != 0;
    std::int32_t ownModeEffect = 0; // the kind of effect its own mode keeps it at a command by, if one does
    if (const dao::script::OwnedCommand current = api::GetCurrentCommand(id))
    {
        actor.current = static_cast<CommandType>(api::GetCommandType(*current.get()));
        // The step the command is at, where the engine times it: its elapsed
        // time and its length, whatever the command (the running sub-action,
        // the first of DAOLib's dao::commands::SubActions; a length of 0 is a
        // step the engine does not time, or one not begun). The machine's
        // limits do not count the time an action is at a timed step that
        // advances (core/TimedStep.h, issue 28: an ogre's death blow is
        // sub-action 37 with a length of 12,500 ms, measured in play
        // 2026-10-05 and read here as that 2026-10-06, and was stopped half a
        // second before its end). One still waiting out its delay has not
        // begun, and is given as untimed.
        if (const std::vector<dao::commands::SubAction> steps = dao::commands::SubActions(id); !steps.empty())
        {
            const dao::commands::SubAction &running = steps.front();
            if (running.delayMs <= 0)
            {
                actor.step = StepTime{running.elapsedMs, running.durationMs};
            }
            SayLongStep(id, actor.current, running);
        }
        // At an attack: whether its command is still at its approach, as the
        // engine itself has it (the sub-action the command is at). The
        // machine judges an attacker that stands still by this (core:
        // ActorObservation::approaching), not by a walk worked out as the
        // command was let through: an archer in range, or a swordsman in
        // reach with others to walk round, was "short of where its walk was
        // to end" by the path's length and stopped as it shot or swung
        // (issue 11). A read of the command's own list: nothing is searched
        // or cast each frame.
        if (actor.current == CommandType::ExecuteEffect)
        {
            // How long the engine's own count has this command still to run,
            // where it can say: the sub-actions left on it, each with its
            // delay and the timed time it has left (DAOLib's
            // dao::commands::HeldFor, its issue 18). Not known, it is "at
            // least" that: one of them is untimed or has not begun (an
            // animation takes its length only as it begins), or is spent
            // and still there, or none is left of a command still current.
            // No command at all leaves both as they are, nothing known. The
            // machine says it beside the time the hold then ran, which is
            // what the hold costs (core: TurnMachine::HoldRuns, issue 30).
            // Seen in play 2026-10-04 with DAOLib 57e1540, which read known
            // and 0.00 s for every knockback of the high dragon's (its one
            // sub-action spent, the fall and rise on no list, 0.61 to
            // 3.19 s): since DAOLib 5e0108a that reads at least 0.
            const std::vector<dao::commands::SubAction> subs = dao::commands::SubActions(id);
            if (const std::optional<dao::commands::Held> held = dao::commands::HeldFor(id))
            {
                actor.heldSeconds = held->seconds;
                actor.heldKnown = held->known;
            }
            SayEffectCommand(id, *current.get(), subs);
            // What holds it, as the engine has it: every temporary effect
            // standing on the creature, with the ability that applied it,
            // who did and the seconds it has left (DAOLib's
            // dao::effects::On). For the machine's lines: which of them the
            // command is of is not read (the diagnostic above is to show).
            //
            // And whether the command is its own mode's, not a hold (core:
            // ActorObservation::ownMode, Rules.h's OwnModeAtItsCommand;
            // issue 51): by an effect standing on it that it put on itself,
            // by an ability that is a mode of its own and on, of a kind that
            // keeps its creature at a command (Captivating Song's singing
            // effect, Feign Death's). The mode is asked about only for an
            // effect that passes the two cheap tests, so a creature with no
            // such effect costs no table read and no script call more.
            std::vector<EffectOn> standing;
            for (const dao::effects::Standing &effect : dao::effects::On(id))
            {
                EffectOn on;
                on.type = static_cast<std::int32_t>(effect.type);
                on.ability = effect.ability;
                on.creator = effect.creator;
                on.modeOn = on.creator == id && KeepsItsCreatureAtACommand(on.type) && on.ability > 0 &&
                            dao::gda::GetInt(dao::script::Table::AbilitiesTalents, on.ability, "UseType").value_or(0) ==
                                kUseTypeMode &&
                            api::IsModalAbilityActive(id, on.ability) != 0;
                standing.push_back(on);
                if (!(effect.remaining > 0.0f))
                {
                    continue;
                }
                if (!actor.effects.empty())
                {
                    actor.effects += "; ";
                }
                actor.effects += std::format("effect type {} of ability {} by {:#x}, {:.1f} of {:.1f} s left",
                                             static_cast<std::int32_t>(effect.type), effect.ability, effect.creator,
                                             effect.remaining, effect.duration);
            }
            if (actor.effects.empty())
            {
                actor.effects = "no temporary effect stands on it";
            }
            actor.ownMode = OwnModeAtItsCommand(id, standing);
            if (actor.ownMode != kNoMode)
            {
                const auto own = std::ranges::find_if(standing, [&](const EffectOn &on) {
                    return on.modeOn && on.ability == actor.ownMode && KeepsItsCreatureAtACommand(on.type);
                });
                ownModeEffect = own != standing.end() ? own->type : 0;
            }
        }
        if (actor.current == CommandType::Attack)
        {
            const std::uint32_t at = dao::commands::RunningSubAction(id);
            actor.approaching = at == kMeleeApproach || at == kRangedApproach;
            // Said as it changes, at debug: which sub-action a standing
            // attacker is at is what a play test's log is read for.
            static ObjectId lastWho = dao::kInvalidObject;
            static std::uint32_t lastAt = 0;
            if (id != lastWho || at != lastAt)
            {
                lastWho = id;
                lastAt = at;
                spdlog::debug("{:#x}'s attack is at sub-action {}{}", id, at,
                              actor.approaching ? " (its approach)" : "");
            }
        }
        else if (actor.current == CommandType::Deathblow)
        {
            // A death blow that stands (issue 28: twice an ogre's killer
            // stood at one 36 s of the game's time): which sub-action it is
            // at, and its victim's command, said as either changes, are what
            // the next one's log is read for.
            const std::uint32_t at = dao::commands::RunningSubAction(id);
            const std::optional<ObjectId> victim = current.get()->Target();
            std::int32_t victimAt = -1;
            if (victim && api::IsObjectValid(*victim) != 0)
            {
                if (const dao::script::OwnedCommand theirs = api::GetCurrentCommand(*victim))
                {
                    victimAt = api::GetCommandType(*theirs.get());
                }
            }
            static std::uint32_t lastAt = ~0U;
            static std::int32_t lastVictimAt = -2;
            if (at != lastAt || victimAt != lastVictimAt)
            {
                lastAt = at;
                lastVictimAt = victimAt;
                spdlog::debug("{:#x}'s death blow on {:#x} is at sub-action {}; its victim's command is {} ({})", id,
                              victim.value_or(dao::kInvalidObject), at, victimAt,
                              victim && api::IsDead(*victim) != 0 ? "dead" : "alive");
            }
        }
    }
    SayOwnMode(id, actor.ownMode, ownModeEffect);
    // Busy with a command of its own, or with one queued and not yet begun
    // (core: ActorObservation::busy says what the queue's half is for).
    actor.busy = actor.current != CommandType::Invalid || api::GetCommandQueueSize(id) > 0;
    // Out of the world by the game's own word: it has no turn (issue 44).
    // Anyone's, the party's own too.
    actor.inactive = Inactive(id);
    SayQueueNotRunning(id, actor.current, actor.inactive);
    actor.moving = api::IsMoving(id) != 0;
    const dao::Vector position = api::GetPosition(id);
    actor.position = {position.x, position.y, position.z};
    actor.conjureHeld = dao::commands::ConjureHeld(id) && dao::commands::Conjure(id).has_value();
    actor.timersHeld = dao::timers::Waiting(id) > 0;
    // What it has noticed, as of this frame (core: ActorObservation::
    // perception): a hostile that perceives nobody to fight is not told its
    // turn has come, and one that notices in its turn is told then (issue
    // 29). The party's own is not asked about. A few reads of the engine's
    // perception lists: IsPerceivingHostiles, and IsPerceiving of each party
    // member until one answers yes.
    if (std::ranges::find(party_, id) == party_.end())
    {
        actor.perception = Perceived(id, party_);
        // Said as it changes, at debug: when a creature noticed is what a
        // play test's log is read for.
        static ObjectId lastWho = dao::kInvalidObject;
        static bool lastUnaware = false;
        const bool unaware = !actor.perception.inCombat && !actor.perception.ofParty && !actor.perception.ofFoes;
        if (id != lastWho || unaware != lastUnaware)
        {
            lastWho = id;
            lastUnaware = unaware;
            spdlog::debug("{:#x} on its turn: it is {} combat, and perceives {}, {}; the party {} it", id,
                          actor.perception.inCombat ? "in" : "not in",
                          actor.perception.ofParty ? "a party member" : "no party member",
                          actor.perception.ofFoes ? "someone hostile to it" : "nobody hostile to it",
                          actor.perception.byParty ? "perceives" : "does not perceive");
        }
    }
    return actor;
}

namespace
{
// The effects that hold a creature for turns, and that the machine ends
// once their turns are spent (Status, EndConditions).
//
// A list of kinds, where what a hold costs is otherwise read with none
// (issue 30: any effect's command costs the time it runs, core's HoldRuns).
// Why these five keep theirs: each keeps its creature at its command for as
// long as the effect itself stands (a pin's command outlasted its victim's
// turn once effects' time was held, 2026-09-30), and in a fight an effect's
// time is on a clock the mod holds and moves by its creature's own turns.
// So the time such a hold has left is the effect's own, not a length its
// command has to be read off its sub-actions (what DAOLib's count says of
// one while its effect stands has not been looked at; of a knockback's
// fall and rise the engine keeps no time ahead at all); and counted as it
// runs, it would mean the
// world running through every turn the hold takes, a paralysed fighter
// standing out its bar, where today the turn is lost at once and the hold's
// share is taken as the turn begins. Which effect a command waits on is not
// read from the command yet (DAOLib's dao/SubActions.h), so the kinds still
// say which effects' time left that is. The engine's command once one of
// them is ended (the getting up) is charged as any other. What goes when a
// command names its effect: this list for the time, not for what the card
// calls stunned.
constexpr std::array kHolding{k::EFFECT_TYPE_STUN, k::EFFECT_TYPE_PARALYZE, k::EFFECT_TYPE_SLEEP, k::EFFECT_TYPE_ROOT,
                              k::EFFECT_TYPE_KNOCKDOWN};
constexpr std::array<const char *, kHolding.size()> kHoldingNames{"stun", "paralysis", "sleep", "pin", "knockdown"};

// Which of them hold a creature, by name: "pin, stun", or "nothing".
std::string Holding(ObjectId id)
{
    std::string out;
    for (std::size_t i = 0; i < kHolding.size(); ++i)
    {
        if (api::GetHasEffects(id, kHolding[i]) != 0)
        {
            out += out.empty() ? "" : ", ";
            out += kHoldingNames[i];
        }
    }
    return out.empty() ? "nothing" : out;
}
} // namespace

CombatantStatus Bridge::Status(ObjectId id)
{
    CombatantStatus status;
    status.id = id;
    status.exists = api::IsObjectValid(id) != 0;
    if (status.exists)
    {
        status.down = api::IsDead(id) != 0;
        status.health = Health(id);
        status.energy = Energy(id);
        status.mana = Mage(id);
        // Its Haste and slows as they stand now (MovementEffectsOn): what
        // its next turn's meters are made of, and with a turn a bar of time
        // what its turn under way walks at from this look (core:
        // TurnMachine::FollowPace; issue 49).
        status.movement = MovementEffectsOn(id);
        // The conditions that hold a creature for turns: the effects whose
        // command the engine keeps at the front of its queue while they
        // last (EffectStun, effect_stun_h; Pinning Shot's EffectRoot,
        // talent_singletarget.nss; Shield Bash's EffectKnockdown). Daze is
        // only a penalty (effect_daze_h). Seen for stuns and pins
        // 2026-09-30; knockdowns not yet.
        for (const std::int32_t type : kHolding)
        {
            if (api::GetHasEffects(id, type) != 0)
            {
                status.condition = Condition::Disabled;
                break;
            }
        }
        // The seconds it has left, the longest of what holds it: with a
        // turn a bar of time a hold takes its own time of its victim's
        // turns (core: Held::owedMs). DAOLib's dao::effects::On (a893f20),
        // first used here: not yet seen in play. The effect's time, not its
        // command's (kHolding, above, says why): read ahead, so that a turn
        // it outlasts is lost with the world still.
        if (status.condition == Condition::Disabled)
        {
            for (const dao::effects::Standing &effect : dao::effects::On(id))
            {
                if (std::ranges::find(kHolding, static_cast<std::int32_t>(effect.type)) != kHolding.end())
                {
                    status.conditionSeconds = std::max(status.conditionSeconds, effect.remaining);
                }
            }
        }
    }
    return status;
}

void Bridge::Apply(const std::vector<Effect> &effects)
{
    for (const Effect &e : effects)
    {
        spdlog::debug("effect: {} {:#010x} ({}, {})", Name(e.kind), e.target, e.a, e.b);
        switch (e.kind)
        {
        case EffectKind::Pause:
        case EffectKind::Unpause:
            if (auto *app = dao::CClientExoApp::Get())
            {
                // `second` as the pause key passes it (DAOLib's
                // dev/TURN_BASED.md; what it means is not read yet).
                app->SetPaused(e.kind == EffectKind::Pause, true);
            }
            break;
        case EffectKind::ClearCommands:
            api::ClearAllCommands(e.target, 1);
            break;
        case EffectKind::Select:
            api::SetPrimaryControlled(e.target);
            break;
        case EffectKind::PokeAi:
            // One told as a fight ends may never have been in its order, or
            // may have left it alive (core: TurnMachine::LetGo;
            // issue 43): nobody has read whether it still stands, so it is
            // asked here. A dead creature's own script would hear the event
            // before rules_core discards it, and one gone from the area is no
            // object to signal.
            if (Gone(e.target))
            {
                spdlog::debug("{:#010x} is dead or gone: not told to choose", e.target);
                break;
            }
            Poke(e.target);
            break;
        case EffectKind::ReleaseCast:
            if (ReleaseEngineConjure(e.target))
            {
                break; // the engine's own cast, let complete (Gate.cpp): nothing kept to give
            }
            // The cast its creature charged (core: "Casts that charge"),
            // given to it as a script gives a command: it comes back
            // through the gate, which lets it through for nothing. Its
            // conjuring is short now, the turns having been its time.
            if (const std::optional<HeldCast> kept = TakeHeldCast(e.target);
                kept && kept->target != dao::kInvalidObject && NotThere(kept->target))
            {
                // (Dead, gone, or made inactive since it was kept: NotThere.
                // Each line says which.)
                const char *why = WhyNotThere(kept->target);
                // A plain attack whose target fell is turned on the nearest
                // enemy its creature can strike from where it stands, and
                // given at that one: through the gate as the kept one would
                // have come, for nothing, its time paid as the turn began
                // (the user, 2026-10-03, issue 22: "as the game's own
                // auto-attack would", for everyone). Who it may be is the
                // machine's to name (Effect::others) and which the core's to
                // choose; here they are measured and the attack given.
                const ObjectId next = kept->type == CommandType::Attack ? TurnedOn(e.target, e.others) : kNoObject;
                if (next != kNoObject)
                {
                    if (GiveAttack(e.target, next))
                    {
                        spdlog::info(
                            "{:#010x}: the target of its kept attack ({:#x}) {}: turned on {:#x}, the nearest of {} "
                            "enemies standing that it can strike from where it is ({:.1f} m away)",
                            e.target, kept->target, why, next, e.others.size(),
                            api::GetDistanceBetween(e.target, next));
                        break;
                    }
                    spdlog::warn("{:#010x}: could not make the command for its kept attack, turned on {:#x}", e.target,
                                 next);
                }
                // Its target fell while it charged: not cast at nothing. In
                // play the cast was given all the same, struck nothing, and
                // put the talent on cooldown for five turns (the twelfth
                // play test, 2026-10-01). Not given, nothing is spent: the
                // machine says its cast did not come, and the turn goes on.
                // A talent or a spell, always: its target was chosen. An
                // attack, with no enemy in its reach: its creature's turn is
                // handed over whole, and an AI chooses anew.
                spdlog::info(
                    "{:#010x}: the target of its charged cast of ability {} ({:#x}) {}: not cast, nothing spent{}",
                    e.target, kept->ability, kept->target, why,
                    kept->type == CommandType::Attack
                        ? std::format(" (an attack: none of {} enemies standing is in its reach and sight)",
                                      e.others.size())
                        : std::string{});
                // Logged, not shown: "Its target is gone" floated here in
                // grey, and with a turn a bar of time most turns end on a
                // kept attack whose target has often fallen by the next,
                // eight to twelve times a fight in the Korcari Wilds. Nothing
                // is lost by it, so nothing is asked of the player (the user,
                // watching, 2026-10-03: "That can just be a thing we log and
                // not show"; issue 8).
                // Told to the machine with the next observation: with a turn
                // a bar of time, what the turn paid for it is given back.
                keptVoid_.push_back(e.target);
            }
            else if (kept && kept->type == CommandType::Attack)
            {
                // With a turn a bar of time, an attack kept for the turn its
                // time is paid on (core: TurnMachine::PayAction), given as a
                // script gives one.
                if (kept->target == dao::kInvalidObject)
                {
                    spdlog::warn("{:#010x}: its kept attack has no target: not given", e.target);
                }
                else if (const dao::script::OwnedCommand attack = api::CommandAttack(kept->target))
                {
                    api::AddCommand(e.target, *attack.get(), 1);
                    spdlog::debug("{:#010x}: its kept attack on {:#x} is given to it", e.target, kept->target);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the command for its kept attack", e.target);
                }
            }
            else if (kept)
            {
                const dao::Vector place = kept->placed ? dao::Vector{kept->x, kept->y, kept->z} : dao::Vector{};
                if (const dao::script::OwnedCommand cast =
                        api::CommandUseAbility(kept->ability, kept->target, place, kChargedConjureSeconds))
                {
                    api::AddCommand(e.target, *cast.get(), 1);
                    spdlog::debug("{:#010x}: its charged cast of ability {} is given to it", e.target, kept->ability);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the command for its charged cast of ability {}", e.target,
                                 kept->ability);
                }
            }
            else
            {
                spdlog::warn("{:#010x}: no cast kept for it to be given (ability {})", e.target, e.a);
            }
            break;
        case EffectKind::DropCast:
            DropHeldCast(e.target);
            (void)DropEngineConjure(e.target);
            break;
        case EffectKind::FaceTarget:
            if (!e.others.empty())
            {
                faced_.push_back(FaceTarget(e.target, e.others.front(), static_cast<CommandType>(e.a)));
            }
            break;
        case EffectKind::Approach:
            // With a turn a bar of time, an attack that did not fit is kept
            // for its creature's next turn, and the creature walks to where
            // it can strike now (core: TurnMachine::PayAction): a move to
            // its target, given as a script gives one, to end inside what
            // the engine's own approach for the attack asks (DAOLib's
            // dao::attack::ApproachOf: for a shot the distance its approach
            // stops at, for a blow the centres' furthest, its range).
            if (const std::optional<HeldCast> kept = PeekHeldCast(e.target);
                kept && kept->target != dao::kInvalidObject && api::IsObjectValid(kept->target) != 0)
            {
                constexpr float kInside = 0.3f; // meters inside the reach, so that the attack need not step
                constexpr float kBeside = 1.5f; // where the engine has no approach to say: beside its target
                const std::optional<dao::attack::Approach> approach = dao::attack::ApproachOf(e.target, kept->target);
                // A blow's: how far from its target the engine's own place
                // for the attacker on the target's ring is (the approach's
                // goal), not a distance of the mod's. A ring with no place
                // for it at all has the target itself for a goal (DAOLib
                // 151e624; it had no approach to say before): beside its
                // target then too.
                const dao::Vector at = api::GetPosition(kept->target);
                const float toPlace =
                    approach && !approach->ranged ? std::hypot(approach->goal.x - at.x, approach->goal.y - at.y) : 0.0f;
                const float reach = !approach          ? kBeside
                                    : approach->ranged ? approach->stopWithin
                                    : toPlace > 0.0f   ? toPlace
                                                       : kBeside;
                const float within = std::max(0.5f, reach - kInside);
                if (const dao::script::OwnedCommand walk = api::CommandMoveToObject(kept->target, 1, within))
                {
                    api::AddCommand(e.target, *walk.get(), 0);
                    spdlog::debug("{:#010x}: it walks to within {:.1f} m of {:#x}, where its kept action lands",
                                  e.target, within, kept->target);
                }
                else
                {
                    spdlog::warn("{:#010x}: could not make the walk to its kept action's target", e.target);
                }
            }
            else
            {
                spdlog::debug("{:#010x}: no kept action with a target to walk to", e.target);
            }
            break;
        case EffectKind::Explain:
            Explain(e.target);
            break;
        case EffectKind::HoldTime:
            // The clock temporary effects wear off on and regeneration ticks
            // on (DAOLib's dao/Clock.h, a42be67): held through a fight,
            // moved on a round's worth at a time. And with it the scripts'
            // own timers of a second or more, whatever their kind (issue 26,
            // Settings::scriptTimersFrom): named before the hold, which takes
            // what is already in the engine's queue by the same rule.
            if (e.a != 0)
            {
                dao::timers::HoldFrom(settings_.scriptTimersFrom);
                dao::timers::Trace(settings_.scriptTimersTrace);
            }
            dao::clock::Hold(e.a != 0);
            if (e.a == 0)
            {
                camera_.Release(); // the fight is over: the camera as the game has it
            }
            if (e.a != 0)
            {
                ReadInstalledRules(); // what the installed scripts count (Gate.cpp)
            }
            break;
        case EffectKind::OwnClock:
            // A combatant's effects, regeneration and the scripts' timers on
            // it on a clock of its own (DAOLib's dao/Clock.h, 04b3be3), moved
            // by the targeted AgeTime below as its turn is spent; what is not
            // in the fight stays on the shared one. Given back as the hold is
            // let go.
            dao::clock::OwnClock(e.target, e.a != 0);
            break;
        case EffectKind::GameSpeed:
            // Game logic and animation together (DAOLib's dao/Clock.h,
            // 4e0032e); the Flash UI keeps its own clock.
            // Whether the world's timer took it and the frame's hook is placed
            // (DAOLib ed55ab8): an archer's shot took as long at 2 as at 1 in
            // the second play test, the mod's own waits aside.
            speedTook_ = dao::clock::SetSpeed(static_cast<float>(e.b) / 1000.0f);
            camera_.SetSpeed(static_cast<float>(e.b) / 1000.0f);
            if (!*speedTook_)
            {
                spdlog::warn("the game's speed {:.2f} did not take", static_cast<float>(e.b) / 1000.0f);
            }
            else
            {
                spdlog::debug("the game's speed is {:.2f}", static_cast<float>(e.b) / 1000.0f);
            }
            break;
        case EffectKind::AgeTime:
            if (e.target != kNoObject)
            {
                // One creature's own clock (core: AgeOwnClock), in steps as
                // small as a frame's walk: DAOLib keeps the fractions.
                dao::clock::Advance(e.target, static_cast<float>(e.b) / 1000.0f);
                break;
            }
            // With the effects and regeneration, since DAOLib 48fe011, the
            // scripts' own timers that pace them (dao/Timers.h): a damage
            // over time's ticks and a lasting area's pulses wait while the
            // clock is held and come due as it is moved, where before they
            // ran on in real time (the user, playing, 2026-10-02: "there's a
            // environmental poison trap that keeps hitting the party even
            // during turn-based combat"). How many wait is said, to read a
            // fight's ticks against its rounds.
            dao::clock::Advance(static_cast<float>(e.b) / 1000.0f);
            if (const std::size_t waiting = dao::timers::Waiting(); waiting > 0)
            {
                spdlog::debug("the clock moved {:.2f} s: {} of the scripts' timers wait on it",
                              static_cast<float>(e.b) / 1000.0f, waiting);
            }
            break;
        case EffectKind::Cooldown:
            // Held, released, or back to seconds (core: "Cooldowns in turns").
            // With what the sweep is drawn against (c), through DAOLib
            // (dao/Abilities.h, b51d676): the scripts' SetCooldown sets what
            // is left and what it was set to as one, so a hold showed a full
            // sweep that never moved, where the user wants the turns shown.
            // DAOLib's refuses a creature with no record for the ability
            // (an item's, by its tag): the scripts' then, as before.
            if (e.c <= 0 ||
                !dao::abilities::SetCooldown(e.target, e.a, static_cast<float>(e.b), static_cast<float>(e.c)))
            {
                if (e.c > 0)
                {
                    spdlog::debug(
                        "ability {} of {:#010x}: no cooldown record to set {} of {} s on; set as the scripts do", e.a,
                        e.target, e.b, e.c);
                }
                api::SetCooldown(e.target, e.a, static_cast<float>(e.b));
            }
            break;
        case EffectKind::EndConditions:
            // Their turns are spent (core: "Conditions in turns"): the
            // engine's own timers are not waited for.
            {
                // Said before and after: the desktop play test (2026-09-30) saw
                // a pin the machine ended hold its creature still.
                const std::string before = Holding(e.target);
                for (const std::int32_t type : kHolding)
                {
                    api::RemoveEffectsByParameters(e.target, type);
                }
                const std::string after = Holding(e.target);
                if (after != "nothing")
                {
                    spdlog::warn("{:#010x}: ended {}, and {} holds it still", e.target, before, after);
                }
                else
                {
                    spdlog::debug("{:#010x}: ended {}", e.target, before);
                }
            }
            break;
        case EffectKind::Floaty:
            api::DisplayFloatyMessage(e.target, e.text, k::FLOATY_MESSAGE, static_cast<std::int32_t>(e.colour), 2.0f);
            break;
        case EffectKind::Signal:
            Signal(e);
            break;
        case EffectKind::CenterCamera:
            camera_.TurnTo(e.target, PlayersOwn());
            break;
        }
    }
}

void Bridge::Poke(ObjectId id) const
{
    // Processed at once (bProcessImmediate): the creature's scripts run
    // now, inside this call, and whatever its AI queues comes back through
    // the gate while it is the creature's turn (dev/DESIGN.md, "The AI's
    // turn").
    if (settings_.aiPoke == AiPoke::Kickstart)
    {
        if (const dao::script::OwnedEvent event = api::Event(kKickstartAi))
        {
            api::SignalEvent(id, *event.get(), 1);
        }
        return;
    }
    // What the engine sends when a command ends: the creature's own script
    // sees it first, then rules_core, which runs its AI (custom AI too).
    // A wait that succeeded, sub-command -1 (rules_core reads ints 0-2).
    dao::script::OwnedEvent event = api::Event(k::EVENT_TYPE_COMMAND_COMPLETE);
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 0, k::COMMAND_TYPE_WAIT);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 1, k::COMMAND_SUCCESSFUL);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 2, -1);
    }
    if (event)
    {
        api::SignalEvent(id, *event.get(), 1);
    }
    else
    {
        spdlog::warn("could not make the event to tell {:#010x} its turn has come", id);
    }
}

// What an AI saw when it chose nothing on its turn: the conditions under
// which rules_core and AI_DetermineCombatRound return without a command
// (rules_core.nss, EVENT_TYPE_COMMAND_COMPLETE; ai_main_h_2.nss), and the
// effects that disable a creature. Read in play, it says why.
void Bridge::Explain(ObjectId id)
{
    std::int32_t current = k::COMMAND_TYPE_INVALID;
    if (const dao::script::OwnedCommand command = api::GetCurrentCommand(id))
    {
        current = api::GetCommandType(*command.get());
    }
    // And whether it perceives anyone to choose (issue 29): one not in
    // combat that perceives no party member and no hostile is not told its
    // turn has come at all, so this line with "combat state 0" and
    // "perceiving hostiles 0" is worth a look.
    spdlog::info("{} ({:#010x}) chose nothing: combat state {}, disabled flags {:#x}, stun {} paralyze {} "
                 "knockdown {} daze {} sleep {}, current command {}, {} queued, light AI {}, custom AI {}, "
                 "perceiving hostiles {}",
                 Utf8(Remember(id).name), id, api::GetCombatState(id), api::GetEffectsFlags(id),
                 api::GetHasEffects(id, k::EFFECT_TYPE_STUN), api::GetHasEffects(id, k::EFFECT_TYPE_PARALYZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_KNOCKDOWN), api::GetHasEffects(id, k::EFFECT_TYPE_DAZE),
                 api::GetHasEffects(id, k::EFFECT_TYPE_SLEEP), current, api::GetCommandQueueSize(id),
                 api::GetLocalInt(id, L"AI_LIGHT_ACTIVE"), api::GetLocalInt(id, L"AI_CUSTOM_AI_ACTIVE"),
                 api::IsPerceivingHostiles(id));
}

void Bridge::Signal(const Effect &effect)
{
    dao::script::OwnedEvent event = api::Event(effect.event);
    if (event && effect.target != kNoObject)
    {
        event = api::SetEventObject(*event.get(), 0, effect.target);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 0, effect.a);
    }
    if (event)
    {
        event = api::SetEventInteger(*event.get(), 1, effect.b);
    }
    if (event)
    {
        api::SignalEvent(api::GetModule(), *event.get(), 0);
    }
}

bool Bridge::PartyInCombat()
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady())
    {
        return false;
    }
    const auto mode = ModeOf(*app);
    if (mode == Mode::Combat)
    {
        return true;
    }
    if (OutOfWorld(mode))
    {
        return false;
    }
    // module_core.nss sets the party's combat state on every change of mode
    // but a menu's (SetCombatStateParty), so it still says combat under one.
    // Any living member's (core: PartyIsInCombat), not the hero's: hers is 0
    // once she has fallen, with the rest still fighting (issue 42: asked by
    // script of the fallen hero in a fight that went on, GetCombatState 0),
    // and a menu opened then read as no fight, the toggle free to change.
    const ObjectId hero = api::GetHero();
    return api::IsObjectValid(hero) != 0 && PartyIsInCombat(ReadParty(hero));
}

void Bridge::Say(const std::wstring &text, std::uint32_t colour)
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady() || OutOfWorld(ModeOf(*app)))
    {
        return;
    }
    // Over the one the player acts as, where the player is looking
    // (PlayersOwn). Until 2026-10-04 it was said over the hero: over her
    // body when she had fallen, and wherever she stood while the player had
    // gone on with another (issue 42). With nobody living, over her all the
    // same.
    const ObjectId over = PlayersOwn();
    if (over == kNoObject)
    {
        return;
    }
    api::DisplayFloatyMessage(over, text, k::FLOATY_MESSAGE, static_cast<std::int32_t>(colour), 2.0f);
}

void Bridge::LookAt(ObjectId id)
{
    camera_.TurnTo(id, PlayersOwn());
}

std::vector<Follower> PartyNow()
{
    std::vector<Follower> party;
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || app->GameModes() == nullptr || !dao::script::CommandsReady() || OutOfWorld(ModeOf(*app)))
    {
        return party;
    }
    // The hero is the player's own character, the scripts' GetHero, as the
    // scan has it (Combatant::hero): not the creature the player controls
    // just now (GetMainControlled) nor the party's leader (GetPartyLeader),
    // either of which may be a companion (ReadParty has what was read of the
    // three).
    const ObjectId hero = api::GetHero();
    if (api::IsObjectValid(hero) == 0)
    {
        return party;
    }
    for (const PartyMember &member : ReadParty(hero))
    {
        party.push_back(Follower{member.id, api::GetName(member.id), api::GetTag(member.id), member.id == hero});
    }
    return party;
}

std::vector<Follower> FollowersNow()
{
    std::vector<Follower> all = PartyNow();
    if (all.empty())
    {
        return all;
    }
    // The party's pool, the scripts' GetPartyPoolList: everyone recruited,
    // in the party or left at the camp, as the game's own scripts walk them
    // (the climax's area scripts, cli300ar_redcliffe_castle and
    // cli400ar_city_gates, read in the toolset's sources, 2026-10-05). The
    // command had not been called from here before issue 46's box, and what
    // it answers in play is still to see.
    const dao::script::Array pool = api::GetPartyPoolList();
    for (std::size_t i = 0; i < pool.Size(); ++i)
    {
        const ObjectId id = pool.Object(i);
        if (api::IsObjectValid(id) != 0 && std::ranges::find(all, id, &Follower::id) == all.end())
        {
            all.push_back(Follower{id, api::GetName(id), api::GetTag(id), false});
        }
    }
    return all;
}

} // namespace tb::game
