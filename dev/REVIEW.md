# DAOTurnBased review

Reviewed 2026-09-30. Scope: turn-state transitions, engine command gate, frame integration, live settings, portrait/UI lifetime, and the Eluvian integration. Source review plus the existing debug test suite; no game was launched or deployed. This is not an exhaustive verification of engine addresses or every combat rule.

## Assessment

The architecture is well suited to this mod: core decides from observations and emits effects, while game performs engine operations through DAOLib. Plugin.cpp releases the machine mutex before applying effects, allowing synchronous AI commands to re-enter the gate without deadlocking that mutex. The command gate explicitly handles ownership of rejected engine commands. Scripted tests, randomized fights, and the documented mutation-testing workflow are strong foundations.

The greatest remaining risk is at the boundary between the state machine and persistent engine state: turning UI features off, changing timing policies during a fight, and recovering from an exception after the machine has already advanced. A correct pure-core transition does not establish that its effects were applied successfully in the game.

## Findings

### TB-1: Disabling the turn bar bypasses its cleanup

Priority: high for UI/resource lifetime. References: src/plugin/Plugin.cpp:119, :157; src/game/Bar.cpp:127; src/core/Portraits.cpp.

UseTurnBar sets g.turnBar to the new setting. ServeBar then returns immediately whenever it is false. If feedback / turn_bar is switched from true to false while the screen is shown, the plugin stops publishing views, running FollowFight, processing the movie's release acknowledgement, and calling KeepScreen. The existing screen is not hidden by the setting change, and rendered portraits do not reach the normal release path when the fight ends. The same problem occurs if disabled while releases are pending.

Reproduction to run in-game: enable the bar and portraits, start a fight, disable turn_bar through the INI or Eluvian, and finish the fight or change areas. Check that the screen disappears and that owned portrait renderers are released. Source inspection shows neither is driven while ServeBar is skipped; an actual crash from this sequence was not reproduced here.

Give the bar an explicit disable/cleanup transition. Continue publishing an empty view and servicing the movie's release handshake until it can be hidden and its owned renderers released. Add a lifecycle test for disabling during combat and while cleanup is pending, not just tests of PortraitBook in isolation.

### TB-2: Enabling effects-in-rounds mid-fight never holds the clock

Priority: medium. References: src/core/TurnMachine.cpp:146, :517, :809, :824.

StartFight calls HoldTime(settings_.effectSecondsPerRound > 0). SetSettings changes the policy without reconciling timeHeld_. SpendRound immediately returns when timeHeld_ is false. Therefore, starting a fight with effect_seconds_per_round = 0 and changing it to 6 during that fight never enables round-based aging for the remainder of that fight. Turning it off is detected at SpendRound, but turning it back on after release is not.

Define when this policy changes take effect and reconcile the engine hold at that boundary. Alternatively, defer this setting explicitly to the next fight and report that behavior. Add core tests for 0 -> positive and positive -> 0 -> positive during an active fight. This is a source-confirmed asymmetric transition; it was not exercised in the game.

### TB-3: Frame exceptions can leave the machine ahead of engine effects

Priority: medium; recovery design gap. References: src/plugin/Plugin.cpp:198-220 and :505-518; src/core/TurnMachine.cpp:277, :795-817; src/game/Bridge.cpp, Bridge::Apply.

Tick advances the machine and obtains an effect vector before applying it. The outer frame callback catches an exception and merely logs it. If Bridge::Apply throws after some effects, the rest of that local vector is lost while the machine retains its new phase and bookkeeping. SetSpeed and HoldTime update their cached state when emitting effects, so a missed engine operation may not be emitted again. A thrown logging/allocation operation in the application path is sufficient; this finding does not require treating an access violation as a C++ exception.

Distinguish requested state from successfully applied engine state, or enter an explicit recovery mode that releases held clocks, restores speed, and stops turn enforcement safely. Do not blindly retry non-idempotent effects such as aging time or signalling an AI event. Add fault-injection coverage at the observe/step/apply boundary. This path was identified in source, not induced in-game.

### TB-4: The initiative seed tool accepts partial integers

Priority: low. Reference: src/plugin/Plugin.cpp:289-303.

SeedTool checks from_chars(...).ec but not the returned end pointer. JsonField exposes numeric text as a string, so a valid JSON numeric input such as seed: 42.5 or seed: 42e2 is accepted as 42. A string containing a numeric prefix has the same issue. That undermines reproducibility when the reply does not match the intended seed. An inputSchema is descriptive here; the handler must enforce the contract itself.

Require complete consumption and an integer representation, with tests for fractional, exponential, trailing-text, negative, and overflowing inputs. Keep the accepted range explicit.

## Related-project considerations

This checkout builds against extern/DAOLib at 79d2baae447b77c62897217c1f0ab92af1fc4260. DAOEluvianMCP's reviewed checkout uses f253b8c60ea03be49bd40f0e45c05b80e60a3025. Because DAOLib is statically linked into each plugin, their implementations and state can differ inside one game process. The DAOLib sibling review's loader-lock, hook-failure, and executable-validation findings should be checked against each pinned revision rather than assumed fixed by updating the sibling working tree.

The live integration with Eluvian is intentionally optional and discovered on the first frame. Engine mutation remains on the frame thread for the registered turn-based tools. The reviewed code avoids calling the engine while holding the machine mutex during effect application.

## Validation and limits

Ran tools/build.ps1 -Preset debug -Test successfully: all 139 CTest entries passed. Existing gameplay screenshots were already untracked before this review and were left untouched. No implementation files were changed.

The full mutation run, the separately invoked thousand-fight run, ASan, static analysis, movie rendering, and in-game playtests were not rerun. The findings above are source-level findings unless otherwise stated. README.md still says the mod has not been played, while dev/PLAYTEST-* and implementation comments describe multiple sessions; update that status to keep user-facing maturity claims consistent.

