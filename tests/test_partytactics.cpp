// Whose turns their tactics play (core/PartyTactics.h; issue 46): the rule,
// the file a save keeps the choices in, the test tool's asking, the tactics
// page's box (what it shows, what its movie asks and says), and the machine
// playing fights by them in the stand-in for the game (World.h).
//
// What is promised: off unless chosen or set; chosen, a companion's turn is
// an AI's; a choice applies from that companion's next turn, the one under
// way played out the way it began; never the hero; `[turns] party` is what a
// companion starts as, and a choice wins over it; and a save's choices are
// the playthrough's once it is loaded.
#include "World.h"

#include "core/PartyTactics.h"
#include "core/State.h"
#include "core/TurnMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace tb;
using namespace tb::test;

namespace
{

constexpr std::wstring_view kHeroTag = L"gen00pl_hero";
constexpr std::wstring_view kAlistairTag = L"gen00fl_alistair";

// The standard fight with the party's tags, as the game's scan reads them.
void Tag(World &w)
{
    w.Get(kHero).tag = kHeroTag;
    w.Get(kCompanion).tag = kAlistairTag;
}

// One of the party as the game layer hands it to the machine.
Follower Of(const World &w, ObjectId id)
{
    const Creature &c = w.Get(id);
    return Follower{c.id, c.name, c.tag, c.hero};
}

// Every AI passes when told its turn has come, but `keeps`, whose turn stays
// open for the case to look at.
void OthersPass(World &w, ObjectId keeps = kNoObject)
{
    w.onPoke = [&w, keeps](ObjectId id) {
        if (id != keeps)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
}

bool InTurn(const World &w)
{
    const Phase phase = w.machine.GetPhase();
    return phase == Phase::AwaitingOrder || phase == Phase::AiThinking || phase == Phase::Executing;
}

// Turns pass, the player's ended by End, until `id`'s has begun.
void UntilTurnOf(World &w, ObjectId id)
{
    for (int i = 0; i < 6000; ++i)
    {
        if (w.Current() == id && InTurn(w))
        {
            return;
        }
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    FAIL("its turn never came");
}

// The turn in hand is left (ended by End if the player's, else waited out),
// and turns pass until `id`'s next has begun.
void NextTurnOf(World &w, ObjectId id)
{
    const int round = w.machine.Order().Round();
    const ObjectId was = w.Current();
    for (int i = 0; i < 6000 && w.Current() == was && w.machine.Order().Round() == round; ++i)
    {
        if (w.machine.GetPhase() == Phase::AwaitingOrder)
        {
            (void)w.machine.OnEndTurn();
        }
        w.Frame();
    }
    UntilTurnOf(w, id);
}

int Lines(const World &w, std::string_view text)
{
    return static_cast<int>(
        std::ranges::count_if(w.log, [&](const std::string &line) { return line.find(text) != std::string::npos; }));
}

CardView Card(const World &w, ObjectId id)
{
    const TurnView view = w.machine.View();
    const auto card = std::ranges::find(view.upcoming, id, &CardView::id);
    REQUIRE(card != view.upcoming.end());
    return *card;
}

// Who was selected last before `told`'s AI was told to choose.
ObjectId SelectedFor(const World &w, ObjectId told)
{
    ObjectId selected = kNoObject;
    for (const Effect &e : w.applied)
    {
        if (e.kind == EffectKind::Select)
        {
            selected = e.target;
        }
        if (e.kind == EffectKind::PokeAi && e.target == told)
        {
            return selected;
        }
    }
    return kNoObject;
}

} // namespace

TEST_CASE("a companion's tactics play its turns as chosen for it, else as the setting starts it; the hero's never",
          "[partytactics]")
{
    // Nothing chosen: `[turns] party`, manual unless the file says tactics.
    CHECK_FALSE(TacticsPlay(std::nullopt, false, PartyControl::Manual));
    CHECK(TacticsPlay(std::nullopt, false, PartyControl::Tactics));
    // A choice wins over the setting, either way.
    CHECK(TacticsPlay(true, false, PartyControl::Manual));
    CHECK_FALSE(TacticsPlay(false, false, PartyControl::Tactics));
    // The hero: whatever is chosen or set.
    CHECK_FALSE(TacticsPlay(true, true, PartyControl::Manual));
    CHECK_FALSE(TacticsPlay(std::nullopt, true, PartyControl::Tactics));
    CHECK_FALSE(TacticsPlay(true, true, PartyControl::Tactics));
}

TEST_CASE("a choice is kept by the follower's tag, and a first choice is a change whatever it says", "[partytactics]")
{
    PartyTactics choices;
    CHECK_FALSE(choices.Chosen(kAlistairTag));
    CHECK(choices.Choose(kAlistairTag, false)); // the first: kept from now, though off is what nothing meant
    CHECK(choices.Chosen(kAlistairTag) == std::optional{false});
    CHECK_FALSE(choices.Choose(kAlistairTag, false)); // the same again
    CHECK(choices.Choose(kAlistairTag, true));
    CHECK(choices.Chosen(kAlistairTag) == std::optional{true});
    CHECK_FALSE(choices.Chosen(L"gen00fl_wynne")); // nobody else's
    CHECK(choices.All().size() == 1);
    // What cannot be told apart or written on a line is not kept.
    CHECK_FALSE(choices.Choose(L"", true));
    CHECK_FALSE(choices.Choose(L"two\nlines", true));
    CHECK_FALSE(choices.Choose(std::wstring{L'a', L'\r'}, true));
    CHECK(choices.All().size() == 1);
    // By the whole tag: one that begins it, or that it begins, is another's.
    CHECK_FALSE(choices.Chosen(L"gen00fl_ali"));
    CHECK_FALSE(choices.Chosen(L"gen00fl_alistair2"));
    PartyTactics two = choices;
    CHECK(two.Choose(L"gen00fl_ali", false));
    CHECK(two.Choose(L"gen00fl_alistair2", false));
    CHECK(two.Chosen(kAlistairTag) == std::optional{true});
    CHECK(two.All().size() == 3);
    // Kept in the tags' order, whatever order they were made in.
    PartyTactics other;
    CHECK(other.Choose(L"gen00fl_alistair2", false));
    CHECK(other.Choose(kAlistairTag, true));
    CHECK(other.Choose(L"gen00fl_ali", false));
    CHECK(other == two);
    CHECK(Keepable(L"gen00fl_dog"));
    CHECK(Keepable(L"a tag with spaces"));
    CHECK_FALSE(Keepable(L""));
    CHECK_FALSE(Keepable(std::wstring{L'x', static_cast<wchar_t>(0x7F)}));
}

TEST_CASE("the choices are said for the log: how many, and for whom", "[partytactics]")
{
    PartyTactics choices;
    CHECK(Describe(choices) == "none");
    (void)choices.Choose(L"gen00fl_wynne", false);
    CHECK(Describe(choices) == "1 (gen00fl_wynne off)");
    (void)choices.Choose(kAlistairTag, true);
    CHECK(Describe(choices) == "2 (gen00fl_alistair on, gen00fl_wynne off)");
}

TEST_CASE("the choices' file is a line a follower under the format's version, and reads back as written",
          "[partytactics][cosave]")
{
    PartyTactics choices;
    // With none it is written all the same: a save made over another must
    // not keep the older one's file.
    CHECK(WriteChoices(choices) == "turnbased 1\nend\n");
    CHECK(ReadChoices("turnbased 1\nend\n").problem.empty());
    CHECK(ReadChoices("turnbased 1\nend\n").choices == choices);

    (void)choices.Choose(kAlistairTag, true);
    (void)choices.Choose(L"gen00fl_wynne", false);
    (void)choices.Choose(L"a tag with spaces", true);
    (void)choices.Choose(L"end", true);               // a tag that is the file's last word
    (void)choices.Choose(L"zévran\U0001F409", false); // two bytes, and a surrogate pair's four
    const std::string file = WriteChoices(choices);
    CHECK(file == "turnbased 1\n"
                  "tactics on a tag with spaces\n"
                  "tactics on end\n"
                  "tactics on gen00fl_alistair\n"
                  "tactics off gen00fl_wynne\n"
                  "tactics off z\xc3\xa9vran\xf0\x9f\x90\x89\n"
                  "end\n");
    const ChoicesRead read = ReadChoices(file);
    CHECK(read.problem.empty());
    CHECK(read.choices == choices);
    CHECK(WriteChoices(read.choices) == file);

    // A follower not in the file has nothing chosen: the setting's start.
    CHECK_FALSE(read.choices.Chosen(L"gen00fl_morrigan"));
    CHECK(TacticsPlay(read.choices.Chosen(L"gen00fl_morrigan"), false, PartyControl::Tactics));
    CHECK_FALSE(TacticsPlay(read.choices.Chosen(L"gen00fl_morrigan"), false, PartyControl::Manual));

    // A file that went through an editor on Windows, and one whose last line
    // has no line feed, read the same.
    const ChoicesRead edited =
        ReadChoices("turnbased 1\r\ntactics on gen00fl_alistair\r\n\r\ntactics off gen00fl_wynne\r\nend");
    CHECK(edited.problem.empty());
    CHECK(edited.choices.Chosen(kAlistairTag) == std::optional{true});
    CHECK(edited.choices.Chosen(L"gen00fl_wynne") == std::optional{false});
    CHECK(edited.choices.All().size() == 2);
}

TEST_CASE("a file cut short anywhere is read as nothing", "[partytactics][cosave]")
{
    PartyTactics choices;
    (void)choices.Choose(kAlistairTag, true);
    (void)choices.Choose(L"gen00fl_wynne", false);
    (void)choices.Choose(L"zévran", true);
    const std::string file = WriteChoices(choices);
    // Every beginning of it but the whole, and the whole less its last line
    // feed, which is the whole.
    for (std::size_t length = 0; length + 1 < file.size(); ++length)
    {
        CAPTURE(length);
        const ChoicesRead read = ReadChoices(std::string_view{file}.substr(0, length));
        CHECK(read.choices.All().empty());
        CHECK_FALSE(read.problem.empty());
    }
    CHECK(ReadChoices(std::string_view{file}.substr(0, file.size() - 1)).choices == choices);
    CHECK(ReadChoices("turnbased 1\ntactics on gen00fl_alistair\n").problem ==
          "it has no \"end\" after its last line: cut short");
}

TEST_CASE("a file of another version is read as nothing, and says which", "[partytactics][cosave]")
{
    // Whatever else it holds: a newer build's lines are not guessed at.
    for (const std::string_view file :
         {"turnbased 2\ntactics on gen00fl_alistair\nend\n", "turnbased 2\nauto gen00fl_alistair every turn\n",
          "turnbased 0\nend\n", "turnbased 10\nend\n"})
    {
        CAPTURE(file);
        const ChoicesRead read = ReadChoices(file);
        CHECK(read.choices.All().empty());
        CHECK(read.problem.find("and this build reads version 1") != std::string::npos);
    }
    CHECK(ReadChoices("turnbased 2\nend\n").problem ==
          "it is of version 2, and this build reads version 1: written by another build");
}

TEST_CASE("a damaged file is read as nothing, not as the half of it that reads", "[partytactics][cosave]")
{
    const std::array<std::string_view, 17> damaged{
        "",                                                                              // empty
        "\n",                                                                            // no head
        "tactics on gen00fl_alistair\nend\n",                                            //
        "turnbased\nend\n",                                                              // no version
        "turnbased1\nend\n",                                                             //
        "turnbasedness 1\nend\n",                                                        // another file's
        "something 1\ntactics on gen00fl_alistair\nend\n",                               // by a name as long
        "turnbased one\nend\n",                                                          // no number
        "turnbased 1.5\nend\n",                                                          //
        "turnbased 1\ntactics on gen00fl_alistair\n",                                    // no end
        "turnbased 1\ntactics on gen00fl_alistair\ntactics maybe x\nend\n",              // a line of another kind
        "turnbased 1\ntactics on gen00fl_alistair\ncamera on x\nend\n",                  //
        "turnbased 1\ntactics on gen00fl_alistair\ntactics off \nend\n",                 // no tag
        "turnbased 1\ntactics on gen00fl_alistair\ntactics off gen00fl_alistair\nend\n", // a follower twice
        "turnbased 1\ntactics on gen00fl_alistair\ntactics on \xc3\nend\n",              // bytes that are no text
        "turnbased 1\ntactics on gen00fl_alistair\ntactics on a\x01z\nend\n",            // a control character
        "turnbased 1\ntactics on gen00fl_alistair\nend\ntactics on gen00fl_wynne\n",     // more after its end
    };
    for (const std::string_view file : damaged)
    {
        CAPTURE(file);
        const ChoicesRead read = ReadChoices(file);
        CHECK(read.choices.All().empty()); // Alistair's line read, and is not believed
        CHECK_FALSE(read.problem.empty());
    }
    // Which line, for whoever reads the log.
    CHECK(ReadChoices("turnbased 1\ntactics on a\n\ntactics maybe x\nend\n").problem ==
          "line 4 is not a choice (\"tactics on <tag>\" or \"tactics off <tag>\"): damaged");
    CHECK(ReadChoices("turnbased 1\ntactics on a\ntactics off a\nend\n").problem ==
          "line 3 chooses for a follower a second time: damaged");
    CHECK(ReadChoices("turnbased 1\ntactics on \nend\n").problem == "line 2 names no follower's tag: damaged");
    CHECK(ReadChoices("turnbased 1\nend\nend\n").problem == "line 3 comes after its end: damaged");
    CHECK(ReadChoices("tactics on a\nend\n").problem ==
          "it does not begin with \"turnbased <version>\": not the mod's file, or damaged");
    CHECK(ReadChoices("turnbased x\nend\n").problem == "its version is not a number: damaged");
}

TEST_CASE("the tool's asking names a companion by id or by name, and chooses only with on", "[partytactics][tool]")
{
    const AutoAskRead byId = ReadAutoAsk(R"({"id": "0x7d9", "on": true})");
    REQUIRE(byId.ask);
    CHECK(byId.ask->id == std::optional<ObjectId>{0x7d9});
    CHECK(byId.ask->name.empty());
    CHECK(byId.ask->on == std::optional{true});
    const AutoAskRead decimal = ReadAutoAsk(R"({"id": 2009, "on": false})");
    REQUIRE(decimal.ask);
    CHECK(decimal.ask->id == std::optional<ObjectId>{2009});
    CHECK(decimal.ask->on == std::optional{false});
    // By name; with no `on` it asks.
    const AutoAskRead byName = ReadAutoAsk(R"({"name": "Alistair"})");
    REQUIRE(byName.ask);
    CHECK_FALSE(byName.ask->id);
    CHECK(byName.ask->name == "Alistair");
    CHECK_FALSE(byName.ask->on);
    // An id is who is meant where both are given.
    const AutoAskRead both = ReadAutoAsk(R"({"id": "0X101", "name": "Wynne"})");
    REQUIRE(both.ask);
    CHECK(both.ask->id == std::optional<ObjectId>{0x101});
    CHECK(both.ask->name.empty());

    for (const std::string_view wrong :
         {R"({})", R"({"on": true})", R"({"name": ""})", R"({"id": "0xzz"})", R"({"id": "0x"})",
          R"({"id": "12 or so"})", R"({"id": "0xFFFFFFFF"})", R"({"id": "0x1FFFFFFFF"})",
          R"({"name": "Alistair", "on": "yes"})", R"({"name": "Alistair", "on": 1})", "not json"})
    {
        CAPTURE(wrong);
        const AutoAskRead read = ReadAutoAsk(wrong);
        CHECK_FALSE(read.ask);
        CHECK(read.error.find(R"({"name": "Alistair", "on": true})") != std::string::npos); // how to ask
    }
    CHECK(ReadAutoAsk("{}").error.starts_with("There is no id and no name. "));
    CHECK(ReadAutoAsk(R"({"id": "0xzz"})").error.starts_with("The id is not a creature's id. "));
    CHECK(ReadAutoAsk(R"({"id": "0x101", "on": "yes"})").error.starts_with("on is neither true nor false. "));
    // An id that is none is an error, a name beside it or not: the caller
    // meant somebody by it, and is not answered about somebody else.
    CHECK_FALSE(ReadAutoAsk(R"({"id": "0xzz", "name": "Alistair"})").ask);
}

TEST_CASE("the tool finds whom it was asked about in the party, and chooses for no hero", "[partytactics][tool]")
{
    const std::vector<Follower> party{
        {0x82, L"Lyna", L"gen00pl_hero", true},  {0x7d9, L"Alistair", L"gen00fl_alistair", false},
        {0x7da, L"Wolf", L"summon_wolf", false}, {0x7db, L"Wolf", L"summon_wolf", false},
        {0x7dc, L"Zévran", L"", false},
    };
    const auto ask = [](std::string_view json) {
        const AutoAskRead read = ReadAutoAsk(json);
        REQUIRE(read.ask);
        return *read.ask;
    };
    {
        const FollowerFound found = FindFollower(ask(R"({"id": "0x7d9", "on": true})"), party);
        REQUIRE(found.who != nullptr);
        CHECK(found.who->id == 0x7d9);
        CHECK(found.error.empty());
    }
    {
        // By name, whatever the letters' case.
        const FollowerFound found = FindFollower(ask(R"({"name": "aLISTAIR"})"), party);
        REQUIRE(found.who != nullptr);
        CHECK(found.who->id == 0x7d9);
    }
    {
        // A name's other characters as they are (UTF-8).
        const FollowerFound found = FindFollower(ask("{\"name\": \"z\xc3\xa9VRAN\"}"), party);
        REQUIRE(found.who != nullptr);
        CHECK(found.who->id == 0x7dc);
    }
    // Not of the party: the caller is told who is.
    CHECK(FindFollower(ask(R"({"id": "0x999"})"), party).who == nullptr);
    CHECK(FindFollower(ask(R"({"id": "0x999"})"), party).error ==
          "0x999 is not one of the party: Lyna (0x82, the hero), Alistair (0x7d9), Wolf (0x7da), Wolf (0x7db), "
          "Z\xc3\xa9vran (0x7dc).");
    CHECK(FindFollower(ask(R"({"name": "Morrigan"})"), party).who == nullptr);
    CHECK(FindFollower(ask(R"({"name": "Morrigan"})"), party)
              .error.starts_with("Nobody of the party is named \"Morrigan\": Lyna"));
    CHECK(FindFollower(ask(R"({"name": "Alistai"})"), party).who == nullptr); // all of the name
    // A name two have: the id says which.
    CHECK(FindFollower(ask(R"({"name": "wolf"})"), party).who == nullptr);
    CHECK(FindFollower(ask(R"({"name": "wolf"})"), party)
              .error.starts_with("2 of the party are named \"wolf\": give the id"));
    CHECK(FindFollower(ask(R"({"id": "0x7db", "on": true})"), party).who == &party[3]);
    // The hero is answered about, and nothing is chosen for it, on or off.
    CHECK(FindFollower(ask(R"({"name": "Lyna"})"), party).who == party.data());
    for (const std::string_view on : {"true", "false"})
    {
        const FollowerFound hero =
            FindFollower(ask(std::string{R"({"id": "0x82", "on": )"} + std::string{on} + "}"), party);
        CHECK(hero.who == nullptr);
        CHECK(hero.error == "Lyna is the hero, the player's own character: its turns always wait for the player, and "
                            "there is nothing to choose for it (issue 46: companions only).");
    }
    // One with no tag is answered about, and no choice could be kept for it.
    CHECK(FindFollower(ask(R"({"id": "0x7dc"})"), party).who == &party[4]);
    CHECK(FindFollower(ask(R"({"id": "0x7dc", "on": true})"), party).who == nullptr);
    CHECK(FindFollower(ask(R"({"id": "0x7dc", "on": true})"), party).error ==
          "Z\xc3\xa9vran has no tag a choice could be kept by.");
    // No party: the main menu.
    CHECK(FindFollower(ask(R"({"name": "Alistair"})"), {}).error ==
          "There is no party in the world now: at the main menu, or loading.");
}

TEST_CASE("the tool answers who, the state now, and from when it applies", "[partytactics][tool]")
{
    const Follower alistair{0x7d9, L"Ali\"stair", L"gen00fl_alistair", false};
    TacticsAnswer answer;
    answer.on = true;
    answer.chosen = true;
    answer.changed = true;
    CHECK(ToJson(alistair, answer) ==
          R"({"id":"0x7d9","name":"Ali\"stair","tag":"gen00fl_alistair","hero":false,"on":true,"by":"chosen",)"
          R"("changed":true,"applies":"from its next turn","turn_under_way":null})");
    // Chosen in its own turn: that turn is played out as it began.
    answer.turnUnderWay = true;
    CHECK(ToJson(alistair, answer)
              .ends_with(R"x("applies":"from its next turn; the turn under way is played out as it began (yours)",)x"
                         R"x("turn_under_way":"yours"})x"));
    answer.turnIsTactics = true;
    CHECK(ToJson(alistair, answer)
              .ends_with(R"x("applies":"from its next turn; the turn under way is played out as it began (AI)",)x"
                         R"x("turn_under_way":"AI"})x"));
    // Asked of one nothing was chosen for: the setting's word.
    const TacticsAnswer unchosen;
    CHECK(ToJson(Follower{0x82, L"Lyna", L"gen00pl_hero", true}, unchosen) ==
          R"({"id":"0x82","name":"Lyna","tag":"gen00pl_hero","hero":true,"on":false,"by":"setting",)"
          R"("changed":false,"applies":"from its next turn","turn_under_way":null})");
}

TEST_CASE("with nothing chosen and the setting as shipped, every party member's turn waits for the player",
          "[partytactics][machine]")
{
    World w;
    Tag(w);
    OthersPass(w);
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK(Lines(w, "round 1: Alistair's turn (party, yours)") == 1);
    CHECK(Card(w, kCompanion).yours);
    CHECK_FALSE(Card(w, kCompanion).tacticsPlay);
    const TacticsAnswer answer = w.machine.TacticsOf(Of(w, kCompanion));
    CHECK_FALSE(answer.on);
    CHECK_FALSE(answer.chosen);
    CHECK_FALSE(answer.changed);
    CHECK(answer.turnUnderWay);
    CHECK_FALSE(answer.turnIsTactics);
    CHECK(w.machine.GetPartyTactics().All().empty());
}

TEST_CASE("chosen for a companion, its turn is its tactics': told to choose, the hero selected, bound by a turn",
          "[partytactics][machine]")
{
    World w; // [turns] party = manual
    Tag(w);
    OthersPass(w, kCompanion);
    std::vector<TurnEvent> started;
    w.machine.SetEventSink([&](const TurnEvent &e) {
        if (e.kind == EventKind::TurnStarted)
        {
            started.push_back(e);
        }
    });
    // Before the fight: asked of one who is in no fight, it has no turn.
    const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kCompanion), true, "the turnbased_auto tool");
    CHECK(chosen.on);
    CHECK(chosen.chosen);
    CHECK(chosen.changed);
    CHECK_FALSE(chosen.turnUnderWay);
    CHECK(Lines(w, "Alistair's tactics play its turns from its next turn (set by the turnbased_auto tool)") == 1);
    CHECK(w.log.back() == "Alistair's tactics play its turns from its next turn (set by the turnbased_auto tool)");
    // Said once: the same choice again changes nothing and says nothing.
    CHECK_FALSE(w.machine.ChooseTactics(Of(w, kCompanion), true, "the turnbased_auto tool").changed);
    CHECK(Lines(w, "Alistair's tactics play its turns") == 1);

    w.StartFight();
    REQUIRE(w.Current() == kHero);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder); // the hero's is the player's
    CHECK(Card(w, kCompanion).tacticsPlay);
    CHECK_FALSE(Card(w, kCompanion).yours);
    CHECK_FALSE(Card(w, kHero).tacticsPlay);
    CHECK_FALSE(Card(w, kGenlock).tacticsPlay); // an enemy's AI is no tactics of the party's
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
    // Its full AI runs only while it is not the one the player controls.
    CHECK(SelectedFor(w, kCompanion) == kHero);
    CHECK(Lines(w, "round 1: Alistair's turn (party, AI)") == 1);
    REQUIRE(started.size() == 3);
    CHECK(started[2].who == kCompanion);
    CHECK(started[2].detail == "AI");
    CHECK_FALSE(w.machine.View().playerTurn);
    // The turn is its AI's: what its tactics choose is let through, and End
    // and Delay are not offered on it.
    CHECK_FALSE(w.machine.View().delayOffered);
    // (A swing of its whole bar: with time left it would be told to choose
    // again.)
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Script, 0.0f, 4.0f) == Verdict::Allow);
    w.Finish(kCompanion);
    w.RunFor(1200);
    CHECK(Lines(w, "Alistair's turn ends: done") == 1);
    const TacticsAnswer after = w.machine.TacticsOf(Of(w, kCompanion));
    CHECK(after.on);
    CHECK_FALSE(after.turnUnderWay);
}

