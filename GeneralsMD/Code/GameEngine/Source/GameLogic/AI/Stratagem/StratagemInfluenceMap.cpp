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

// StratagemInfluenceMap.cpp //
// Project STRATAGEM - world-model influence map (PROTOTYPE).
// See StratagemInfluenceMap.h for the design contract and determinism rules.
//
// STATUS: PROTOTYPE. Not yet added to any .vcxproj / CMake target. The includes
// below follow the conventions used by the neighbouring AI sources
// (AI.cpp / AIPlayer.cpp); reconcile exact paths when wiring into the build
// (STRATAGEM_PHASE0.md task D1).

#include "PreRTS.h"   // engine-wide precompiled header, as every GameLogic .cpp does

#include "GameLogic/Stratagem/StratagemInfluenceMap.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"   // BodyModuleInterface::getHealth/getMaxHealth
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"

//-----------------------------------------------------------------------------
StratagemInfluenceMap::StratagemInfluenceMap() :
	m_initialized(false),
	m_width(0),
	m_height(0),
	m_originX(0.0f),
	m_originY(0.0f),
	m_cellSize(STRATAGEM_INFLUENCE_CELL_SIZE),
	m_lastRebuildFrame(0)
{
}

//-----------------------------------------------------------------------------
StratagemInfluenceMap::~StratagemInfluenceMap()
{
	reset();
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::init()
{
	reset();

	Region3D extent;
	TheTerrainLogic->getExtent( &extent );

	m_originX = extent.lo.x;
	m_originY = extent.lo.y;
	m_cellSize = STRATAGEM_INFLUENCE_CELL_SIZE;

	Real worldW = extent.hi.x - extent.lo.x;
	Real worldH = extent.hi.y - extent.lo.y;

	// ceil() so the far edge is always covered.
	m_width  = (Int)ceil( worldW / m_cellSize );
	m_height = (Int)ceil( worldH / m_cellSize );
	if (m_width  < 1) m_width  = 1;
	if (m_height < 1) m_height = 1;

	const Int cells = m_width * m_height;
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
		m_layer[p].assign( cells, 0.0f );

	m_initialized = true;
	m_lastRebuildFrame = 0;

	DEBUG_LOG(("STRATAGEM influence map: %dx%d cells (%g world units/cell)\n",
		m_width, m_height, m_cellSize));
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::reset()
{
	// swap-with-empty actually RELEASES the buffers (clear() keeps capacity, which the
	// debug leak detector then flags at shutdown).
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
		std::vector<Real>().swap( m_layer[p] );
	m_width = m_height = 0;
	m_initialized = false;
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::clearLayers()
{
	const Int cells = m_width * m_height;
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
	{
		// assign reuses the existing allocation when the size is unchanged, so this
		// keeps the grid memory and just zeroes it (no <algorithm> dependency).
		m_layer[p].assign( cells, 0.0f );
	}
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::update( UnsignedInt currentFrame )
{
	if (!m_initialized)
		return;

	// Throttled full rebuild. (A future optimization can time-slice the rebuild
	// across several frames; at this cadence/cost a full pass is fine.)
	if (currentFrame - m_lastRebuildFrame < (UnsignedInt)STRATAGEM_INFLUENCE_REBUILD_INTERVAL)
		return;

	m_lastRebuildFrame = currentFrame;
	rebuildNow();
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::rebuildNow()
{
	if (!m_initialized)
		return;

	clearLayers();

	// Deterministic iteration: the partition manager yields objects in a fixed
	// spatial order that is identical across machines in lockstep. Each object is
	// attributed to its controlling player's layer, so summation order is stable.
	PartitionFilter *filters[1] = { nullptr };
	ObjectIterator *iter = ThePartitionManager->iterateAllObjects( filters );
	MemoryPoolObjectHolder holder( iter );

	for (Object *obj = iter->first(); obj; obj = iter->next())
	{
		stampObject( obj );
	}
}

//-----------------------------------------------------------------------------
Real StratagemInfluenceMap::computeUnitThreat( const Object *obj ) const
{
	if (obj == nullptr)
		return 0.0f;

	// Skip the obviously-non-military so harvesters/dozers/supply don't read as
	// "army". (KINDOF flags are how the stock AI already classifies units, e.g.
	// AIPlayer.cpp's structure/vehicle/infantry counting.)
	if (obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_DOZER ))
		return 0.0f;
	if (obj->isEffectivelyDead())
		return 0.0f;

	Bool isCombatant = obj->isKindOf( KINDOF_VEHICLE )
									|| obj->isKindOf( KINDOF_INFANTRY )
									|| obj->isKindOf( KINDOF_AIRCRAFT )
									|| obj->isKindOf( KINDOF_STRUCTURE );  // base defenses project influence too
	if (!isCombatant)
		return 0.0f;

	// PROTOTYPE threat proxy: scale by current health so damaged/under-construction
	// units count for less. Structures contribute a flat defensive weight.
	// TODO (D2 refinement): replace with real offensive value =
	//   weapon DPS * effective range, summed over the unit's weapon set.
	Real threat = 1.0f;
	if (obj->isKindOf( KINDOF_STRUCTURE ))
	{
		threat = obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) ? 3.0f : 0.5f;
	}

	BodyModuleInterface *body = obj->getBodyModule();
	if (body && body->getMaxHealth() > 0.0f)
	{
		Real healthFrac = body->getHealth() / body->getMaxHealth();
		if (healthFrac < 0.0f) healthFrac = 0.0f;
		if (healthFrac > 1.0f) healthFrac = 1.0f;
		threat *= healthFrac;
	}

	return threat;
}

//-----------------------------------------------------------------------------
void StratagemInfluenceMap::stampObject( Object *obj )
{
	Real threat = computeUnitThreat( obj );
	if (threat <= 0.0f)
		return;

	Player *owner = obj->getControllingPlayer();
	if (owner == nullptr)
		return;
	Int playerIndex = owner->getPlayerIndex();
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return;

	const Coord3D *pos = obj->getPosition();

	// Falloff radius. PROTOTYPE: a constant; later this can be the unit's weapon
	// range / vision so artillery projects farther than a rifleman.
	Real radius = STRATAGEM_INFLUENCE_DEFAULT_RADIUS;

	Int ccx, ccy;
	if (!worldToCell( pos->x, pos->y, &ccx, &ccy ))
		return;

	Int cellRadius = (Int)ceil( radius / m_cellSize );
	std::vector<Real> &layer = m_layer[playerIndex];

	// Stamp a disc with linear falloff into the owner's layer.
	for (Int dy = -cellRadius; dy <= cellRadius; ++dy)
	{
		Int cy = ccy + dy;
		for (Int dx = -cellRadius; dx <= cellRadius; ++dx)
		{
			Int cx = ccx + dx;
			if (!inBounds( cx, cy ))
				continue;

			Real wx, wy;
			cellToWorld( cx, cy, &wx, &wy );
			Real ddx = wx - pos->x;
			Real ddy = wy - pos->y;
			Real dist = sqrt( ddx*ddx + ddy*ddy );
			if (dist > radius)
				continue;

			Real falloff = 1.0f - (dist / radius);   // 1 at center -> 0 at edge
			layer[ cellIndex(cx, cy) ] += threat * falloff;
		}
	}
}

//-----------------------------------------------------------------------------
// Coordinate helpers
//-----------------------------------------------------------------------------
Bool StratagemInfluenceMap::worldToCell( Real wx, Real wy, Int *cx, Int *cy ) const
{
	Int x = (Int)floor( (wx - m_originX) / m_cellSize );
	Int y = (Int)floor( (wy - m_originY) / m_cellSize );
	if (!inBounds( x, y ))
		return false;
	*cx = x;
	*cy = y;
	return true;
}

void StratagemInfluenceMap::cellToWorld( Int cx, Int cy, Real *wx, Real *wy ) const
{
	*wx = m_originX + (cx + 0.5f) * m_cellSize;
	*wy = m_originY + (cy + 0.5f) * m_cellSize;
}

Real StratagemInfluenceMap::sampleLayer( Int playerIndex, Real wx, Real wy ) const
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return 0.0f;
	Int cx, cy;
	if (!worldToCell( wx, wy, &cx, &cy ))
		return 0.0f;
	return m_layer[playerIndex][ cellIndex(cx, cy) ];
}

//-----------------------------------------------------------------------------
// Queries
//-----------------------------------------------------------------------------
Real StratagemInfluenceMap::getMilitaryInfluence( Int playerIndex, const Coord3D *pos ) const
{
	if (!m_initialized || pos == nullptr)
		return 0.0f;
	return sampleLayer( playerIndex, pos->x, pos->y );
}

Real StratagemInfluenceMap::getThreatAt( const Player *forPlayer, const Coord3D *pos ) const
{
	if (!m_initialized || forPlayer == nullptr || pos == nullptr)
		return 0.0f;

	Real threat = 0.0f;
	// Player-index order -> deterministic accumulation.
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *other = ThePlayerList->getNthPlayer( i );
		if (other == nullptr)
			continue;
		if (forPlayer->getRelationship( other->getDefaultTeam() ) == ENEMIES)
			threat += sampleLayer( other->getPlayerIndex(), pos->x, pos->y );
	}
	return threat;
}

Real StratagemInfluenceMap::getControlAt( const Player *forPlayer, const Coord3D *pos ) const
{
	if (!m_initialized || forPlayer == nullptr || pos == nullptr)
		return 0.0f;

	Real own = 0.0f, enemy = 0.0f;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *other = ThePlayerList->getNthPlayer( i );
		if (other == nullptr)
			continue;
		Real v = sampleLayer( other->getPlayerIndex(), pos->x, pos->y );
		Relationship rel = forPlayer->getRelationship( other->getDefaultTeam() );
		if (rel == ENEMIES)
			enemy += v;
		else
			own += v;   // self + allies
	}
	return own - enemy;
}

