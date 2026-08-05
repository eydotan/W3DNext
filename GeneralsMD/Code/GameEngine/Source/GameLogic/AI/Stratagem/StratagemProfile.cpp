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

// StratagemProfile.cpp //
// Project STRATAGEM - PROFILE schema, layered resolver, hashing (task E1).
// See StratagemProfile.h for the design and determinism contract.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemProfile.h"

//-----------------------------------------------------------------------------
// small deterministic helpers (FNV-1a, quantized so float jitter can't change a hash)
//-----------------------------------------------------------------------------
static inline UnsignedInt fnvStep( UnsignedInt h, UnsignedInt v )
{
	h ^= v;
	h *= 16777619u;
	return h;
}

static inline UnsignedInt fnvReal( UnsignedInt h, Real v )
{
	// quantize to 1/1000 - finer than any meaningful trait/competence step
	return fnvStep( h, (UnsignedInt)(Int)(v * 1000.0f) );
}

static inline UnsignedInt fnvStr( UnsignedInt h, const AsciiString &s )
{
	const char *p = s.str();
	h = fnvStep( h, (UnsignedInt)s.getLength() );
	for ( ; p && *p; ++p )
		h = fnvStep( h, (UnsignedInt)(UnsignedByte)*p );
	return h;
}

static inline Real clampReal( Real v, Real lo, Real hi )
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

//-----------------------------------------------------------------------------
// StratagemTraits
//-----------------------------------------------------------------------------
void StratagemTraits::zero()
{
	aggression = economy = techPriority = riskTolerance = harassment = 0.0f;
	expansion = defensiveness = adaptiveness = scouting = commitDiscipline = 0.0f;
	randomness = buildRigidity = tempo = grudge = 0.0f;
}

void StratagemTraits::addInPlace( const StratagemTraits &d )
{
	aggression += d.aggression;     economy += d.economy;         techPriority += d.techPriority;
	riskTolerance += d.riskTolerance; harassment += d.harassment; expansion += d.expansion;
	defensiveness += d.defensiveness; adaptiveness += d.adaptiveness; scouting += d.scouting;
	commitDiscipline += d.commitDiscipline; randomness += d.randomness; buildRigidity += d.buildRigidity;
	tempo += d.tempo;               grudge += d.grudge;
}

void StratagemTraits::clamp01()
{
	aggression = clampReal(aggression,0,1);   economy = clampReal(economy,0,1);
	techPriority = clampReal(techPriority,0,1); riskTolerance = clampReal(riskTolerance,0,1);
	harassment = clampReal(harassment,0,1);   expansion = clampReal(expansion,0,1);
	defensiveness = clampReal(defensiveness,0,1); adaptiveness = clampReal(adaptiveness,0,1);
	scouting = clampReal(scouting,0,1);       commitDiscipline = clampReal(commitDiscipline,0,1);
	randomness = clampReal(randomness,0,1);   buildRigidity = clampReal(buildRigidity,0,1);
	tempo = clampReal(tempo,0,1);             grudge = clampReal(grudge,0,1);
}

//-----------------------------------------------------------------------------
// StratagemCompetence
//-----------------------------------------------------------------------------
void StratagemCompetence::clampLegal()
{
	if (level < STRAT_DIFF_EASY) level = STRAT_DIFF_EASY;
	if (level >= STRAT_DIFF_COUNT) level = STRAT_DIFF_BRUTAL;
	thinkInterval   = clampReal(thinkInterval, 0.2f, 10.0f);
	reactionLatency = clampReal(reactionLatency, 0.0f, 10.0f);
	scoutAccuracy   = clampReal(scoutAccuracy, 0.0f, 1.0f);
	mistakeRate     = clampReal(mistakeRate, 0.0f, 1.0f);
	if (microTier < 1) microTier = 1;
	if (microTier > 16) microTier = 16;
	if (repertoireDepth < 1) repertoireDepth = 1;
	if (repertoireDepth > 64) repertoireDepth = 64;
}

StratagemCompetence StratagemMakeCompetence( StratagemDifficulty level )
{
	// Decision-quality only. NO resource/health/vision handicaps - "Brutal" is an
	// elite-but-fallible human, not a cheater.
	StratagemCompetence c;
	c.level = level;
	switch (level)
	{
		case STRAT_DIFF_EASY:
			c.thinkInterval = 4.0f; c.reactionLatency = 3.0f; c.scoutAccuracy = 0.40f;
			c.mistakeRate = 0.35f;  c.microTier = 1;          c.repertoireDepth = 2;
			break;
		case STRAT_DIFF_HARD:
			c.thinkInterval = 1.5f; c.reactionLatency = 0.8f; c.scoutAccuracy = 0.80f;
			c.mistakeRate = 0.08f;  c.microTier = 3;          c.repertoireDepth = 6;
			break;
		case STRAT_DIFF_BRUTAL:
			c.thinkInterval = 0.8f; c.reactionLatency = 0.3f; c.scoutAccuracy = 0.95f;
			c.mistakeRate = 0.02f;  c.microTier = 5;          c.repertoireDepth = 10;
			break;
		case STRAT_DIFF_NORMAL:
		default:
			c.level = STRAT_DIFF_NORMAL;
			c.thinkInterval = 2.5f; c.reactionLatency = 1.5f; c.scoutAccuracy = 0.60f;
			c.mistakeRate = 0.18f;  c.microTier = 2;          c.repertoireDepth = 4;
			break;
	}
	c.clampLegal();
	return c;
}

