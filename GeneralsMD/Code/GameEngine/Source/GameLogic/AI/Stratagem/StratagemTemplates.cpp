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

// StratagemTemplates.cpp //
// Project STRATAGEM - shipped read-only base-template roster (task E2).
// See StratagemTemplates.h. The roster is defined here in code (the canonical,
// content-addressed shipped defaults); INI/JSON override loading is a follow-up
// (E2b) and would simply replace these values before the first lookup.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemTemplates.h"

#include "Common/STLTypedefs.h"

//-----------------------------------------------------------------------------
static StratagemProfile makeTemplate(
	const char *id, const char *name, const char *description, UnsignedInt emblemColor,
	Real aggression, Real economy, Real techPriority, Real riskTolerance,
	Real harassment, Real expansion, Real defensiveness, Real adaptiveness,
	Real scouting, Real commitDiscipline, Real randomness, Real buildRigidity,
	Real tempo, Real grudge )
{
	StratagemProfile t;
	t.name = name;
	t.baseTemplateId = id;
	t.description = description;
	t.emblemColor = emblemColor;
	t.baseTemplateHash = 0;            // a base template has no parent
	t.traits.aggression = aggression; t.traits.economy = economy;
	t.traits.techPriority = techPriority; t.traits.riskTolerance = riskTolerance;
	t.traits.harassment = harassment; t.traits.expansion = expansion;
	t.traits.defensiveness = defensiveness; t.traits.adaptiveness = adaptiveness;
	t.traits.scouting = scouting; t.traits.commitDiscipline = commitDiscipline;
	t.traits.randomness = randomness; t.traits.buildRigidity = buildRigidity;
	t.traits.tempo = tempo; t.traits.grudge = grudge;
	// Templates carry STYLE, not skill: competence is a NORMAL placeholder that the
	// match-setup difficulty overrides (it lives on a separate, orthogonal axis).
	t.competence = StratagemMakeCompetence( STRAT_DIFF_NORMAL );
	t.repertoireRef = "rep.default";
	t.provenance = STRAT_PROV_AUTHORED;
	t.clampToLegalRanges();
	return t;
}

//-----------------------------------------------------------------------------
// Lazily-built roster. (Static so it's constructed once, on first access, after
// AsciiString is ready - avoids static-init-order issues.)
//-----------------------------------------------------------------------------
static std::vector<StratagemProfile> &getRoster()
{
	static std::vector<StratagemProfile> s_roster;
	if (s_roster.empty())
	{
		//                          id                name        description                                                                                                                                                  emblem    aggr econ tech risk hara expa defe adap scou comm rand brig temp grud
		s_roster.push_back( makeTemplate("tmpl.rommel@1","Rommel",
			"The Desert Fox. Hits before your first power plant is up and never lets you breathe - early raids, relentless harassment, bold armored thrusts. Punishes the greedy and the slow.",
			0xA32D2D, 0.85f,0.30f,0.30f,0.70f,0.80f,0.40f,0.20f,0.45f,0.55f,0.40f,0.40f,0.55f,0.85f,0.55f) );
		s_roster.push_back( makeTemplate("tmpl.montgomery@1", "Montgomery",
			"The set-piece master. Walls up, tech-climbs behind a thicket of defenses, and dares you to break it. Patient and meticulous - overextend and the prepared counterblow ends you.",
			0x185FA5, 0.25f,0.55f,0.70f,0.25f,0.20f,0.30f,0.85f,0.50f,0.45f,0.65f,0.25f,0.55f,0.30f,0.40f) );
		s_roster.push_back( makeTemplate("tmpl.eisenhower@1", "Eisenhower",
			"The supreme organizer. Out-expands and out-banks you, then drowns the map in everything overwhelming production can buy. Leave it alone and you lose to logistics.",
			0xBA7517, 0.30f,0.90f,0.55f,0.40f,0.25f,0.80f,0.45f,0.50f,0.50f,0.55f,0.30f,0.45f,0.40f,0.35f) );
		s_roster.push_back( makeTemplate("tmpl.giap@1",  "Giap",
			"The patient reader. Scouts everything, commits to nothing early, and builds exactly what beats whatever you're doing. The smartest, most adaptive opponent on the board.",
			0x0F6E56, 0.50f,0.50f,0.50f,0.45f,0.45f,0.50f,0.50f,0.80f,0.75f,0.55f,0.35f,0.35f,0.50f,0.40f) );
		s_roster.push_back( makeTemplate("tmpl.zhukov@1",  "Zhukov",
			"The complete general. Every trait centered, no bias either way - the balanced benchmark, and the canvas to author a Strategist of your own from nothing.",
			0x888780, 0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f) );
	}
	return s_roster;
}

