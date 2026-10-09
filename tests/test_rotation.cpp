// Who is not there is not in the fight (issue 66): the rotation is who the
// one rule says is in it, at every look, from what the engine says of each
// now (core/Rules.h, InTheRotation). Two of its answers are this issue's: one
// the game has made inactive is out of the world, and one of the fight's
// party that the game has put out of the party has no place in it, unless
// the rules for anyone not of the party have it in the fight.
//
// The fight these are from is the Landsmeet's (2026-10-08). A brawl of
// twenty-five was stopped by a conversation, enemies still standing, and the
// game came back from it in combat with a duel of one against one: everyone
// of the party but one put out of it, to stand by as onlookers; the brawl's
// soldiers made inactive; and a Loghain of the duel's own set hostile. The
// mod took the fight up again with the brawl's whole order, as a conversation
// in the middle of a fight has it do: fifteen cards, ten of them of
// creatures out of the world whose turns were passed over every round ("it
// perceives a party member, someone hostile to it", each read, by lists the
// engine does not clear), three of them drawn as the party's with no
// portrait, the game's party no longer having them, and a turn that waited
// on the player's order for one of those three.
//
// Scripted cases in the stand-in for the game (World.h). What is promised:
//
//   - as the fight is taken up again after a conversation, everyone in it is
//     looked at before any turn goes on: the order is the two duellists from
//     that look, the turn that stood at one put out of the party has ended,
//     and no turn is begun for anyone else;
//   - each that leaves is said once, with why, and has no card;
//   - one put out of the party stays out whatever its perception lists still
//     hold, and is in the fight, on the side the game gives it, only where
//     the rules for anyone not of the party find it fighting;
//   - one put back into the party is in the fight again, as the party's: in
//     the same round with the place it had, in a later one as one that joins;
//   - one that leaves in the middle of its own turn has that turn end there;
//   - one that left is on the shared clock, which the rounds move, and on its
//     own again when it is back: what the game has waiting on it comes due.
#include "World.h"

#include "core/Events.h"
#include "core/Rules.h"
#include "core/TurnMachine.h"
#include "core/TurnOrder.h"
#include "core/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

inline constexpr ObjectId kLyna = 0x100; // the player's own character
inline constexpr ObjectId kAlistair = 0x101;
inline constexpr ObjectId kWynne = 0x102;
inline constexpr ObjectId kGuard = 0x200;
inline constexpr ObjectId kMage = 0x201;
inline constexpr ObjectId kLoghainsGuard = 0x202;
inline constexpr ObjectId kLoghain = 0x203; // the duel's own: not in the brawl
inline constexpr ObjectId kArcher = 0x300;  // fights beside the party

// The brawl, in the order its turns come under Fixed(): Lyna, the archer,
// the guard, Wynne, the mage, Alistair, Loghain's guard. The duel's Loghain
// stands by, not yet in combat, where no scan finds him.
std::vector<Creature> Brawl()
{
    std::vector<Creature> fight{
        Make(kLyna, Side::Party, true, L"Lyna", 20.0f),
        Make(kArcher, Side::Ally, false, L"Waking Sea Archer", 19.0f),
        Make(kGuard, Side::Enemy, false, L"Royal Guard", 18.0f),
        Make(kWynne, Side::Party, false, L"Wynne", 17.0f),
        Make(kMage, Side::Enemy, false, L"Mage", 16.0f),
        Make(kAlistair, Side::Party, false, L"Alistair", 15.0f),
        Make(kLoghainsGuard, Side::Enemy, false, L"Loghain's Guard", 14.0f),
        Make(kLoghain, Side::Enemy, false, L"Loghain", 12.0f),
    };
    fight.back().fighting = false;
    fight.back().perception = Unnoticing();
    return fight;
}

// An onlooker as the game had the three in the Landsmeet: combat state 0,
// perceiving no hostile, and the party's lists and its own still naming each
// other.
Perception Onlooking()
{
    Perception perception;
    perception.ofFoes = false;
    perception.inCombat = false;
    return perception;
}

// One of the party put out of it, to stand by: no longer the party's, not
// hostile to it, and in no fight.
void PutOut(World &w, ObjectId id, const Perception &perception)
{
    Creature &c = w.Get(id);
    c.side = Side::Ally;
    c.fighting = false;
    c.perception = perception;
}

