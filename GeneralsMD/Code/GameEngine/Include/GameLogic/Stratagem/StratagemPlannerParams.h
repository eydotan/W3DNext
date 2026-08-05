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

// StratagemPlannerParams.h //
// Project STRATAGEM - the trait -> planner-knob binding (task E4; see
// STRATAGEM_PERSONALITIES.md).
//
// This is the bridge between a Strategist's PERSONALITY (the trait vector on a
// resolved StratagemProfile) and the concrete inputs the future Phase-2 Strategic
// Planner will read: macro-decision utility weights, think cadence, force-
// concentration (commit) threshold, attack-routing entropy, scouting investment,
// counter-build bias, and the humanlike mistake rate. The planner that consumes
// these is Phase 2; E4 establishes the mapping so two profiles differing only in
// trait data produce legibly different planner behavior.
//
// DETERMINISM: StratagemDeriveParams() is a pure function of the resolved profile
// (no wall-clock, no RNG, no globals). Resolve once at match setup, hand the planner
// frozen knobs. contentHash() lets the params join the replay/save fingerprint.

#pragma once

#include "GameLogic/Stratagem/StratagemProfile.h"

//-----------------------------------------------------------------------------
struct StratagemPlannerParams
{
	// Macro-decision utility weights (relative, ~0..1). Higher = more inclined.
	Real wExpand;            ///< expand / take map control
	Real wTech;              ///< tech up
	Real wMass;              ///< mass army & push
	Real wDefend;            ///< fortify / static defense
	Real wHarass;            ///< harass / deny economy
	Real wAllIn;             ///< commit everything

	// Cadence & commitment
	Int  thinkIntervalFrames; ///< logic frames between strategic re-plans (lower = sharper)
	Real forceConcentration;  ///< 0..1 - how much army to bank before committing (vs. dribble)
	// Offense floor (L1): guarantees every personality mounts an army-backed attack eventually.
	Real commitReleaseThresh; ///< 0..0.80 - commit-pressure needed to release (rises w/ defensiveness, HARD-capped 0.80)
	Real minCommitArmy;       ///< 0..1 - minimum banked army before the pressure trigger may fire
	Real patienceBias;        ///< 0..1 - pressure weight on "match is late" vs "army is ready" (patient = higher)
	Real routingEntropy;      ///< 0..1 - variability of attack routes (drives findWeakestApproach use)
	Real scoutWeight;         ///< 0..1 - scouting investment
	Real counterBias;         ///< 0..1 - how strongly to counter the scouted enemy composition
	Real mistakeRate;         ///< 0..1 - humanlike error rate (carried from competence)
	Real teamCadenceScale;    ///< multiplier on the AI's team-build timer (<1 faster, >1 slower)

	/// Deterministic content hash of the derived knobs (for the replay/save fingerprint).
	UnsignedInt contentHash() const;
};

/// Pure mapping: resolved profile -> planner knobs. Same profile -> same knobs, always.
StratagemPlannerParams StratagemDeriveParams( const StratagemProfile &profile );

#if defined(RTS_DEBUG)
/// Derive knobs for a few archetypes and DEBUG_LOG them, showing legible divergence (E4).
void StratagemPlannerParamsSelfTest();
#endif
