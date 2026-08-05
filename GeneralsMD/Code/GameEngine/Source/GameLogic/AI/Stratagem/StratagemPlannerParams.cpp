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

// StratagemPlannerParams.cpp //
// Project STRATAGEM - trait -> planner-knob binding (task E4).
// See StratagemPlannerParams.h. The numeric coefficients here are the design's
// opinion about how each personality trait should shape strategic behavior; they
// are deliberately simple, legible, and deterministic.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemPlannerParams.h"

static inline Real clamp01( Real v )
{
	if (v < 0.0f) return 0.0f;
	if (v > 1.0f) return 1.0f;
	return v;
}

static inline UnsignedInt fnvStep( UnsignedInt h, UnsignedInt v ) { h ^= v; h *= 16777619u; return h; }
static inline UnsignedInt fnvReal( UnsignedInt h, Real v ) { return fnvStep( h, (UnsignedInt)(Int)(v * 1000.0f) ); }

//-----------------------------------------------------------------------------
UnsignedInt StratagemPlannerParams::contentHash() const
{
	UnsignedInt h = 2166136261u;
	h = fnvReal(h, wExpand); h = fnvReal(h, wTech);   h = fnvReal(h, wMass);
	h = fnvReal(h, wDefend); h = fnvReal(h, wHarass); h = fnvReal(h, wAllIn);
	h = fnvStep(h, (UnsignedInt)thinkIntervalFrames);
	h = fnvReal(h, forceConcentration);
	h = fnvReal(h, commitReleaseThresh); h = fnvReal(h, minCommitArmy); h = fnvReal(h, patienceBias);
	h = fnvReal(h, routingEntropy);
	h = fnvReal(h, scoutWeight); h = fnvReal(h, counterBias); h = fnvReal(h, mistakeRate);
	h = fnvReal(h, teamCadenceScale);
	return h;
}

//-----------------------------------------------------------------------------
// The mapping. Pure arithmetic over the resolved profile's traits + competence.
//-----------------------------------------------------------------------------
StratagemPlannerParams StratagemDeriveParams( const StratagemProfile &profile )
{
	const StratagemTraits &t = profile.traits;
	StratagemPlannerParams k;

	// --- macro utility weights ---
	k.wExpand = clamp01( t.expansion * 0.7f + t.economy * 0.4f );
	k.wTech   = clamp01( t.techPriority );
	k.wMass   = clamp01( t.aggression * 0.5f + t.economy * 0.3f + 0.2f );
	k.wDefend = clamp01( t.defensiveness );
	k.wHarass = clamp01( t.harassment * 0.8f + t.aggression * 0.3f );
	k.wAllIn  = clamp01( t.riskTolerance * 0.7f + t.aggression * 0.3f - t.defensiveness * 0.3f );

	// --- cadence: competence think interval, sharpened by high tempo ---
	Real secs = profile.competence.thinkInterval * (1.3f - 0.6f * t.tempo);
	if (secs < 0.2f) secs = 0.2f;
	k.thinkIntervalFrames = (Int)(secs * 30.0f);   // LOGICFRAMES_PER_SECOND

	// --- commitment: discipline banks force, risk tolerance commits sooner ---
	k.forceConcentration = clamp01( 0.25f + t.commitDiscipline * 0.6f - t.riskTolerance * 0.25f );

	// --- offense floor (L1): every personality must eventually strike (the user's "no
	// personality has 0 offense" rule). These knobs only DELAY a defensive commit; they
	// never cancel it (the planner's late-game backstop is a fixed guarantee in decide()).
	//   * commitReleaseThresh: pressure needed to release. Rises with defensiveness so a
	//     turtle banks longer, but HARD-CAPPED at 0.80 so the pressure trigger stays
	//     reachable for a maxed army in a long game. (A trait FLOOR, if ever wanted, lives
	//     here as a local read - NEVER in clampToLegalRanges(), which would corrupt the
	//     additive deltas + the content hash.)
	//   * minCommitArmy: don't dribble a token force into the pressure trigger; banks more
	//     for disciplined personalities, less for the risk-tolerant.
	//   * patienceBias: how much commit pressure leans on "the match is getting late" vs
	//     "my army is ready". Patient/defensive generals lean on time; aggressors on army.
	k.commitReleaseThresh = clamp01( 0.45f + t.defensiveness * 0.50f );
	if (k.commitReleaseThresh > 0.80f) k.commitReleaseThresh = 0.80f;
	k.minCommitArmy = clamp01( 0.25f + t.commitDiscipline * 0.35f - t.riskTolerance * 0.15f );
	k.patienceBias  = clamp01( 0.20f + t.defensiveness * 0.40f + t.commitDiscipline * 0.20f );

	// --- routing variety / scouting / counter-building ---
	k.routingEntropy = clamp01( t.randomness * 0.6f + t.adaptiveness * 0.3f );
	k.scoutWeight    = clamp01( t.scouting );
	k.counterBias    = clamp01( t.adaptiveness * 0.8f + t.scouting * 0.2f );

	// --- humanlike error carried straight from competence ---
	k.mistakeRate = clamp01( profile.competence.mistakeRate );

	// --- team-build cadence: aggressive/high-tempo pumps teams faster; turtles slower ---
	Real cadence = 1.35f - 0.60f * t.aggression - 0.25f * t.tempo + 0.35f * t.defensiveness;
	if (cadence < 0.5f) cadence = 0.5f;
	if (cadence > 1.6f) cadence = 1.6f;
	k.teamCadenceScale = cadence;

	return k;
}

#if defined(RTS_DEBUG)
#include "GameLogic/Stratagem/StratagemTemplates.h"

static void logParams( const char *who, const StratagemProfile &p )
{
	StratagemPlannerParams k = StratagemDeriveParams( p );
	DEBUG_LOG(("STRATAGEM E4 %-9s hash=%08x | wExpand=%.2f wMass=%.2f wDefend=%.2f wHarass=%.2f wAllIn=%.2f | think=%df conc=%.2f route=%.2f counter=%.2f | release=%.2f minArmy=%.2f patience=%.2f",
		who, k.contentHash(), k.wExpand, k.wMass, k.wDefend, k.wHarass, k.wAllIn,
		k.thinkIntervalFrames, k.forceConcentration, k.routingEntropy, k.counterBias,
		k.commitReleaseThresh, k.minCommitArmy, k.patienceBias));
}

void StratagemPlannerParamsSelfTest()
{
	// Derive knobs for three very different archetypes; the divergence should be legible.
	StratagemProfile p;
	if (StratagemCloneTemplate( "tmpl.rommel@1",     &p )) logParams( "Rommel",     p );
	if (StratagemCloneTemplate( "tmpl.montgomery@1", &p )) logParams( "Montgomery", p );
	if (StratagemCloneTemplate( "tmpl.eisenhower@1", &p )) logParams( "Eisenhower", p );
	if (StratagemCloneTemplate( "tmpl.giap@1",       &p )) logParams( "Giap",       p );
}
#endif
