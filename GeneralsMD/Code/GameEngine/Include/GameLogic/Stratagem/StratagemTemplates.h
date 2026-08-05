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

// StratagemTemplates.h //
// Project STRATAGEM - the shipped read-only base-template roster (task E2;
// see STRATAGEM_PERSONALITIES.md Part II "Strategist Roster & Template Inheritance").
//
// Five immutable, versioned, content-addressed base templates a player clones a
// Strategist from. Templates carry STYLE (the trait vector), NOT skill - the
// competence/difficulty is selected independently at match setup and overrides the
// template's placeholder. Each template's `baseTemplateHash` is its own
// contentHash(), so a clone can pin and hash-verify exactly what it forked from
// (the rebase guard).
//
// Named after famous modern-history generals, matched to playstyle:
//   tmpl.rommel@1      Rommel      - early pressure / harass (Rusher)
//   tmpl.montgomery@1  Montgomery  - fortify + tech behind defense (Turtle)
//   tmpl.eisenhower@1  Eisenhower  - out-economy, then convert the lead (Economist)
//   tmpl.giap@1        Giap        - balanced, reads & counters you (Adaptive Generalist)
//   tmpl.zhukov@1      Zhukov      - deliberately neutral (advanced "new from scratch")

#pragma once

#include "GameLogic/Stratagem/StratagemProfile.h"

/// Number of shipped base templates.
Int StratagemGetBaseTemplateCount();

/// Get a base template by roster index [0..count), or nullptr.
const StratagemProfile *StratagemGetBaseTemplateByIndex( Int index );

/// Look up a base template by id (e.g. "tmpl.rommel@1"), or nullptr if unknown.
const StratagemProfile *StratagemGetBaseTemplate( const AsciiString &id );

/// Roster index (0..count-1) for a general by display name ("Rommel") or short id ("rommel"),
/// case-insensitive; -1 if unknown. For -stratagemOpponentAI -> slot->setStrategistId().
Int StratagemRosterIndexByName( const AsciiString &name );

/// Clone a base template into @p out as the starting (empty-delta) profile for a new
/// Strategist: copies the template and pins its content hash. Returns false on unknown id.
Bool StratagemCloneTemplate( const AsciiString &id, StratagemProfile *out );

/// Release the lazily-built roster (the static vector + its pooled strings). Call at
/// engine teardown so the debug leak detector doesn't flag this function-static at shutdown.
void StratagemTemplatesShutdown();

#if defined(RTS_DEBUG)
/// Load every template, hash-verify (content-address self-consistency), clone one, and
/// DEBUG_LOG the results - the E2 acceptance check.
void StratagemTemplatesSelfTest();
#endif