// And put back into it.
void PutBack(World &w, ObjectId id)
{
    Creature &c = w.Get(id);
    c.side = Side::Party;
    c.fighting = true;
    c.perception = Perception{};
}

// The game stages the duel: the brawl's own out of the world, their
// perception lists what they were; all of the party but Alistair out of it;
// and the duel's Loghain hostile and in combat.
void StageTheDuel(World &w, const Perception &onlookers)
{
    for (const ObjectId id : {kArcher, kGuard, kMage, kLoghainsGuard})
    {
        w.SetActive(id, false);
    }
    PutOut(w, kLyna, onlookers);
    PutOut(w, kWynne, onlookers);
    Creature &duellist = w.Get(kLoghain);
    duellist.fighting = true;
    duellist.perception = Perception{};
}

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

int Effects(const World &w, EffectKind kind, ObjectId target)
{
    return static_cast<int>(
        std::ranges::count_if(w.applied, [&](const Effect &e) { return e.kind == kind && e.target == target; }));
}

bool InATurn(const World &w)
{
    const Phase phase = w.machine.GetPhase();
    return phase == Phase::AwaitingOrder || phase == Phase::Dispatching || phase == Phase::AiThinking ||
           phase == Phase::Executing || phase == Phase::Settling;
}

// The turns go by, the player's ended as they come, until it is `id`'s and
// being played; the frames it took, or -1 if it never came.
int UntilTurnOf(World &w, ObjectId id, int most = 2000)
{
    for (int frames = 0; frames < most; ++frames)
    {
        if (w.Current() == id && InATurn(w))
        {
            return frames;
        }
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    return -1;
}

// Everyone but the party passes when told, and each telling is counted.
struct Tellings
{
    explicit Tellings(World &w)
    {
        w.onPoke = [this, &w](ObjectId id) {
            who.push_back(id);
            (void)w.Wait(id, 0.0f);
        };
    }
    [[nodiscard]] int Of(ObjectId id) const
    {
        return static_cast<int>(std::ranges::count(who, id));
    }
    std::vector<ObjectId> who;
};

struct Heard
{
    explicit Heard(World &w)
    {
        w.machine.SetEventSink([this](const TurnEvent &e) { events.push_back(e); });
    }
    [[nodiscard]] int Of(EventKind kind, ObjectId who) const
    {
        return static_cast<int>(
            std::ranges::count_if(events, [&](const TurnEvent &e) { return e.kind == kind && e.who == who; }));
    }
    // Whose events of a kind were heard, each once.
    [[nodiscard]] std::set<ObjectId> Whose(EventKind kind) const
    {
        std::set<ObjectId> who;
        for (const TurnEvent &e : events)
        {
            if (e.kind == kind)
            {
                who.insert(e.who);
            }
        }
        return who;
    }
    [[nodiscard]] std::vector<std::string> EndsOf(ObjectId who) const
    {
        std::vector<std::string> why;
        for (const TurnEvent &e : events)
        {
            if (e.kind == EventKind::TurnEnded && e.who == who)
            {
                why.push_back(e.detail);
            }
        }
        return why;
    }
    std::vector<TurnEvent> events;
};

// Who stands in the order, and who has a card on the bar.
std::set<ObjectId> Standing(const World &w)
{
    std::set<ObjectId> standing;
    for (const Combatant &c : w.machine.Order().All())
    {
        if (!c.down)
        {
            standing.insert(c.id);
        }
    }
    return standing;
}

std::set<ObjectId> Cards(const World &w)
{
    std::set<ObjectId> cards;
    for (const CardView &card : w.machine.View().upcoming)
    {
        cards.insert(card.id);
    }
    return cards;
}

const CardView *CardOf(const TurnView &view, ObjectId id)
{
    const auto found = std::ranges::find(view.upcoming, id, &CardView::id);
    return found != view.upcoming.end() ? &*found : nullptr;
}

// The brawl, played to Wynne's turn, which waits on the player's order; then
// the conversation that stops it.
void BrawlToTheConversation(World &w)
{
    w.StartFight();
    REQUIRE(UntilTurnOf(w, kWynne) >= 0);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    w.mode = Mode::Dialog;
    w.RunFor(50);
    REQUIRE(w.machine.GetPhase() == Phase::Suspended);
}

// A round or more of the duel, Alistair's turns ended as they come.
void PlayOn(World &w, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
}

const std::set<ObjectId> kDuellists{kAlistair, kLoghain};

} // namespace