TEST_CASE("a choice made in a companion's own turn applies from its next: the turn under way is the player's still",
          "[partytactics][machine]")
{
    // The user, 2026-10-05: "Should be changeable even during battle,
    // applies to character's next turn." A turn begins where it is handed
    // out, and is played out the way it began.
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    w.applied.clear();
    const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kCompanion), true, "the turnbased_auto tool");
    CHECK(chosen.on);
    CHECK(chosen.changed);
    CHECK(chosen.turnUnderWay);
    CHECK_FALSE(chosen.turnIsTactics);
    CHECK(w.log.back() == "Alistair's tactics play its turns from its next turn (set by the turnbased_auto tool); the "
                          "turn under way is played out as it began, by the player");
    w.RunFor(300);
    // Nothing of the turn changed hands.
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK_FALSE(w.Did(EffectKind::Select, kHero));
    CHECK(w.machine.View().playerTurn);
    // The card says both: this turn is the player's, the next its tactics'.
    CHECK(Card(w, kCompanion).yours);
    CHECK(Card(w, kCompanion).tacticsPlay);
    // The player's orders are taken, and its own AI's are still held.
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Script, 0.0f) == Verdict::Block);
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 0.0f) == Verdict::Allow);
    w.Finish(kCompanion);
    w.RunFor(600);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder); // and it waits on the player again after its action
    CHECK(w.Current() == kCompanion);
    // Its next turn is its tactics'.
    w.applied.clear();
    NextTurnOf(w, kCompanion);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK(SelectedFor(w, kCompanion) == kHero);
    CHECK_FALSE(Card(w, kCompanion).yours);
    CHECK(Lines(w, "round 1: Alistair's turn (party, yours)") == 1);
    CHECK(Lines(w, "round 2: Alistair's turn (party, AI)") == 1);
}