//-----------------------------------------------------------------------------
Int StratagemGetBaseTemplateCount()
{
	return (Int)getRoster().size();
}

const StratagemProfile *StratagemGetBaseTemplateByIndex( Int index )
{
	std::vector<StratagemProfile> &r = getRoster();
	if (index < 0 || index >= (Int)r.size())
		return nullptr;
	return &r[index];
}

const StratagemProfile *StratagemGetBaseTemplate( const AsciiString &id )
{
	std::vector<StratagemProfile> &r = getRoster();
	for (size_t i = 0; i < r.size(); ++i)
		if (r[i].baseTemplateId == id)
			return &r[i];
	return nullptr;
}

Int StratagemRosterIndexByName( const AsciiString &name )
{
	std::vector<StratagemProfile> &r = getRoster();
	AsciiString full;
	full.format( "tmpl.%s@1", name.str() );   // short-name form, e.g. "rommel" -> "tmpl.rommel@1"
	for (size_t i = 0; i < r.size(); ++i)
	{
		if (r[i].name.compareNoCase( name.str() ) == 0)              return (Int)i;   // display name "Rommel"
		if (r[i].baseTemplateId.compareNoCase( full.str() ) == 0)    return (Int)i;   // id "tmpl.rommel@1"
	}
	return -1;
}

Bool StratagemCloneTemplate( const AsciiString &id, StratagemProfile *out )
{
	if (out == nullptr)
		return false;
	const StratagemProfile *t = StratagemGetBaseTemplate( id );
	if (t == nullptr)
		return false;
	*out = *t;
	out->baseTemplateHash = t->contentHash();   // pin the exact base we forked from
	return true;
}

void StratagemTemplatesShutdown()
{
	// swap-with-empty frees the vector storage AND runs the StratagemProfile dtors
	// (releasing their pooled AsciiStrings) - so nothing survives to the leak check.
	std::vector<StratagemProfile>().swap( getRoster() );
}

#if defined(RTS_DEBUG)
//-----------------------------------------------------------------------------
void StratagemTemplatesSelfTest()
{
	const Int n = StratagemGetBaseTemplateCount();
	DEBUG_LOG(("STRATAGEM E2: %d base templates loaded", n));
	for (Int i = 0; i < n; ++i)
	{
		const StratagemProfile *t = StratagemGetBaseTemplateByIndex( i );
		if (t == nullptr) continue;
		// content-address stability: hashing twice yields the same value.
		UnsignedInt h1 = t->contentHash();
		UnsignedInt h2 = t->contentHash();
		DEBUG_LOG(("STRATAGEM E2 template: %-16s hash=%08x stable=%d emblem=#%06x descLen=%d aggr=%.2f defe=%.2f adap=%.2f",
			t->baseTemplateId.str(), h1, (h1 == h2) ? 1 : 0, t->emblemColor, t->description.getLength(),
			t->traits.aggression, t->traits.defensiveness, t->traits.adaptiveness));
	}
	// clone + pin verify: the clone records the template's content hash.
	StratagemProfile clone;
	if (StratagemCloneTemplate( "tmpl.rommel@1", &clone ))
	{
		const StratagemProfile *v = StratagemGetBaseTemplate( "tmpl.rommel@1" );
		DEBUG_LOG(("STRATAGEM E2 clone of Rommel: pinnedBaseHash=%08x matchesTemplate=%d emptyDelta-resolvedHash=%08x",
			clone.baseTemplateHash, (v && clone.baseTemplateHash == v->contentHash()) ? 1 : 0,
			clone.contentHash()));
	}
}
#endif