TEST_CASE("a duel staged in a conversation in the middle of a brawl: the order is the two duellists from the fight's "
          "first look after it",
          "[machine][rotation]")
{
    World w{Fixed(), Brawl()};
    Tellings told{w};
    Heard heard{w};
    BrawlToTheConversation(w);
    REQUIRE(Standing(w) == std::set<ObjectId>{kLyna, kArcher, kGuard, kWynne, kMage, kAlistair, kLoghainsGuard});
    Perception onlookers = Onlooking();
    SECTION("the onlookers as the game had them: out of combat, perceiving no hostile")
    {
    }
    SECTION("the onlookers' lists stale, in combat and perceiving hostiles by the engine's word: what one perceives "
            "brings it into no fight")
    {
        onlookers = Perception{};
    }
    StageTheDuel(w, onlookers);
    w.applied.clear();
    heard.events.clear();
    told.who.clear();
    const std::int64_t wynnesClock = w.agedOwn[kWynne];
    w.mode = Mode::Combat;
    w.RunFor(100);

    // The first look: everyone of the brawl is out, and the duel's own in.
    CHECK(Standing(w) == kDuellists);
    CHECK(Cards(w) == kDuellists);
    for (const char *soldier : {"Waking Sea Archer", "Royal Guard", "Mage", "Loghain's Guard"})
    {
        CHECK(Lines(w, std::string{soldier} + " leaves the fight: the game has taken it out of the world") == 1);
    }
    CHECK(Lines(w, "Lyna leaves the fight: the game has put it out of the party") == 1);
    CHECK(Lines(w, "Wynne leaves the fight: the game has put it out of the party") == 1);
    CHECK(Lines(w, "Loghain joins the fight (enemy") == 1);
    // The turn that stood at Wynne has ended, and was not gone on with: it
    // is Alistair's, the one of the party the game still has in it.
    CHECK(heard.EndsOf(kWynne) == std::vector<std::string>{"down"});
    CHECK(Lines(w, "Wynne's turn goes on") == 0);
    CHECK(heard.Of(EventKind::AwaitingOrder, kWynne) == 0);
    CHECK(Effects(w, EffectKind::Select, kWynne) == 0);
    // Nor did its end move her own clock the rest of a turn, as a turn's end
    // does: she is on the shared one, with everyone that left.
    CHECK(w.agedOwn[kWynne] == wynnesClock);
    for (const ObjectId id : {kLyna, kWynne, kArcher, kGuard, kMage, kLoghainsGuard})
    {
        CHECK_FALSE(w.onOwnClock.contains(id));
    }
    CHECK(w.onOwnClock.contains(kAlistair));
    CHECK(w.onOwnClock.contains(kLoghain));
    REQUIRE(w.Current() == kAlistair);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    // Nobody left on the bar is drawn as the party's but the one who is.
    const TurnView view = w.machine.View();
    REQUIRE(CardOf(view, kAlistair) != nullptr);
    REQUIRE(CardOf(view, kLoghain) != nullptr);
    CHECK(CardOf(view, kAlistair)->side == Side::Party);
    CHECK(CardOf(view, kAlistair)->yours);
    CHECK(CardOf(view, kLoghain)->side == Side::Enemy);

    // Three rounds of the duel: no turn is begun, lost or passed for anyone
    // else, nobody else is told to choose or handed to the player, and each
    // that left was said once.
    PlayOn(w, 600);
    CHECK(w.machine.Order().Round() >= 4);
    CHECK(heard.Whose(EventKind::TurnStarted) == kDuellists);
    CHECK(heard.Whose(EventKind::TurnLost).empty());
    CHECK(heard.Whose(EventKind::AwaitingOrder) == std::set<ObjectId>{kAlistair});
    for (const TurnEvent &e : heard.events)
    {
        if (e.kind == EventKind::TurnEnded && e.who != kWynne)
        {
            CHECK(kDuellists.contains(e.who));
        }
    }
    CHECK(std::set<ObjectId>(told.who.begin(), told.who.end()) == std::set<ObjectId>{kLoghain});
    for (const ObjectId id : {kLyna, kWynne, kArcher, kGuard, kMage, kLoghainsGuard})
    {
        CHECK(Effects(w, EffectKind::Select, id) == 0);
        CHECK(Effects(w, EffectKind::PokeAi, id) == 0);
        CHECK(Effects(w, EffectKind::CenterCamera, id) == 0);
        CHECK(heard.Of(EventKind::Gone, id) == 1);
    }
    CHECK(Lines(w, " leaves the fight: ") == 6);
    CHECK(Lines(w, "is passed") == 0);
    // Nobody in the fight changed sides: those put out of the party left it.
    CHECK(Lines(w, "now by the game's own word") == 0);
    CHECK(Standing(w) == kDuellists);
    CHECK(Cards(w) == kDuellists);
    // With the round's turn those that left have no place in the order.
    CHECK(w.machine.Order().All().size() == 2);

    // The duel won, the fight is over: none of the brawl's stands in it. As
    // it lets the world go, those that left it alive and are not the party's
    // are told to choose, as any that left are (the onlookers among them,
    // who are the game's now); the one of the party is not.
    w.applied.clear();
    w.Get(kLoghain).down = true;
    PlayOn(w, 200);
    CHECK_FALSE(w.machine.Active());
    CHECK(Lines(w, "the turn-based fight is over") == 1);
    CHECK(Effects(w, EffectKind::PokeAi, kAlistair) == 0);
    CHECK(Effects(w, EffectKind::PokeAi, kGuard) == 1);
}

