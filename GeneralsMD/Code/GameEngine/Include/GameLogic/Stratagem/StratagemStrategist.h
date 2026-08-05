/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// StratagemStrategist.h //
// Project STRATAGEM - the per-AI-player Strategist (planner execution, P2.2).
//
// One of these is attached to a skirmish AI player (AIPlayer) when a Strategist is
// assigned. It owns the resolved profile, the derived knobs, a StratagemPlanner, and
// the current directive. Each think-interval it digests the world model into signals
// and re-decides; the AIPlayer then reads the directive/knobs to MODULATE its stock
// behavior (team cadence first, attack-commit + composition later).
//
// DETERMINISM: think() reads only the synced influence map + the AI's synced state,
// runs the pure planner, and stores a directive. No wall-clock, no RNG. If no
// Strategist is assigned (the default), the AI is unchanged stock behavior.

#pragma once

#include "GameLogic/Stratagem/StratagemPlanner.h"

class Player;

/// Sum of a player's ALIVE combat-unit build cost (excludes structures/dozers/harvesters/inert/dead).
/// The real "army strength" world signal (L0) - replaces the old frame-based placeholder. Crash-safe.
Real StratagemSumArmyValue( Player *me );

/// Count a player's ALIVE combat units sitting IDLE (same filter as SumArmyValue, plus
/// getAIUpdateInterface()->isIdle()). High idle count while losing = the turtle smoking gun (L2). Crash-safe.
Int StratagemCountIdleArmy( Player *me );

class StratagemStrategist
{
public:
	StratagemStrategist();

	/// Clone a base template (e.g. "tmpl.rommel@1") and derive its knobs. Returns false on unknown id.
	Bool assignTemplate( const AsciiString &templateId );
	/// Build a profile from an ARBITRARY 14-trait vector (for sweeps), then derive knobs.
	/// Trait order matches StratagemTraits (aggression..grudge). Returns false on null.
	Bool assignCustomTraits( const Real *traits14 );
	/// Build a profile from a base-template roster index (0..count-1) with the given
	/// competence (from the skirmish difficulty). For the lobby per-slot picker.
	Bool assignByRosterIndex( Int rosterIndex, StratagemDifficulty competence );
	/// Pick the source from the command line: custom traits (-stratagemTraits) if set,
	/// else the named template (-stratagemAI). Returns false if neither is usable.
	Bool assignFromCommandLine();
	Bool isValid() const { return m_valid; }
	const AsciiString &name() const { return m_profile.name; }

	/// Re-plan if the think-interval has elapsed: digest the world model for @p me into
	/// signals, decide, and cache the directive.
	void think( Player *me, UnsignedInt frame );

	const StratagemDirective     &directive() const { return m_directive; }
	const StratagemWorldSignals  &signals()   const { return m_signals; }
	const StratagemDecisionTrace &trace()     const { return m_trace; }     ///< L2 diagnostics (votes/pressure/floor)
	const StratagemPlannerParams &knobs()     const { return m_knobs; }
	Real teamCadenceScale() const { return m_knobs.teamCadenceScale; }
	UnsignedInt firstCommitFrame() const { return m_firstCommitFrame; }     ///< 0 until commitAttack first flips true

private:
	StratagemProfile        m_profile;
	StratagemPlannerParams  m_knobs;
	StratagemPlanner        m_planner;
	StratagemDirective      m_directive;
	StratagemWorldSignals   m_signals;
	StratagemDecisionTrace  m_trace;            ///< last decision's diagnostic trace (write-only from logic's view)
	UnsignedInt             m_firstCommitFrame; ///< first frame commitAttack became true (logging only)
	UnsignedInt             m_nextThinkFrame;
	Bool                    m_valid;
};

//-----------------------------------------------------------------------------
// Command-line config: which base template skirmish AIs adopt. Set by
// -stratagemAI <name> (e.g. "rommel"); empty means "none" -> stock AI unchanged.
// Backed by a plain BSS char buffer (NOT a GlobalData field, NOT an AsciiString):
// the value is written by the very-early startup command-line parse, before the
// memory/string pool is stable, so a pooled AsciiString stored then can be lost.
// A raw buffer is valid the instant it's written and survives memory-system init.
// The AsciiString is materialized lazily on read (after the pool is up).
//-----------------------------------------------------------------------------
void StratagemAISetTemplateId( const char *id );
AsciiString StratagemAIGetTemplateId();

//-----------------------------------------------------------------------------
// -stratagemSeed <n>: fixed match seed for the auto-capture harness (default 1337).
// Varying it gives independent deterministic samples (different factions/positions),
// which is what lets a fleet of -multiInstance runs cover persona x seed in parallel.
//-----------------------------------------------------------------------------
void StratagemAISetSeed( Int seed );
Int  StratagemAIGetSeed();

//-----------------------------------------------------------------------------
// -stratagemShots <n>: number of capture samples before the match auto-quits
// (default 16; sample frames are 150 + k*240). The regression/divergence checks use
// 16 (~frame 3750); fitness runs pass a larger n so the AIs reach actual COMBAT. The
// first 16 signatures are identical regardless of n, so the regression baseline holds.
//-----------------------------------------------------------------------------
void StratagemAISetShots( Int shots );
Int  StratagemAIGetShots();

// -stratagemSlotAI <idx>: test hook - stamp a Strategist roster index onto harness slot 1,
// to exercise the per-slot lobby assignment path headlessly. -1 = none.
void StratagemAISetSlotStrategist( Int idx );
Int  StratagemAIGetSlotStrategist();

// -stratagemFaction <name>: pin both harness AIs to a faction (e.g. "China"); empty = RANDOM.
// -stratagemOpponentAI <name>: the slot-2 opponent general (by name); empty = none.
void        StratagemAISetFaction( const char *name );
const char *StratagemAIGetFaction();
void        StratagemAISetOpponent( const char *name );
const char *StratagemAIGetOpponent();

//-----------------------------------------------------------------------------
// The -stratagemAI persona is given to the FIRST skirmish AI only (the "subject");
// every other skirmish AI stays stock = the baseline opponent. So a 2-AI harness
// match is persona-vs-baseline. Reset once per game before players are created.
//-----------------------------------------------------------------------------
void StratagemAIResetAssignment();
Bool StratagemAIClaimSubjectSlot();   ///< true exactly once (for the first skirmish AI)

//-----------------------------------------------------------------------------
// -stratagemTraits a,e,t,r,h,x,d,p,s,c,n,b,m,g : an ARBITRARY 14-value trait vector
// (StratagemTraits order: aggression, economy, techPriority, riskTolerance, harassment,
// expansion, defensiveness, adaptiveness, scouting, commitDiscipline, randomness,
// buildRigidity, tempo, grudge). This is the optimization search space - the sweep driver
// generates candidate vectors and runs each. Takes precedence over -stratagemAI.
//-----------------------------------------------------------------------------
void        StratagemAISetTraits( const Real *vals, Int n );
Bool        StratagemAIHasCustomTraits();
const Real *StratagemAIGetTraitsPtr();   ///< 14 values; valid only if HasCustomTraits()
/// True if EITHER a custom trait vector or a template name was given on the command line.
Bool        StratagemAIHasConfig();