TEST_CASE("a choice taken back in a companion's own turn applies from its next: its tactics play the one under way out",
          "[partytactics][machine]")
{
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
    w.applied.clear();
    const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kCompanion), false, "the turnbased_auto tool");
    CHECK_FALSE(chosen.on);
    CHECK(chosen.chosen);
    CHECK(chosen.changed);
    CHECK(chosen.turnUnderWay);
    CHECK(chosen.turnIsTactics);
    CHECK(w.log.back() == "Alistair's turns wait for the player from its next turn (set by the turnbased_auto tool); "
                          "the turn under way is played out as it began, by its tactics");
    w.RunFor(100);
    // Still its AI's: not handed to the player, and what its tactics choose
    // is let through.
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK_FALSE(w.Did(EffectKind::Select, kCompanion));
    CHECK_FALSE(w.machine.View().playerTurn);
    CHECK_FALSE(Card(w, kCompanion).yours);
    CHECK_FALSE(Card(w, kCompanion).tacticsPlay);
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Script, 0.0f, 4.0f) == Verdict::Allow); // its whole bar
    w.Finish(kCompanion);
    w.RunFor(1200);
    CHECK(Lines(w, "Alistair's turn ends: done") == 1);
    // Its next turn waits for the player.
    w.applied.clear();
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.Order().Round() == 2);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(w.Did(EffectKind::Select, kCompanion));
    CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
    CHECK(Lines(w, "round 2: Alistair's turn (party, yours)") == 1);
}