TEST_CASE("one put back into the party is in the fight again, as the party's", "[machine][rotation]")
{
    World w{Fixed(), Brawl()};
    Tellings told{w};
    Heard heard{w};
    BrawlToTheConversation(w);
    StageTheDuel(w, Onlooking());
    w.mode = Mode::Combat;
    w.RunFor(100);
    REQUIRE(Standing(w) == kDuellists);
    REQUIRE(w.Current() == kAlistair);
    w.applied.clear();
    heard.events.clear();

    SECTION("in the round it left in: it has the place it had")
    {
        PutBack(w, kLyna);
        PutBack(w, kWynne);
        REQUIRE(UntilTurnOf(w, kLyna) >= 0);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.machine.Order().Round() == 2);
        CHECK(Lines(w, "Lyna is fighting again") == 1);
        CHECK(Lines(w, "Wynne is fighting again") == 1);
        CHECK(Lines(w, "Lyna is of the party now by the game's own word, where it was an ally") == 1);
        CHECK(heard.Of(EventKind::UpAgain, kLyna) == 1);
        CHECK(heard.Of(EventKind::Joined, kLyna) == 0);
        CHECK(w.onOwnClock.contains(kLyna));
    }
    SECTION("in a later round: it joins, as anyone does that comes into a fight")
    {
        REQUIRE(UntilTurnOf(w, kLoghain) >= 0);
        REQUIRE(UntilTurnOf(w, kAlistair) >= 0);
        REQUIRE(w.machine.Order().Round() == 2);
        REQUIRE(w.machine.Order().All().size() == 2);
        PutBack(w, kLyna);
        PutBack(w, kWynne);
        REQUIRE(UntilTurnOf(w, kLyna) >= 0);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Lines(w, "Lyna joins the fight (party") == 1);
        CHECK(Lines(w, "Wynne joins the fight (party") == 1);
        CHECK(heard.Of(EventKind::Joined, kLyna) == 1);
    }
    // Either way they are the party's own again: a card each, drawn as the
    // party's, and a turn that waits on the player.
    CHECK(Standing(w) == std::set<ObjectId>{kLyna, kWynne, kAlistair, kLoghain});
    const TurnView view = w.machine.View();
    for (const ObjectId id : {kLyna, kWynne})
    {
        REQUIRE(CardOf(view, id) != nullptr);
        CHECK(CardOf(view, id)->side == Side::Party);
        CHECK_FALSE(CardOf(view, id)->down);
    }
    CHECK(Effects(w, EffectKind::Select, kLyna) == 1);
    REQUIRE(UntilTurnOf(w, kWynne) >= 0);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(Effects(w, EffectKind::Select, kWynne) == 1);
}

