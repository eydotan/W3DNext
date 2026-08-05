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

// StratagemStrategist.cpp //
// Project STRATAGEM - per-AI-player Strategist (planner execution). See header.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemStrategist.h"

#include "GameLogic/Stratagem/StratagemBrain.h"       // TheStratagemBrain + influence map
#include "GameLogic/Stratagem/StratagemTemplates.h"   // base-template lookup / clone
#include "Common/Player.h"
#include "Common/Money.h"
#include "Common/KindOf.h"          // KINDOF_* for the army-value filter
#include "Common/ThingTemplate.h"   // friend_getBuildCost()
#include "GameLogic/Object.h"       // Object::isKindOf / getTemplate / isEffectivelyDead / getAIUpdateInterface
#include "GameLogic/Module/AIUpdate.h"  // AIUpdateInterface::isIdle() for the idle-army metric (L2)

//-----------------------------------------------------------------------------
// L0 - the REAL army-strength signal (replaces the old frame-based placeholder).
// Tuning constants (see STRATAGEM_TRAINING.md, L0):
static const Real STRAT_READY_ARMY_VALUE = 6000.0f;   // own combat-unit build-cost that reads as "ready to commit"
static const Real STRAT_LATEGAME_FRAMES  = 10000.0f;  // frame at which gameProgress -> 1.0 (~5.5 min)

namespace {
	struct StratArmyAccum { Real value; };
	// iterateObjects callback: sum ALIVE combat-unit build cost. Crash-safe - skips dead + non-combat
	// objects (structures/dozers/harvesters/inert) by isKindOf BEFORE touching the template, so a
	// bad/missing template (e.g. the map's neutral "Tech Center") is never dereferenced; then null-guards tt.
	static void stratArmyAccum( Object *obj, void *ud )
	{
		if (obj == nullptr || ud == nullptr) return;
		if (obj->isEffectivelyDead()) return;
		if (obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_DOZER )
				|| obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_INERT )) return;
		const ThingTemplate *tt = obj->getTemplate();
		if (tt == nullptr) return;
		((StratArmyAccum *)ud)->value += (Real)tt->friend_getBuildCost();
	}
}

Real StratagemSumArmyValue( Player *me )
{
	if (me == nullptr) return 0.0f;
	StratArmyAccum acc; acc.value = 0.0f;
	me->iterateObjects( stratArmyAccum, &acc );
	return acc.value;
}

namespace {
	struct StratIdleAccum { Int count; };
	// iterateObjects callback: count ALIVE combat units that are AI-idle. Same crash-safe filter
	// as stratArmyAccum (skip dead + structures/dozers/harvesters/inert before touching anything),
	// then null-guard the AI interface. Idle army while the match is lost = the turtle smoking gun.
	static void stratIdleAccum( Object *obj, void *ud )
	{
		if (obj == nullptr || ud == nullptr) return;
		if (obj->isEffectivelyDead()) return;
		if (obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_DOZER )
				|| obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_INERT )) return;
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai != nullptr && ai->isIdle())
			((StratIdleAccum *)ud)->count++;
	}
}

Int StratagemCountIdleArmy( Player *me )
{
	if (me == nullptr) return 0;
	StratIdleAccum acc; acc.count = 0;
	me->iterateObjects( stratIdleAccum, &acc );
	return acc.count;
}

//-----------------------------------------------------------------------------
// command-line config (free global, not GlobalData -> no PCH invalidation).
// Stored in a plain BSS buffer rather than an AsciiString: -stratagemAI is parsed
// in the very-early startup pass (before the string pool is stable), and a pooled
// AsciiString written that early does not reliably survive to player-creation time.
// A raw char buffer is valid immediately and is never touched by memory-system init.
//-----------------------------------------------------------------------------
static char s_stratagemAITemplateId[64] = { 0 };

void StratagemAISetTemplateId( const char *id )
{
	if (id == nullptr) id = "";
	Int i = 0;
	for (; id[i] != 0 && i < 63; ++i) s_stratagemAITemplateId[i] = id[i];
	s_stratagemAITemplateId[i] = 0;
}

AsciiString StratagemAIGetTemplateId()
{
	return AsciiString( s_stratagemAITemplateId );	// materialize lazily, pool is up by read time
}

// -stratagemSeed: plain POD global (safe to set in the early startup parse).
static Int s_stratagemSeed = 1337;
void StratagemAISetSeed( Int seed ) { s_stratagemSeed = seed; }
Int  StratagemAIGetSeed() { return s_stratagemSeed; }

// -stratagemShots: match length in capture samples (default 16).
static Int s_stratagemShots = 16;
void StratagemAISetShots( Int shots ) { s_stratagemShots = (shots > 0) ? shots : 16; }
Int  StratagemAIGetShots() { return s_stratagemShots; }