TEST_CASE("a choice made between a companion's turns is its next turn's, in the same round if that is still to come",
          "[partytactics][machine]")
{
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    w.StartFight();
    REQUIRE(w.Current() == kHero); // Alistair's turn of this round is still to come
    const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
    CHECK(chosen.changed);
    CHECK_FALSE(chosen.turnUnderWay); // the hero's is, not his
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.Order().Round() == 1);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
}

TEST_CASE("a turn put off begins again when it comes at the round's end, by what is chosen then",
          "[partytactics][machine][delay]")
{
    // Nothing of a turn put off was spent, and it is handed out a second
    // time: that is where it begins.
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    REQUIRE(w.machine.OnDelay());
    (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
    w.applied.clear();
    w.Frame();
    REQUIRE(w.Current() != kCompanion); // put off: the hurlock's turn, which passes
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.Order().Round() == 1);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK(w.Did(EffectKind::PokeAi, kCompanion));
}

TEST_CASE("a turn a conversation interrupted goes on the way it began, whatever was chosen meanwhile",
          "[partytactics][machine]")
{
    const auto converse = [](World &w) {
        w.mode = Mode::Dialog;
        w.Frame();
        REQUIRE(w.machine.GetPhase() == Phase::Suspended);
        w.mode = Mode::Combat;
        w.RunFor(100);
        REQUIRE(w.Current() == kCompanion);
    };
    SECTION("the player's")
    {
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
        w.applied.clear();
        converse(w);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK(w.Did(EffectKind::Select, kCompanion));
        CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
        CHECK(Lines(w, "Alistair's turn goes on") == 1);
    }
    SECTION("its tactics'")
    {
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), false, "a test");
        w.applied.clear();
        converse(w);
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
        CHECK(w.Did(EffectKind::PokeAi, kCompanion)); // told again, as any AI's turn that goes on
        CHECK_FALSE(w.Did(EffectKind::Select, kCompanion));
    }
}