TEST_CASE("one put out of the party is in the fight on the side the game gives it, where the rules for anyone find it "
          "fighting",
          "[machine][rotation]")
{
    World w{Fixed(), Brawl()};
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kLyna);
    // Wynne is turned on the party (a spell's doing, a plot's): hostile, in
    // combat, fighting it.
    Creature &wynne = w.Get(kWynne);
    wynne.side = Side::Enemy;
    SECTION("found fighting the party, it is an enemy of the fight: its turn is its AI's")
    {
        REQUIRE(UntilTurnOf(w, kWynne) >= 0);
        CHECK(w.machine.GetPhase() != Phase::AwaitingOrder);
        CHECK(told.Of(kWynne) == 1);
        CHECK(Effects(w, EffectKind::Select, kWynne) == 0);
        CHECK(heard.Of(EventKind::AwaitingOrder, kWynne) == 0);
        CHECK(Lines(w, "Wynne is an enemy now by the game's own word, where it was of the party") == 1);
        CHECK(Lines(w, "Wynne leaves the fight") == 0);
        const TurnView view = w.machine.View();
        REQUIRE(CardOf(view, kWynne) != nullptr);
        CHECK(CardOf(view, kWynne)->side == Side::Enemy);
        CHECK_FALSE(CardOf(view, kWynne)->yours);
        // And the party's again, it is the player's to order.
        PutBack(w, kWynne);
        REQUIRE(UntilTurnOf(w, kAlistair) >= 0);
        REQUIRE(UntilTurnOf(w, kWynne) >= 0);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(Lines(w, "Wynne is of the party now by the game's own word, where it was an enemy") == 1);
    }
    SECTION("fighting near, and not the party: it has left, as one put out of the party")
    {
        wynne.engaged = false;
        REQUIRE(UntilTurnOf(w, kMage) >= 0);
        CHECK(Lines(w, "Wynne leaves the fight: the game has put it out of the party") == 1);
        CHECK(told.Of(kWynne) == 0);
        CHECK_FALSE(Cards(w).contains(kWynne));
        // Fighting the party after all, it is back, an enemy.
        wynne.engaged = true;
        REQUIRE(UntilTurnOf(w, kAlistair) >= 0);
        CHECK(Lines(w, "Wynne is fighting again") == 1);
        REQUIRE(w.machine.Order().Find(kWynne) != nullptr);
        CHECK(w.machine.Order().Find(kWynne)->side == Side::Enemy);
    }
}

TEST_CASE("one that leaves the fight in the middle of its own turn has that turn end there", "[machine][rotation]")
{
    // No conversation between: the game puts the hero out of the party while
    // her turn waits on the player. A scan in the middle of a turn comes when
    // a creature in combat that is not in the order tries a command, and the
    // one here is asked for so.
    std::vector<Creature> fight = StandardFight();
    fight.push_back(Make(0x400, Side::Enemy, false, L"Stranger", 5.0f));
    fight.back().unfound = true;
    World w{Fixed(), fight};
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    PutOut(w, kHero, Onlooking());
    w.RunFor(100);
    // With no scan nothing has been read of her, and the turn stands.
    REQUIRE(w.Current() == kHero);
    CHECK(w.Order(0x400, CommandType::Attack, Source::Script) == Verdict::Block);
    w.RunFor(50);
    CHECK(w.Current() != kHero);
    CHECK(heard.EndsOf(kHero) == std::vector<std::string>{"gone"});
    CHECK(Lines(w, "Hero leaves the fight: the game has put it out of the party") == 1);
    CHECK(Lines(w, "Hero's turn ends: gone") == 1);
    CHECK_FALSE(Cards(w).contains(kHero));
}