// -stratagemSlotAI: test hook - the harness stamps this roster index onto skirmish slot 1
// (the "subject"), exercising the per-slot lobby path headlessly. -1 = none.
static Int s_stratagemSlotStrategist = -1;
void StratagemAISetSlotStrategist( Int idx ) { s_stratagemSlotStrategist = idx; }
Int  StratagemAIGetSlotStrategist() { return s_stratagemSlotStrategist; }

// -stratagemFaction <name>: pin both harness AIs to a faction ("China" -> FactionChina). Empty = RANDOM.
static char s_stratagemFaction[64] = { 0 };
void StratagemAISetFaction( const char *name )
{
	if (name == nullptr) name = "";
	Int i = 0; for (; name[i] != 0 && i < 63; ++i) s_stratagemFaction[i] = name[i];
	s_stratagemFaction[i] = 0;
}
const char *StratagemAIGetFaction() { return s_stratagemFaction; }

// -stratagemOpponentAI <name>: slot-2 opponent general (resolved to a roster index in the harness). Empty = none.
static char s_stratagemOpponent[64] = { 0 };
void StratagemAISetOpponent( const char *name )
{
	if (name == nullptr) name = "";
	Int i = 0; for (; name[i] != 0 && i < 63; ++i) s_stratagemOpponent[i] = name[i];
	s_stratagemOpponent[i] = 0;
}
const char *StratagemAIGetOpponent() { return s_stratagemOpponent; }

// Persona-to-first-AI assignment (persona vs baseline). One game per harness process,
// but reset anyway for correctness in case a process ever runs more than one match.
static Int s_stratagemAssignCount = 0;
void StratagemAIResetAssignment() { s_stratagemAssignCount = 0; }
Bool StratagemAIClaimSubjectSlot() { return (s_stratagemAssignCount++ == 0); }

// -stratagemTraits: an arbitrary 14-value trait vector (the sweep search space). POD globals.
static Bool s_stratagemHasCustomTraits = false;
static Real s_stratagemCustomTraits[14] = { 0 };
void StratagemAISetTraits( const Real *vals, Int n )
{
	if (vals == nullptr || n < 14) { s_stratagemHasCustomTraits = false; return; }
	for (Int i = 0; i < 14; ++i) s_stratagemCustomTraits[i] = vals[i];
	s_stratagemHasCustomTraits = true;
}
Bool StratagemAIHasCustomTraits() { return s_stratagemHasCustomTraits; }
const Real *StratagemAIGetTraitsPtr() { return s_stratagemCustomTraits; }
Bool StratagemAIHasConfig() { return s_stratagemHasCustomTraits || !StratagemAIGetTemplateId().isEmpty(); }

//-----------------------------------------------------------------------------
StratagemStrategist::StratagemStrategist() :
	m_firstCommitFrame(0),
	m_nextThinkFrame(0),
	m_valid(false)
{
	m_directive.posture = POSTURE_EXPAND;
	m_directive.commitAttack = FALSE;
	m_directive.confidence = 0.0f;
	m_signals.clear();
	m_trace.clear();
}

//-----------------------------------------------------------------------------
Bool StratagemStrategist::assignTemplate( const AsciiString &templateId )
{
	m_valid = false;
	if (templateId.isEmpty())
		return false;

	// Accept either a full id ("tmpl.rommel@1") or a short name ("rommel").
	AsciiString id = templateId;
	if (StratagemGetBaseTemplate( id ) == nullptr)
	{
		AsciiString full;
		full.format( "tmpl.%s@1", templateId.str() );
		id = full;
	}
	if (!StratagemCloneTemplate( id, &m_profile ))
		return false;

	m_knobs = StratagemDeriveParams( m_profile );
	m_planner.setProfile( m_profile );
	m_nextThinkFrame = 0;
	m_valid = true;
	return true;
}

