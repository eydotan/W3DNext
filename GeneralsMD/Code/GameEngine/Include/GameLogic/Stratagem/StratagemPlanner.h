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

// StratagemPlanner.h //
// Project STRATAGEM - the Phase-2 Strategic Planner, DECISION layer (first increment).
//
// The planner is the brain that finally makes a personality matter: it reads a
// Strategist's derived knobs (StratagemPlannerParams, from the trait vector) plus a
// digest of the world model (StratagemWorldSignals, from the influence map + economy)
// and chooses a macro POSTURE and whether to commit an attack. Two Strategists handed
// the SAME battlefield choose differently because their knobs differ.
//
// This increment implements the decision (intent). EXECUTION - turning a directive into
// actual unit/build commands on the AISkirmishPlayer - is the next step; the directive
// is the contract between the two.
//
// DETERMINISM: decide() is a pure function of (knobs, signals). No wall-clock, no RNG,
// no globals. Resolve the profile once at match setup; the planner re-decides each
// think-interval from synced world signals.

#pragma once

#include "GameLogic/Stratagem/StratagemPlannerParams.h"

//-----------------------------------------------------------------------------
enum StratagemPosture CPP_11(: Int)
{
	POSTURE_EXPAND = 0,   ///< take map control / grab supplies
	POSTURE_TECH,         ///< tech up behind safety
	POSTURE_MASS,         ///< build a main army
	POSTURE_DEFEND,       ///< fortify / hold against pressure
	POSTURE_HARASS,       ///< raid / deny economy
	POSTURE_ALLIN,        ///< commit everything
	POSTURE_COUNT
};

const char *StratagemPostureName( StratagemPosture p );

//-----------------------------------------------------------------------------
// A digest of the world model the planner reasons over. Populated from the
// influence map + economy each think-interval. All normalized.
//-----------------------------------------------------------------------------
struct StratagemWorldSignals
{
	Real ownControl;      ///< 0..1 - fraction of contested map influence we hold
	Real threatAtBase;    ///< 0..1 - enemy pressure on our base
	Real armyReadiness;   ///< 0..1 - REAL banked army strength vs. a "ready to commit" amount (L0)
	Real economyLead;     ///< -1..1 - our economy vs. the enemy's
	Real gameProgress;    ///< 0..1 - how late in the match (frame / lategame); drives the offense floor (L1)

	void clear() { ownControl = 0.5f; threatAtBase = 0.0f; armyReadiness = 0.0f; economyLead = 0.0f; gameProgress = 0.0f; }
};

//-----------------------------------------------------------------------------
struct StratagemDirective
{
	StratagemPosture posture;     ///< the chosen macro stance this think
	Bool  commitAttack;           ///< commit the banked army now?
	Real  confidence;             ///< utility margin of the winner over the runner-up
};

//-----------------------------------------------------------------------------
// A read-only DIAGNOSTIC trace of a single decide() (L2 telemetry). Pure, deterministic;
// it is WRITE-ONLY from synced logic's perspective (only logging reads it), so it never
// influences behavior. decideVerbose() fills it; decide() ignores it (passes nullptr).
//-----------------------------------------------------------------------------
struct StratagemDecisionTrace
{
	Real votes[POSTURE_COUNT];  ///< the 6 raw posture utilities (pre-argmax)
	Real pressure;              ///< commit pressure P (after base-threat suppression)
	Real commitFloor;           ///< the commitReleaseThresh the pressure trigger compared against
	Bool trigEager;             ///< aggressive posture + force concentration met
	Bool trigPressure;          ///< pressure >= floor AND army >= minCommitArmy
	Bool trigBackstop;          ///< late-game guarantee: gameProgress >= 0.90 AND army > 0.10

	void clear()
	{
		for (Int i = 0; i < POSTURE_COUNT; ++i) votes[i] = 0.0f;
		pressure = 0.0f; commitFloor = 0.0f;
		trigEager = trigPressure = trigBackstop = false;
	}
};

//-----------------------------------------------------------------------------
class StratagemPlanner
{
public:
	StratagemPlanner();

	/// Resolve + cache the planner knobs for a Strategist profile (do once at setup).
	void setProfile( const StratagemProfile &profile );
	const StratagemPlannerParams &knobs() const { return m_knobs; }

	/// Pure decision: pick the highest-utility posture for these world signals.
	StratagemDirective decide( const StratagemWorldSignals &signals ) const;

	/// Same decision, additionally filling an optional diagnostic trace (L2). Pure; if
	/// @p trace is non-null it receives the posture votes + offense-floor internals.
	StratagemDirective decideVerbose( const StratagemWorldSignals &signals, StratagemDecisionTrace *trace ) const;

private:
	StratagemPlannerParams m_knobs;
};

#if defined(RTS_DEBUG)
/// Drive the four archetypes through a calm and a threatened scenario; DEBUG_LOG the
/// directives to show they decide differently (and respond to the world) (planner increment).
void StratagemPlannerSelfTest();
#endif