//-----------------------------------------------------------------------------
// Dynamic routing: the anti-predictability payoff. Replaces fixed
// SKIRMISH_FLANK / SKIRMISH_BACKDOOR waypoints with "go where the enemy isn't".
//-----------------------------------------------------------------------------
Bool StratagemInfluenceMap::findWeakestApproach( const Player *forPlayer,
																								 const Coord3D *targetCenter,
																								 Real ringRadius,
																								 Coord3D *outApproach ) const
{
	if (!m_initialized || forPlayer == nullptr || targetCenter == nullptr || outApproach == nullptr)
		return false;

	// Fixed sample count -> deterministic; angles derived from the index, never RNG.
	const Int NUM_SAMPLES = 16;
	Bool found = false;
	Real bestThreat = 0.0f;
	Coord3D best;
	best.x = best.y = best.z = 0.0f;

	for (Int i = 0; i < NUM_SAMPLES; ++i)
	{
		Real angle = (2.0f * PI * i) / NUM_SAMPLES;
		Coord3D sample;
		sample.x = targetCenter->x + cos(angle) * ringRadius;
		sample.y = targetCenter->y + sin(angle) * ringRadius;
		sample.z = targetCenter->z;

		// Skip approaches that fall off the playable area.
		Int cx, cy;
		if (!worldToCell( sample.x, sample.y, &cx, &cy ))
			continue;

		Real threat = getThreatAt( forPlayer, &sample );
		if (!found || threat < bestThreat)
		{
			found = true;
			bestThreat = threat;
			best = sample;
		}
	}

	if (found)
		*outApproach = best;
	return found;
}

