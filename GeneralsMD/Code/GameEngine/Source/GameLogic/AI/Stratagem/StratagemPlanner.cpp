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

// StratagemPlanner.cpp //
// Project STRATAGEM - Strategic Planner decision layer. See StratagemPlanner.h.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemPlanner.h"

const char *StratagemPostureName( StratagemPosture p )
{
	switch (p)
	{
		case POSTURE_EXPAND: return "EXPAND";
		case POSTURE_TECH:   return "TECH";
		case POSTURE_MASS:   return "MASS";
		case POSTURE_DEFEND: return "DEFEND";
		case POSTURE_HARASS: return "HARASS";
		case POSTURE_ALLIN:  return "ALLIN";
		default:             return "?";
	}
}

//-----------------------------------------------------------------------------
StratagemPlanner::StratagemPlanner()
{
	StratagemProfile neutral;
	neutral.traits.zero();
	neutral.competence = StratagemMakeCompetence( STRAT_DIFF_NORMAL );
	setProfile( neutral );
}

void StratagemPlanner::setProfile( const StratagemProfile &profile )
{
	m_knobs = StratagemDeriveParams( profile );
}

//-----------------------------------------------------------------------------
// The decision. Utility per posture from (knobs x world signals); pick the max.
//-----------------------------------------------------------------------------
StratagemDirective StratagemPlanner::decide( const StratagemWorldSignals &w ) const
{
	return decideVerbose( w, nullptr );
}

//-----------------------------------------------------------------------------
// The full decision (decide() is this with no trace). Pure: argmax posture utility,
// then the L1 OFFENSE FLOOR decides commitAttack via three OR'd triggers so that
// every personality eventually attacks - defensive only delays the commit, never
// cancels it ("no personality has 0 offense").
//-----------------------------------------------------------------------------
StratagemDirective StratagemPlanner::decideVerbose( const StratagemWorldSignals &w, StratagemDecisionTrace *tr ) const
{
	const StratagemPlannerParams &k = m_knobs;

	Real score[POSTURE_COUNT];
	score[POSTURE_EXPAND] = k.wExpand * (1.0f - 0.7f * w.threatAtBase) * (1.0f - 0.3f * w.economyLead);
	score[POSTURE_TECH]   = k.wTech   * (1.0f - 0.7f * w.threatAtBase);
	score[POSTURE_MASS]   = k.wMass   * (0.5f + 0.5f * w.armyReadiness);
	score[POSTURE_DEFEND] = k.wDefend * (0.3f + 1.2f * w.threatAtBase);
	score[POSTURE_HARASS] = k.wHarass * (0.4f + 0.6f * w.ownControl);
	score[POSTURE_ALLIN]  = k.wAllIn  * w.armyReadiness;

	StratagemPosture best = POSTURE_EXPAND;
	Real bestScore = score[0];
	Real secondScore = -1.0f;
	for (Int i = 1; i < POSTURE_COUNT; ++i)
	{
		if (score[i] > bestScore) { secondScore = bestScore; bestScore = score[i]; best = (StratagemPosture)i; }
		else if (score[i] > secondScore) { secondScore = score[i]; }
	}

	StratagemDirective d;
	d.posture = best;
	d.confidence = bestScore - (secondScore < 0.0f ? 0.0f : secondScore);

	// --- L1 OFFENSE FLOOR ------------------------------------------------------
	// Commit pressure blends "my army is ready now" with "the match is getting late",
	// weighted by patience; a base under threat suppresses committing the army away
	// (defend first). Three independent commit triggers, OR'd:
	const Bool aggressive = (best == POSTURE_MASS || best == POSTURE_HARASS || best == POSTURE_ALLIN);

	Real pressure = (1.0f - k.patienceBias) * w.armyReadiness + k.patienceBias * w.gameProgress;
	pressure *= (1.0f - 0.5f * w.threatAtBase);
	if (pressure < 0.0f) pressure = 0.0f;

	// (1) EAGER: an aggressive posture that has banked its force-concentration target.
	const Bool trigEager    = aggressive && (w.armyReadiness >= k.forceConcentration);
	// (2) PRESSURE: enough blended pressure AND a non-token army. The release floor rises
	//     with defensiveness but is capped 0.80, so this stays reachable for a big army.
	const Bool trigPressure = (pressure >= k.commitReleaseThresh) && (w.armyReadiness >= k.minCommitArmy);
	// (3) BACKSTOP (the GUARANTEE): late in the match with ANY combat units, strike. Two
	//     ANDed soft conditions can both near-max and still miss the floor (Montgomery's
	//     worked example landed 0.79 < 0.80) - this fixed clause closes that gap so a
	//     surviving personality ALWAYS mounts its counterblow before the game ends. The
	//     army test is "> 0" (literally any unit) on purpose: a turtle whittled down to a
	//     handful of cheap units must still throw them - "no personality has 0 offense"
	//     holds for any SURVIVING army, not just one above some threshold.
	const Bool trigBackstop = (w.gameProgress >= 0.90f) && (w.armyReadiness > 0.0f);

	d.commitAttack = (trigEager || trigPressure || trigBackstop);

	if (tr != nullptr)
	{
		for (Int i = 0; i < POSTURE_COUNT; ++i) tr->votes[i] = score[i];
		tr->pressure     = pressure;
		tr->commitFloor  = k.commitReleaseThresh;
		tr->trigEager    = trigEager;
		tr->trigPressure = trigPressure;
		tr->trigBackstop = trigBackstop;
	}
	return d;
}