TEST_CASE("the hero's turns are the player's, whatever is chosen, loaded or set", "[partytactics][machine]")
{
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics;
    for (const Settings &settings : {Fixed(), tactics})
    {
        CAPTURE(settings.partyControl == PartyControl::Tactics);
        World w{settings};
        Tag(w);
        OthersPass(w);
        // Nothing is kept for the hero, on or off.
        const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kHero), true, "a test");
        CHECK_FALSE(chosen.on);
        CHECK_FALSE(chosen.chosen);
        CHECK_FALSE(chosen.changed);
        CHECK_FALSE(w.machine.ChooseTactics(Of(w, kHero), false, "a test").changed);
        CHECK(w.machine.GetPartyTactics().All().empty());
        CHECK(Lines(w, "Hero's") == 0);
        // And a choice a save holds for the hero's tag (a file made by hand)
        // is asked for nobody.
        PartyTactics loaded;
        REQUIRE(loaded.Choose(kHeroTag, true));
        w.machine.SetPartyTactics(loaded);
        CHECK_FALSE(w.machine.TacticsOf(Of(w, kHero)).on);
        CHECK_FALSE(w.machine.TacticsOf(Of(w, kHero)).chosen);
        w.StartFight();
        REQUIRE(w.Current() == kHero);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK_FALSE(w.Did(EffectKind::PokeAi, kHero));
        CHECK(Card(w, kHero).yours);
        CHECK_FALSE(Card(w, kHero).tacticsPlay);
        NextTurnOf(w, kHero);
        CHECK(w.machine.Order().Round() == 2);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK_FALSE(w.Did(EffectKind::PokeAi, kHero));
    }
}