//-----------------------------------------------------------------------------
// Debug overlay support
//-----------------------------------------------------------------------------
Real StratagemInfluenceMap::debugGetCellControl( const Player *forPlayer, Int cx, Int cy ) const
{
	if (!m_initialized || forPlayer == nullptr || !inBounds( cx, cy ))
		return 0.0f;
	Real wx, wy;
	cellToWorld( cx, cy, &wx, &wy );
	Coord3D pos; pos.x = wx; pos.y = wy; pos.z = 0.0f;
	return getControlAt( forPlayer, &pos );
}

//-----------------------------------------------------------------------------
UnsignedInt StratagemInfluenceMap::debugSignature() const
{
	// FNV-1a over the quantized per-player influence layers. Player-independent
	// (hashes the raw stamped state, not a perspective-derived view), so it captures
	// the full world-model output. Quantizing to 1/16 keeps it robust to any
	// sub-quantum float jitter while staying sensitive to real changes.
	UnsignedInt h = 2166136261u;   // FNV offset basis
	h ^= (UnsignedInt)m_width;  h *= 16777619u;
	h ^= (UnsignedInt)m_height; h *= 16777619u;
	if (!m_initialized)
		return h;
	const Int cells = m_width * m_height;
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
	{
		if ((Int)m_layer[p].size() < cells)
			continue;
		for (Int i = 0; i < cells; ++i)
		{
			Int q = (Int)(m_layer[p][i] * 16.0f);
			h ^= (UnsignedInt)q; h *= 16777619u;
		}
	}
	return h;
}
