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

// StratagemProfile.h //
// Project STRATAGEM - the personality/competence PROFILE schema and the
// deterministic layered resolver (task E1; see STRATAGEM_PERSONALITIES.md).
//
// A Strategist is "git for personalities": an immutable BASE template, plus an
// accumulating persistent DELTA, plus zero or more ephemeral TRIAL layers. This
// file defines the RESOLVED, frozen profile the planner consumes, and the pure
// function that flattens the layers into it:
//
//     resolved = clamp( flatten( base (+) deltas (+) trials ) )
//
// DETERMINISM CONTRACT: resolveProfile() is a pure function of its inputs (no
// wall-clock, no RNG, no globals). The same layers always produce the same
// StratagemProfile and the same contentHash(). The resolved profile is inert
// DATA, resolved ONCE at match setup and handed to the deterministic planner;
// in-match behavior never mutates it. contentHash() is what the replay/save
// header records (Phase-0 task B4) and what multiplayer verifies across clients.
//
// E1 scope: the schema, the resolver, clamping, hashing, and the competence
// presets (difficulty as decision-quality, never cheats). The 5 shipped base
// templates and INI/JSON loading are E2.

#pragma once

#include "Common/GameType.h"      // Int, Real, UnsignedInt
#include "Common/AsciiString.h"

//-----------------------------------------------------------------------------
// Trait vector - HOW a Strategist plays (style). Each is normalized 0..1 in a
// resolved profile; in a delta/trial layer the same fields are SIGNED additive
// adjustments. Order is fixed and load-bearing (it defines the hash and the
// INI/JSON field order). multitaskCeiling was intentionally removed - "how many
// fronts it wants" is harassment (intent); "how many it can run" is
// competence.microTier (ability). See STRATAGEM_PERSONALITIES.md.
//-----------------------------------------------------------------------------
struct StratagemTraits
{
	Real aggression;        ///< passive .. hyper-aggressive
	Real economy;           ///< emphasis on booming / worker count
	Real techPriority;      ///< stay low-tech .. rush high-tech
	Real riskTolerance;     ///< cautious .. all-in willing
	Real harassment;        ///< number of fronts / economy-denial intent
	Real expansion;         ///< map-control / expand drive
	Real defensiveness;     ///< turtling / static-defense emphasis
	Real adaptiveness;      ///< willingness to change plan on what it scouts (intent)
	Real scouting;          ///< scouting diligence
	Real commitDiscipline;  ///< bank force & commit at a timing window vs. dribble
	Real randomness;        ///< deliberate unpredictability injected into choices
	Real buildRigidity;     ///< fixed build order vs. flexible
	Real tempo;             ///< patience .. impatience / speed of play
	Real grudge;            ///< focus-fire / hold-a-grudge against a target

	void zero();                          ///< all fields 0 (neutral delta)
	void addInPlace( const StratagemTraits &delta );  ///< field-wise += (layer compose)
	void clamp01();                       ///< clamp every field to [0,1]
};

//-----------------------------------------------------------------------------
// Difficulty as COMPETENCE, not handicaps. These knobs change decision QUALITY
// and humanlike execution limits - never resources, health, or vision cheats.
// Orthogonal to traits: a "Rusher" exists at every competence level.
//-----------------------------------------------------------------------------
enum StratagemDifficulty CPP_11(: Int)
{
	STRAT_DIFF_EASY = 0,
	STRAT_DIFF_NORMAL,
	STRAT_DIFF_HARD,
	STRAT_DIFF_BRUTAL,     ///< plays like a strong human - deep repertoire, sound timing, NO cheats
	STRAT_DIFF_COUNT
};

struct StratagemCompetence
{
	StratagemDifficulty level;
	Real thinkInterval;     ///< seconds between strategic re-plans (lower = sharper)
	Real reactionLatency;   ///< seconds before it acts on newly-scouted info
	Real scoutAccuracy;     ///< 0..1 completeness of its honest world knowledge
	Real mistakeRate;       ///< 0..1 chance of a deliberately suboptimal pick (humanlike)
	Int  microTier;         ///< humanlike cap on simultaneously-managed groups
	Int  repertoireDepth;   ///< how many distinct plans it draws from

	void clampLegal();
};

/// Build the competence knobs for a difficulty level (decision-quality only, no cheats).
StratagemCompetence StratagemMakeCompetence( StratagemDifficulty level );

//-----------------------------------------------------------------------------
enum StratagemProvenance CPP_11(: Int)
{
	STRAT_PROV_AUTHORED = 0,   ///< hand-authored base template
	STRAT_PROV_LEARNED,        ///< delta grown by the offline learning loop
	STRAT_PROV_MIMICKED        ///< style captured from a player's replays
};

//-----------------------------------------------------------------------------
// The RESOLVED profile: the inert, frozen data the planner reads. Produced by
// resolveProfile() and never mutated in-match.
//-----------------------------------------------------------------------------
struct StratagemProfile
{
	AsciiString			name;             ///< display identity
	AsciiString			baseTemplateId;   ///< "tmpl.rommel@1" etc. - what it resolved from
	UnsignedInt			baseTemplateHash; ///< content hash of the base it pinned (rebase guard)
	StratagemTraits		traits;
	StratagemCompetence	competence;
	AsciiString			repertoireRef;    ///< id of the plan repertoire (resolved separately)
	StratagemProvenance	provenance;

	// Presentation-only (NOT part of contentHash - flavor must not invalidate a
	// Strategist's behavioral identity / rebase pin):
	AsciiString			description;      ///< short bio for the commander-select UI
	UnsignedInt			emblemColor;      ///< packed 0x00RRGGBB identity/accent color for the UI

	void clampToLegalRanges();
	/// Deterministic content hash - identical profiles -> identical hash, on every
	/// machine and replay. Recorded in the replay/save header (Phase-0 task B4).
	UnsignedInt contentHash() const;
};

//-----------------------------------------------------------------------------
// The resolver. Pure function: flatten the base with the additive trait layers
// (persistent delta first, then ephemeral trials in push order), then clamp.
// @param base    the immutable base template (already a resolved-shape profile)
// @param layers  additive trait adjustments (delta + active trials), applied in order
// @param numLayers  count of layers
//-----------------------------------------------------------------------------
StratagemProfile StratagemResolveProfile( const StratagemProfile &base,
                                          const StratagemTraits *layers,
                                          Int numLayers );

#if defined(RTS_DEBUG)
/// Build base+delta+trial example profiles, resolve, and DEBUG_LOG the hashes and a
/// few resolved values. Same output every run == the resolver is a pure function (E1).
void StratagemProfileSelfTest();
#endif