TEST_CASE("the setting is what a companion starts as, and a choice wins over it either way", "[partytactics][machine]")
{
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics;
    {
        // party = tactics, nothing chosen: its tactics', as it always was.
        World w{tactics};
        Tag(w);
        OthersPass(w, kCompanion);
        const TacticsAnswer start = w.machine.TacticsOf(Of(w, kCompanion));
        CHECK(start.on);
        CHECK_FALSE(start.chosen);
        w.StartFight();
        CHECK(Card(w, kCompanion).tacticsPlay);
        UntilTurnOf(w, kCompanion);
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
    }
    {
        // party = tactics, and off chosen for him: the player's.
        World w{tactics};
        Tag(w);
        OthersPass(w, kCompanion);
        const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, kCompanion), false, "a test");
        CHECK_FALSE(chosen.on);
        CHECK(chosen.chosen);
        CHECK(chosen.changed);
        w.StartFight();
        CHECK_FALSE(Card(w, kCompanion).tacticsPlay);
        UntilTurnOf(w, kCompanion);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
    }
    {
        // party = manual, and on chosen for him: his tactics', and the
        // choice stays his when the setting is set again.
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
        w.machine.SetSettings(Fixed());
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
    }
    {
        // A companion with no tag has nothing kept for it: the setting's.
        World w{tactics};
        OthersPass(w, kCompanion);
        CHECK_FALSE(w.machine.ChooseTactics(Of(w, kCompanion), false, "a test").changed);
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
    }
}

TEST_CASE("the setting changed in a turn under way hands over the turn of a companion that goes by it, and no other's",
          "[partytactics][machine]")
{
    // As since the second play test (2026-09-30) for a companion nothing was
    // chosen for; one a choice was made for began its turn by that choice,
    // and the setting is not asked about it.
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics;
    {
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        w.applied.clear();
        w.machine.SetSettings(tactics);
        w.Frame();
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
        CHECK(w.Did(EffectKind::PokeAi, kCompanion));
    }
    {
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), false, "a test");
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
        w.applied.clear();
        w.machine.SetSettings(tactics);
        w.RunFor(100);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
        CHECK_FALSE(w.Did(EffectKind::PokeAi, kCompanion));
        CHECK(Lines(w, "Alistair's turn is") == 0);
    }
    {
        // Chosen in the turn, the setting changed after: the turn began by
        // the setting and still goes by it, and the choice is the next's.
        World w;
        Tag(w);
        OthersPass(w, kCompanion);
        w.StartFight();
        UntilTurnOf(w, kCompanion);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), false, "a test");
        w.applied.clear();
        w.machine.SetSettings(tactics);
        w.Frame();
        CHECK(w.machine.GetPhase() == Phase::AiThinking);
        CHECK(w.Did(EffectKind::PokeAi, kCompanion));
        (void)w.Order(kCompanion, CommandType::Wait, Source::Script);
        w.applied.clear();
        NextTurnOf(w, kCompanion);
        CHECK(w.machine.Order().Round() == 2);
        CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    }
}

TEST_CASE("a save's choices are the playthrough's once loaded, in place of what was chosen before",
          "[partytactics][machine][cosave]")
{
    constexpr ObjectId kMage = 0x102;
    std::vector<Creature> fight = StandardFight();
    fight.push_back(Make(kMage, Side::Party, false, L"Wynne", 12.0f)); // after Alistair
    World w{Fixed(), fight};
    Tag(w);
    w.Get(kMage).tag = L"gen00fl_wynne";
    OthersPass(w);
    // Chosen in the session, for Wynne; then a save is loaded that was made
    // with Alistair's tactics playing his turns and nothing chosen for her.
    (void)w.machine.ChooseTactics(Of(w, kMage), true, "a test");
    const ChoicesRead save = ReadChoices("turnbased 1\ntactics on gen00fl_alistair\nend\n");
    REQUIRE(save.problem.empty());
    w.machine.SetPartyTactics(save.choices);
    CHECK(w.machine.GetPartyTactics() == save.choices);
    CHECK(w.machine.TacticsOf(Of(w, kCompanion)).on);
    CHECK(w.machine.TacticsOf(Of(w, kCompanion)).chosen);
    CHECK_FALSE(w.machine.TacticsOf(Of(w, kMage)).on);
    CHECK_FALSE(w.machine.TacticsOf(Of(w, kMage)).chosen);

    w.onPoke = [&w](ObjectId id) {
        if (id != kCompanion)
        {
            (void)w.Order(id, CommandType::Wait, Source::Script);
        }
    };
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    // The turn under way is Alistair's alone: what the cards say of the
    // others is each one's own.
    CHECK_FALSE(Card(w, kCompanion).yours);
    CHECK(Card(w, kMage).yours);
    CHECK(Card(w, kHero).yours);
    CHECK_FALSE(Card(w, kMage).tacticsPlay);
    (void)w.Order(kCompanion, CommandType::Wait, Source::Script);
    UntilTurnOf(w, kMage);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK_FALSE(Card(w, kCompanion).yours);
    CHECK(Card(w, kMage).yours);
    // What is written with the next save is what was read, with what was
    // chosen since.
    (void)w.machine.ChooseTactics(Of(w, kMage), false, "a test");
    CHECK(WriteChoices(w.machine.GetPartyTactics()) ==
          "turnbased 1\ntactics on gen00fl_alistair\ntactics off gen00fl_wynne\nend\n");
    // A new game, or a save from before the mod kept any: nothing chosen.
    w.machine.SetPartyTactics({});
    CHECK(w.machine.GetPartyTactics().All().empty());
    CHECK_FALSE(w.machine.TacticsOf(Of(w, kCompanion)).on);
}