//-----------------------------------------------------------------------------
// StratagemProfile
//-----------------------------------------------------------------------------
void StratagemProfile::clampToLegalRanges()
{
	traits.clamp01();
	competence.clampLegal();
}

UnsignedInt StratagemProfile::contentHash() const
{
	UnsignedInt h = 2166136261u;   // FNV offset basis
	h = fnvStr( h, name );
	h = fnvStr( h, baseTemplateId );
	h = fnvStep( h, baseTemplateHash );
	// traits (fixed order - matches the struct/INI field order)
	h = fnvReal(h, traits.aggression);      h = fnvReal(h, traits.economy);
	h = fnvReal(h, traits.techPriority);    h = fnvReal(h, traits.riskTolerance);
	h = fnvReal(h, traits.harassment);      h = fnvReal(h, traits.expansion);
	h = fnvReal(h, traits.defensiveness);   h = fnvReal(h, traits.adaptiveness);
	h = fnvReal(h, traits.scouting);        h = fnvReal(h, traits.commitDiscipline);
	h = fnvReal(h, traits.randomness);      h = fnvReal(h, traits.buildRigidity);
	h = fnvReal(h, traits.tempo);           h = fnvReal(h, traits.grudge);
	// competence
	h = fnvStep(h, (UnsignedInt)competence.level);
	h = fnvReal(h, competence.thinkInterval);   h = fnvReal(h, competence.reactionLatency);
	h = fnvReal(h, competence.scoutAccuracy);   h = fnvReal(h, competence.mistakeRate);
	h = fnvStep(h, (UnsignedInt)competence.microTier);
	h = fnvStep(h, (UnsignedInt)competence.repertoireDepth);
	h = fnvStr( h, repertoireRef );
	h = fnvStep(h, (UnsignedInt)provenance);
	// NOTE: description and emblemColor are deliberately NOT hashed - they are pure
	// presentation, so editing a bio or accent color must not change the profile's
	// behavioral content-address or invalidate a clone's rebase pin.
	return h;
}

//-----------------------------------------------------------------------------
// The resolver - pure function.
//-----------------------------------------------------------------------------
StratagemProfile StratagemResolveProfile( const StratagemProfile &base,
                                          const StratagemTraits *layers,
                                          Int numLayers )
{
	StratagemProfile r = base;   // identity, competence, refs inherit from the base
	for (Int i = 0; i < numLayers; ++i)
		r.traits.addInPlace( layers[i] );   // field-wise additive compose, in order
	r.clampToLegalRanges();                 // enforce legal ranges on the flattened result
	return r;
}

#if defined(RTS_DEBUG)
//-----------------------------------------------------------------------------
void StratagemProfileSelfTest()
{
	// Base ("Giap"-ish), a persistent "be a rusher" delta, and a "try riskier" trial.
	StratagemProfile base;
	base.name = "SelfTestBase";
	base.baseTemplateId = "tmpl.giap@1";
	base.baseTemplateHash = 0;
	base.traits.zero();
	base.traits.aggression = 0.5f;  base.traits.economy = 0.5f;  base.traits.scouting = 0.5f;
	base.traits.adaptiveness = 0.6f; base.traits.commitDiscipline = 0.5f;
	base.competence = StratagemMakeCompetence( STRAT_DIFF_HARD );
	base.repertoireRef = "rep.default";
	base.provenance = STRAT_PROV_AUTHORED;
	base.description = "self-test base";
	base.emblemColor = 0;
	base.baseTemplateHash = base.contentHash();

	StratagemTraits delta;  delta.zero();
	delta.aggression = 0.4f;  delta.harassment = 0.5f;  delta.tempo = 0.3f;  delta.economy = -0.2f;
	StratagemTraits trial;  trial.zero();
	trial.riskTolerance = 0.5f;

	StratagemTraits layers[2] = { delta, trial };
	StratagemProfile resolved = StratagemResolveProfile( base, layers, 2 );

	DEBUG_LOG(("STRATAGEM E1 self-test: baseHash=%08x resolvedHash=%08x | aggression=%.2f harass=%.2f econ=%.2f risk=%.2f difficulty=%d",
		base.contentHash(), resolved.contentHash(),
		resolved.traits.aggression, resolved.traits.harassment, resolved.traits.economy,
		resolved.traits.riskTolerance, (Int)resolved.competence.level));
}
#endif