TEST_CASE("one of the party the game has taken out of the world has no card, fallen or standing, and is back when it "
          "is in the world again",
          "[machine][rotation]")
{
    World w;
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    bool fallen = false;
    SECTION("standing")
    {
    }
    SECTION("fallen: no greyed card either, where a fallen member in the world has one")
    {
        fallen = true;
    }
    w.Get(kCompanion).down = fallen;
    w.SetActive(kCompanion, false);
    REQUIRE(w.machine.OnEndTurn());
    REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
    CHECK(Lines(w, "Alistair leaves the fight: the game has taken it out of the world") == 1);
    CHECK_FALSE(Cards(w).contains(kCompanion));
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    CHECK(heard.Of(EventKind::AwaitingOrder, kCompanion) == 0);
    CHECK(heard.Of(EventKind::TurnStarted, kCompanion) == 0);
    CHECK(Effects(w, EffectKind::Select, kCompanion) == 0);
    // The round has turned, and it has no place in the order: it is still
    // the party's, by the game's word, and not in its fight.
    CHECK(w.machine.Order().Find(kCompanion) == nullptr);
    CHECK(Lines(w, "Alistair leaves the fight") == 1);
    // In the world again, and standing: it joins, and its turn waits on the
    // player.
    w.Get(kCompanion).down = false;
    w.SetActive(kCompanion, true);
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(Lines(w, "Alistair joins the fight (party") == 1);
    CHECK(Cards(w).contains(kCompanion));
}

TEST_CASE("one that left the fight is on the shared clock, which the rounds move, and on its own again when it is "
          "back",
          "[machine][rotation]")
{
    // What the passed-over turns of an inactive creature carried until issue
    // 66: its own clock moved a turn's seconds with each, and the game's
    // timers on a creature run on that clock through a fight. A tentacle of
    // the Broodmother's comes up by one (a surface event delayed 2.5 s as it
    // burrows). Out of the order it has no turns, so its clock is given back
    // to the shared one, which every round moves a turn's seconds: what it
    // has waiting comes due with the round, as it did with its own turn.
    World w;
    Tellings told{w};
    w.StartFight();
    REQUIRE(w.onOwnClock.contains(kGenlock));
    REQUIRE(w.Current() == kHero);
    // The genlock goes under ground on the hero's turn, in round 1.
    w.SetActive(kGenlock, false);
    const std::int64_t shared = w.agedMs;
    const std::int64_t own = w.agedOwn[kGenlock];
    REQUIRE(w.machine.OnEndTurn());
    REQUIRE(UntilTurnOf(w, kCompanion) >= 0);
    CHECK(Lines(w, "Genlock leaves the fight: the game has taken it out of the world") == 1);
    CHECK_FALSE(w.onOwnClock.contains(kGenlock));
    CHECK(w.onOwnClock.contains(kHurlock));
    // Round 2 begins: the shared clock has moved a turn's four seconds, more
    // than the 2.5 a tentacle waits, and nothing has moved a clock of its own.
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    REQUIRE(w.machine.Order().Round() == 2);
    CHECK(w.agedMs - shared == 4000);
    CHECK(w.agedOwn[kGenlock] == own);
    // Its timer come due, the game makes it active and has it attack (the
    // tentacle's surface event). Not in the order, its attack waits and asks
    // for a look; found fighting, it joins, and has its turn in this round,
    // the round after it went under: as it did when its own passed-over turn
    // brought it up in round 1, to be told its turn had come in round 2.
    w.SetActive(kGenlock, true);
    CHECK(w.Order(kGenlock, CommandType::Attack, Source::Script) == Verdict::Block);
    REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(told.Of(kGenlock) == 1);
    CHECK(Lines(w, "Genlock joins the fight (enemy") == 1);
    CHECK(w.onOwnClock.contains(kGenlock));
}