TEST_CASE("the state says of each party member whether its tactics play its turns", "[partytactics][state]")
{
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    w.StartFight();
    const auto count = [](const std::string &json, std::string_view text) {
        int found = 0;
        for (std::size_t at = json.find(text); at != std::string::npos; at = json.find(text, at + 1))
        {
            ++found;
        }
        return found;
    };
    std::string json = ToJson(w.machine.State());
    // The party's two, and neither enemy.
    CHECK(count(json, R"("auto":false})") == 2);
    CHECK(count(json, R"("auto":)") == 2);
    CHECK(json.find(R"("name":"Alistair","side":"party","rank":0,"health":1.00,"energy":1.00,"mana":false,)"
                    R"("down":false,"current":false,"yours":true,"round":1,"auto":false})") != std::string::npos);
    CHECK(json.find(R"("name":"Genlock","side":"enemy","rank":0,"health":1.00,"energy":1.00,"mana":false,)"
                    R"("down":false,"current":false,"yours":false,"round":1})") != std::string::npos);
    // Chosen in his own turn: this turn is yours, and auto says the next is not.
    UntilTurnOf(w, kCompanion);
    (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
    json = ToJson(w.machine.State());
    CHECK(json.find(R"("current":{"id":"0x101","name":"Alistair","side":"party","yours":true})") != std::string::npos);
    CHECK(json.find(R"("down":false,"current":true,"yours":true,"round":1,"auto":true})") != std::string::npos);
    CHECK(count(json, R"("auto":)") == 2);
}

TEST_CASE("as built, the player's order in a turn a companion's tactics play is one more command of that turn",
          "[partytactics][machine]")
{
    // Not decided, and not built (issue 46, "Taking a turn back": "an order
    // from the player during that character's turn still has to win"). What
    // the gate does today, held here so that a change to it is a decision:
    // the order is let through and spent of the turn as its AI's own would
    // be, with an AI's leave to close in on what is out of reach; the turn
    // stays its tactics', which are told to choose again after it; and End
    // ends no turn the player does not play.
    Settings s = Fixed();
    s.secondsPerTurn = 4.0f;
    s.paceMps = 5.0f;
    World w{s};
    Tag(w);
    OthersPass(w, kCompanion);
    (void)w.machine.ChooseTactics(Of(w, kCompanion), true, "a test");
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AiThinking);
    // End: taken as a key of the fight's, and nothing ends.
    CHECK(w.machine.OnEndTurn());
    w.RunFor(100);
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    // The player's attack at something further than the bar would walk: an
    // AI's is let through to close in, and so is this, with nothing said.
    w.applied.clear();
    CHECK(w.Order(kCompanion, CommandType::Attack, Source::Player, 30.0f, 1.5f) == Verdict::Allow);
    w.Frame();
    CHECK_FALSE(w.Said(L"Too far"));
    CHECK(w.machine.GetPhase() == Phase::Executing);
    CHECK_FALSE(w.machine.View().playerTurn);
    w.Finish(kCompanion);
    w.RunFor(1200);
    // Its tactics go on with what is left: told to choose again, and the
    // turn does not wait on the player.
    CHECK(w.Current() == kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    CHECK(Lines(w, "Alistair chooses again") == 1);
    CHECK_FALSE(w.machine.View().playerTurn);
}

// The tactics page's box, "Use when turn-based" (issue 46's second step): the
// player's way to the same choice. What it shows of whom, what its movie asks
// and says, and that a click is the choice the tool makes.

TEST_CASE("the tactics page's box shows whether a companion's tactics play its turns, and there is none for the hero",
          "[partytactics][box]")
{
    const Follower alistair{0x7d9, L"Alistair", L"gen00fl_alistair", false};
    TacticsAnswer on;
    on.on = true;
    const TacticsAnswer off;
    CHECK(BoxFor(alistair, on) == BoxState::On);
    CHECK(BoxFor(alistair, off) == BoxState::Off);
    // Chosen or by the setting, in a turn or between two: what will apply
    // at its next turn is all the box shows.
    on.chosen = true;
    on.turnUnderWay = true;
    CHECK(BoxFor(alistair, on) == BoxState::On);
    // The hero has none, whatever is answered of it (the user, 2026-10-05:
    // "We can skip the hero for now").
    const Follower hero{0x82, L"Lyna", L"gen00pl_hero", true};
    CHECK(BoxFor(hero, off) == BoxState::None);
    CHECK(BoxFor(hero, on) == BoxState::None);
    // Nor has one a click could keep nothing for: no tag, or one that is
    // no line of the save's file.
    CHECK(BoxFor(Follower{0x7dc, L"Wolf", L"", false}, on) == BoxState::None);
    CHECK(BoxFor(Follower{0x7dd, L"Wolf", L"two\nlines", false}, off) == BoxState::None);
    // The numbers are the movie's: its question is answered with them.
    CHECK(static_cast<int>(BoxState::None) == -1);
    CHECK(static_cast<int>(BoxState::Off) == 0);
    CHECK(static_cast<int>(BoxState::On) == 1);
}

TEST_CASE("the boxes are kept for those that have one, and anyone else has none", "[partytactics][box]")
{
    TacticsBoxes boxes;
    CHECK(boxes.Of(0x7d9) == BoxState::None);
    boxes.Set(0x7d9, BoxState::On);
    boxes.Set(0x7da, BoxState::Off);
    boxes.Set(0x82, BoxState::None);
    CHECK(boxes.Of(0x7d9) == BoxState::On);
    CHECK(boxes.Of(0x7da) == BoxState::Off);
    CHECK(boxes.Of(0x82) == BoxState::None);
    CHECK(boxes.Of(0x999) == BoxState::None); // nobody's id
    CHECK(boxes.Of(kNoObject) == BoxState::None);
    // The same for everyone is the same, in whatever order they were read
    // and with or without those that have none.
    TacticsBoxes other;
    other.Set(0x7da, BoxState::Off);
    other.Set(0x7d9, BoxState::On);
    CHECK(other == boxes);
    // One changed is another: that is what the open page is told of.
    other.Set(0x7da, BoxState::On);
    CHECK_FALSE(other == boxes);
    CHECK(other.Of(0x7da) == BoxState::On);
    other.Set(0x7da, BoxState::Off);
    CHECK(other == boxes);
    // And one whose box is gone has none, and is not kept.
    other.Set(0x7d9, BoxState::None);
    CHECK(other.Of(0x7d9) == BoxState::None);
    CHECK_FALSE(other == boxes);
    TacticsBoxes one;
    one.Set(0x7da, BoxState::Off);
    CHECK(other == one);
}

TEST_CASE("the boxes are said for the log: whose is on, whose is off, and who has none", "[partytactics][box]")
{
    const std::vector<Follower> party{
        {0x82, L"Lyna", L"gen00pl_hero", true},
        {0x7d9, L"Alistair", L"gen00fl_alistair", false},
        {0x7da, L"Wynne", L"gen00fl_wynne", false},
        {0x7dc, L"Wolf", L"", false},
    };
    TacticsBoxes boxes;
    CHECK(Describe(std::span<const Follower>{}, boxes) == "nobody");
    CHECK(Describe(party, boxes) == "none for Lyna (the hero), Alistair, Wynne, Wolf");
    boxes.Set(0x7d9, BoxState::On);
    boxes.Set(0x7da, BoxState::Off);
    CHECK(Describe(party, boxes) == "Alistair on, Wynne off; none for Lyna (the hero), Wolf");
    CHECK(Describe(std::span{party}.subspan(1, 2), boxes) == "Alistair on, Wynne off");
}

TEST_CASE("the box's movie asks by a creature's id, all of the text", "[partytactics][box]")
{
    // As the tactics screen's data tree gives it (GUI.SelectedChar.ID: 130
    // for a hero, 238717 for Shale, read in the game 2026-10-05).
    CHECK(ReadBoxAsk("238717") == std::optional<ObjectId>{238717});
    CHECK(ReadBoxAsk("130") == std::optional<ObjectId>{130});
    CHECK(ReadBoxAsk("0") == std::optional<ObjectId>{0});
    CHECK(ReadBoxAsk("4294967294") == std::optional<ObjectId>{0xFFFFFFFE});
    CHECK(ReadBoxAsk("0x7d9") == std::optional<ObjectId>{0x7d9}); // as Eluvian writes ids
    for (const std::string_view wrong :
         {"", " ", "undefined", "null", "NaN", "238717 ", " 238717", "238717.5", "2.38717e5", "-1", "+5", "12 or so",
          "0x", "0xzz", "4294967295", "0xFFFFFFFF", "4294967296", "99999999999"})
    {
        CAPTURE(wrong);
        CHECK_FALSE(ReadBoxAsk(wrong));
    }
}

TEST_CASE("a click of the box says for whom and what the box shows after it, or it is no click", "[partytactics][box]")
{
    CHECK(ReadBoxClick("238717 1") == std::optional{BoxClick{238717, true}});
    CHECK(ReadBoxClick("238717 0") == std::optional{BoxClick{238717, false}});
    CHECK(ReadBoxClick("0x7d9 1") == std::optional{BoxClick{0x7d9, true}});
    for (const std::string_view wrong :
         {"", " ", "1", "238717", "238717 ", " 1", "238717  1", "238717 1 ", "238717 2", "238717 true", "238717 on",
          "238717 10", "238717 01", "238717,1", "238717\t1", "undefined 1", "4294967295 1", "1 238717 1", "238717 -1"})
    {
        CAPTURE(wrong);
        CHECK_FALSE(ReadBoxClick(wrong));
    }
}

TEST_CASE("the machine's boxes are the setting's start, a choice over it, and none for the hero",
          "[partytactics][box][machine]")
{
    Settings tactics = Fixed();
    tactics.partyControl = PartyControl::Tactics;
    {
        // As shipped: off by default, and none on the hero's page.
        World w;
        Tag(w);
        const std::array party{Of(w, kHero), Of(w, kCompanion)};
        const TacticsBoxes boxes = w.machine.BoxesOf(party);
        CHECK(boxes.Of(kHero) == BoxState::None);
        CHECK(boxes.Of(kCompanion) == BoxState::Off);
        CHECK(boxes.Of(kGenlock) == BoxState::None); // not asked about: not of the party
        CHECK(Describe(party, boxes) == "Alistair off; none for Hero (the hero)");
        CHECK(w.machine.BoxesOf(std::span<const Follower>{}) == TacticsBoxes{});
    }
    {
        // party = tactics is what a companion's box starts as, and a choice
        // wins over it; the hero's page has none under it either.
        World w{tactics};
        Tag(w);
        const std::array party{Of(w, kHero), Of(w, kCompanion)};
        CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::On);
        CHECK(w.machine.BoxesOf(party).Of(kHero) == BoxState::None);
        (void)w.machine.ChooseTactics(Of(w, kCompanion), false, "the tactics page's box");
        CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::Off);
    }
    {
        // A companion with no tag goes by the setting and has no box to
        // change that with: a click could keep nothing for it.
        World w{tactics};
        const std::array party{Of(w, kHero), Of(w, kCompanion)};
        CHECK(w.machine.TacticsOf(Of(w, kCompanion)).on);
        CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::None);
    }
}