//-----------------------------------------------------------------------------
Bool StratagemStrategist::assignCustomTraits( const Real *t )
{
	m_valid = false;
	if (t == nullptr)
		return false;

	StratagemProfile p;
	p.name            = "Custom";
	p.baseTemplateId  = "tmpl.custom@1";
	p.baseTemplateHash = 0;
	p.traits.aggression    = t[0];  p.traits.economy         = t[1];
	p.traits.techPriority  = t[2];  p.traits.riskTolerance   = t[3];
	p.traits.harassment    = t[4];  p.traits.expansion       = t[5];
	p.traits.defensiveness = t[6];  p.traits.adaptiveness    = t[7];
	p.traits.scouting      = t[8];  p.traits.commitDiscipline = t[9];
	p.traits.randomness    = t[10]; p.traits.buildRigidity   = t[11];
	p.traits.tempo         = t[12]; p.traits.grudge          = t[13];
	// Same competence as the shipped templates (NORMAL) so custom vectors are directly
	// comparable to them in a sweep; difficulty is an orthogonal axis.
	p.competence    = StratagemMakeCompetence( STRAT_DIFF_NORMAL );
	p.repertoireRef = "rep.default";
	p.provenance    = STRAT_PROV_LEARNED;   // sweep-generated, not hand-authored
	p.description   = "";
	p.emblemColor   = 0x888780;
	p.clampToLegalRanges();

	m_profile = p;
	m_knobs   = StratagemDeriveParams( m_profile );
	m_planner.setProfile( m_profile );
	m_nextThinkFrame = 0;
	m_valid = true;
	return true;
}

//-----------------------------------------------------------------------------
Bool StratagemStrategist::assignByRosterIndex( Int rosterIndex, StratagemDifficulty competence )
{
	m_valid = false;
	const StratagemProfile *t = StratagemGetBaseTemplateByIndex( rosterIndex );
	if (t == nullptr)
		return false;
	m_profile = *t;
	m_profile.competence = StratagemMakeCompetence( competence );   // competence from the slot difficulty
	m_knobs   = StratagemDeriveParams( m_profile );
	m_planner.setProfile( m_profile );
	m_nextThinkFrame = 0;
	m_valid = true;
	return true;
}

//-----------------------------------------------------------------------------
Bool StratagemStrategist::assignFromCommandLine()
{
	if (StratagemAIHasCustomTraits())
		return assignCustomTraits( StratagemAIGetTraitsPtr() );
	return assignTemplate( StratagemAIGetTemplateId() );
}

//-----------------------------------------------------------------------------
void StratagemStrategist::think( Player *me, UnsignedInt frame )
{
	if (!m_valid || me == nullptr)
		return;
	if (frame < m_nextThinkFrame)
		return;
	Int interval = m_knobs.thinkIntervalFrames;
	if (interval < 6) interval = 6;
	m_nextThinkFrame = frame + (UnsignedInt)interval;

	StratagemWorldSignals sig;
	sig.clear();

	// Digest the influence map into control/threat from THIS AI's perspective.
	const StratagemInfluenceMap *im = (TheStratagemBrain != nullptr) ? TheStratagemBrain->getInfluenceMap() : nullptr;
	if (im != nullptr && im->isInitialized())
	{
		Real ownSum = 0.0f, enemySum = 0.0f;
		const Int w = im->getGridWidth(), h = im->getGridHeight();
		for (Int cy = 0; cy < h; ++cy)
			for (Int cx = 0; cx < w; ++cx)
			{
				Real wx, wy;
				im->debugCellToWorld( cx, cy, &wx, &wy );
				Coord3D pos; pos.x = wx; pos.y = wy; pos.z = 0.0f;
				Real c = im->getControlAt( me, &pos );
				if (c > 0.0f) ownSum += c; else enemySum += -c;
			}
		Real total = ownSum + enemySum + 0.001f;
		sig.ownControl   = ownSum / total;
		sig.threatAtBase = enemySum / total;
	}

	// Economy proxy from banked money (refined later).
	Real money = (Real)me->getMoney()->countMoney();
	sig.economyLead = money / 8000.0f - 0.4f;
	if (sig.economyLead >  1.0f) sig.economyLead =  1.0f;
	if (sig.economyLead < -1.0f) sig.economyLead = -1.0f;
	// L0: REAL army strength (own combat-unit value) + how late the match is - the inputs the
	// offense floor (L1) needs. armyReadiness is no longer a clock.
	sig.armyReadiness = StratagemSumArmyValue( me ) / STRAT_READY_ARMY_VALUE;
	if (sig.armyReadiness > 1.0f) sig.armyReadiness = 1.0f;
	sig.gameProgress = (Real)frame / STRAT_LATEGAME_FRAMES;
	if (sig.gameProgress > 1.0f) sig.gameProgress = 1.0f;

	m_signals  = sig;
	m_directive = m_planner.decideVerbose( sig, &m_trace );   // L2: also fill the diagnostic trace

#if defined(RTS_DEBUG)
	// L2: one-time "first commit" marker - the frame this personality first decides to
	// attack. Finite-vs-infinite TTFC is the headline offense-floor metric per round.
	if (m_directive.commitAttack && m_firstCommitFrame == 0)
	{
		m_firstCommitFrame = frame;
		DEBUG_LOG(("STRATAGEM firstcommit: player=%d frame=%u name=%s",
			me->getPlayerIndex(), frame, m_profile.name.str()));
	}
#endif
}