#if defined(RTS_DEBUG)
#include "GameLogic/Stratagem/StratagemTemplates.h"

static void run( const char *who, const char *scen, const StratagemProfile &p, const StratagemWorldSignals &w )
{
	StratagemPlanner pl;
	pl.setProfile( p );
	StratagemDecisionTrace tr; tr.clear();
	StratagemDirective d = pl.decideVerbose( w, &tr );
	DEBUG_LOG(("STRATAGEM PLAN %-9s [%-9s] -> %-7s commit=%d conf=%.2f | P=%.2f floor=%.2f trig=%c%c%c",
		who, scen, StratagemPostureName(d.posture), d.commitAttack ? 1 : 0, d.confidence,
		tr.pressure, tr.commitFloor,
		tr.trigEager ? 'E' : '-', tr.trigPressure ? 'P' : '-', tr.trigBackstop ? 'B' : '-'));
}

void StratagemPlannerSelfTest()
{
	StratagemWorldSignals calm;       calm.clear();
	calm.ownControl = 0.5f; calm.threatAtBase = 0.10f; calm.armyReadiness = 0.30f; calm.gameProgress = 0.20f;
	StratagemWorldSignals sieged;     sieged.clear();
	sieged.ownControl = 0.3f; sieged.threatAtBase = 0.80f; sieged.armyReadiness = 0.60f; sieged.gameProgress = 0.50f;
	// L1 offense-floor proof: a calm, fully-banked army deep in the match. EVERY personality
	// must show commit=1 here - including the turtle Montgomery (via the backstop trigger 'B').
	StratagemWorldSignals lateMaxed; lateMaxed.clear();
	lateMaxed.ownControl = 0.5f; lateMaxed.threatAtBase = 0.05f; lateMaxed.armyReadiness = 1.0f; lateMaxed.gameProgress = 0.95f;
	// The GUARANTEE at a THIN surviving army (the (0,0.10] band): a turtle whittled down to a
	// few units, still under some pressure, deep in the match. The backstop must STILL fire
	// commit=1 for everyone (this is the case army=1.0 never exercises - it guards the low-army gap).
	StratagemWorldSignals lateThin; lateThin.clear();
	lateThin.ownControl = 0.4f; lateThin.threatAtBase = 0.30f; lateThin.armyReadiness = 0.05f; lateThin.gameProgress = 0.95f;

	const char *ids[5] = { "tmpl.rommel@1", "tmpl.montgomery@1", "tmpl.eisenhower@1", "tmpl.giap@1", "tmpl.zhukov@1" };
	const char *names[5] = { "Rommel", "Montgomery", "Eisenhower", "Giap", "Zhukov" };
	for (Int i = 0; i < 5; ++i)
	{
		StratagemProfile p;
		if (!StratagemCloneTemplate( ids[i], &p )) continue;
		run( names[i], "calm",      p, calm );
		run( names[i], "sieged",    p, sieged );
		run( names[i], "lateMaxed", p, lateMaxed );
		run( names[i], "lateThin",  p, lateThin );
	}
}
#endif