TEST_CASE("one of the party that left the fight out of the world is not told to choose as the fight ends",
          "[machine][rotation]")
{
    // Those that left a fight alive are told as it lets the world go, their
    // AI having had no turn to run it (TurnMachine::LetGo). Not the party's
    // own, which the fight never tells: the game wakes a party member itself.
    World w;
    Tellings told{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.SetActive(kCompanion, false);
    w.SetActive(kGenlock, false);
    REQUIRE(w.machine.OnEndTurn());
    REQUIRE(UntilTurnOf(w, kHurlock) >= 0);
    REQUIRE(Lines(w, "Alistair leaves the fight: the game has taken it out of the world") == 1);
    REQUIRE(Lines(w, "Genlock leaves the fight: the game has taken it out of the world") == 1);
    w.applied.clear();
    w.Get(kHurlock).down = true;
    PlayOn(w, 200);
    REQUIRE_FALSE(w.machine.Active());
    CHECK(Lines(w, "no enemy left in the fight, 1 having left it alive") == 1);
    CHECK(Effects(w, EffectKind::PokeAi, kCompanion) == 0);
    CHECK(Effects(w, EffectKind::PokeAi, kGenlock) == 1);
}

TEST_CASE("the party fallen, its enemies have not left the fight for being found by nobody: the fight ends as the "
          "party's fall",
          "[machine][rotation]")
{
    // Played 2026-10-09, twice (the Broodmother, a retreat in the Dead
    // Trenches): the last of the party fell, the look after it read every
    // enemy as not found fighting the party and perceived by nobody, since
    // nobody was left to be near or to perceive, and the fight ended "no
    // enemy left in the fight, 6 having left it alive", each told to choose.
    World w;
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    for (const ObjectId id : {kHero, kCompanion})
    {
        w.Get(id).down = true;
    }
    // What the game layer reads then: nobody to measure from, and the
    // engine's lists have dropped the fallen.
    for (const ObjectId id : {kGenlock, kHurlock})
    {
        w.Get(id).fighting = false;
        w.Get(id).perception = Unperceived();
    }
    w.applied.clear();
    w.RunFor(3000); // the game's mode has not gone to Dead yet
    CHECK(Lines(w, "leaves the fight") == 0);
    CHECK(Lines(w, "the turn-based fight is over") == 0);
    CHECK(heard.Of(EventKind::Gone, kGenlock) == 0);
    CHECK(w.machine.Active());
    w.mode = Mode::Dead;
    w.RunFor(50);
    CHECK_FALSE(w.machine.Active());
    CHECK(Lines(w, "rounds: the party fell") == 1);
    CHECK(Lines(w, "having left it alive") == 0);
    CHECK(Lines(w, "that left the fight alive") == 0);
}

TEST_CASE("one of the party whose object is gone from the world has no card, and no turn", "[machine][rotation]")
{
    // A member the game destroys or takes from the area in the middle of a
    // fight (a plot's doing). Until issue 66 the party's own were taken as in
    // whatever was read of them, and one that was no longer an object kept
    // its card and its turns.
    World w;
    Tellings told{w};
    Heard heard{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kCompanion).exists = false;
    REQUIRE(w.machine.OnEndTurn());
    REQUIRE(UntilTurnOf(w, kHurlock) >= 0);
    CHECK(Lines(w, "Alistair leaves the fight: it is gone from the world") == 1);
    CHECK_FALSE(Cards(w).contains(kCompanion));
    CHECK(heard.Of(EventKind::AwaitingOrder, kCompanion) == 0);
    CHECK(heard.Of(EventKind::TurnStarted, kCompanion) == 0);
    CHECK(Effects(w, EffectKind::Select, kCompanion) == 0);
}

TEST_CASE("one of the fight that is no longer fighting the party stays while the engine's perception keeps it, as one "
          "no longer found does",
          "[machine][rotation]")
{
    // Whether it is in the party's fight and not only near it (join_by_sight,
    // join_radius) is asked of one of the fight as of one that might join,
    // and one of the fight has the engine's perception besides: the user,
    // 2026-10-04, "Once they're in the fight I think they should stay in the
    // fight for the distance that the game uses."
    World w;
    Tellings told{w};
    w.StartFight();
    REQUIRE(w.Current() == kHero);
    w.Get(kGenlock).engaged = false; // it turns on someone else, out of sight
    REQUIRE(w.machine.OnEndTurn());
    REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
    CHECK(Lines(w, "Genlock leaves the fight") == 0);
    // Lost to the party, and the party to it: it has left.
    w.Get(kGenlock).perception = Unperceived();
    REQUIRE(UntilTurnOf(w, kHero) >= 0);
    CHECK(Lines(w, "Genlock leaves the fight: not found fighting the party, and neither it nor the party perceives "
                   "the other") == 1);
    CHECK_FALSE(Cards(w).contains(kGenlock));
    // Fighting the party again, it is back.
    w.Get(kGenlock).engaged = true;
    w.Get(kGenlock).perception = Perception{};
    REQUIRE(UntilTurnOf(w, kGenlock) >= 0);
    CHECK(Cards(w).contains(kGenlock));
}