TEST_CASE("a click of the box is the choice: said once with who asked, kept, and its companion's next turn's",
          "[partytactics][box][machine]")
{
    // The user, 2026-10-05: "Should be changeable even during battle,
    // applies to character's next turn." The page opens paused in a fight
    // too, so a click can come in the companion's own turn.
    World w;
    Tag(w);
    OthersPass(w, kCompanion);
    w.StartFight();
    UntilTurnOf(w, kCompanion);
    REQUIRE(w.machine.GetPhase() == Phase::AwaitingOrder);
    const std::array party{Of(w, kHero), Of(w, kCompanion)};
    const std::optional<BoxClick> click = ReadBoxClick(std::format("{} 1", kCompanion));
    REQUIRE(click);
    const TacticsAnswer chosen = w.machine.ChooseTactics(Of(w, click->id), click->on, "the tactics page's box");
    CHECK(chosen.changed);
    CHECK(w.log.back() == "Alistair's tactics play its turns from its next turn (set by the tactics page's box); the "
                          "turn under way is played out as it began, by the player");
    // The box shows the choice at once, though the turn under way is the
    // player's still: it is of the next turn, as the card's `auto` is.
    CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::On);
    CHECK(w.machine.GetPhase() == Phase::AwaitingOrder);
    CHECK(Card(w, kCompanion).yours);
    CHECK(w.machine.GetPartyTactics().Chosen(kAlistairTag) == std::optional{true});
    // The same click again changes nothing and says nothing.
    const std::size_t lines = w.log.size();
    CHECK_FALSE(w.machine.ChooseTactics(Of(w, click->id), click->on, "the tactics page's box").changed);
    CHECK(w.log.size() == lines);
    NextTurnOf(w, kCompanion);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    // Unticked while its tactics play the turn: they play it out, and the
    // box is off from the click.
    const std::optional<BoxClick> off = ReadBoxClick(std::format("{} 0", kCompanion));
    REQUIRE(off);
    (void)w.machine.ChooseTactics(Of(w, off->id), off->on, "the tactics page's box");
    CHECK(w.log.back() == "Alistair's turns wait for the player from its next turn (set by the tactics page's box); "
                          "the turn under way is played out as it began, by its tactics");
    CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::Off);
    CHECK(w.machine.GetPhase() == Phase::AiThinking);
    // A save's choices loaded are the boxes' from then.
    PartyTactics loaded;
    REQUIRE(loaded.Choose(kAlistairTag, true));
    w.machine.SetPartyTactics(loaded);
    CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::On);
    w.machine.SetPartyTactics(PartyTactics{});
    CHECK(w.machine.BoxesOf(party).Of(kCompanion) == BoxState::Off);
}
