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

// StratagemInfluenceMap.h //
// Project STRATAGEM - world-model influence map (PROTOTYPE).
//
// This is the first piece of the STRATAGEM strategic-AI world model (see
// AI_REVAMP.md / STRATAGEM_PHASE0.md task D2). It is a coarse grid over the
// playable map that accumulates each player's military "influence" so the
// strategic planner can ask spatial questions the stock AI cannot:
//
//   * "How contested is this point?"            -> getControlAt()
//   * "Where is the enemy weakest around X?"    -> findWeakestApproach()
//   * "Is my base front being pressured?"       -> getThreatAt()
//
// It is the replacement for the stock AI's reliance on hand-placed
// SKIRMISH_CENTER / SKIRMISH_FLANK / SKIRMISH_BACKDOOR map waypoints: instead
// of routing along corridors a map-maker drew, STRATAGEM will route toward the
// lowest-enemy-influence approach it can actually see.
//
// DETERMINISM CONTRACT (this AI runs inside deterministic lockstep):
//   * Rebuild order is fixed: players are visited in player-index order; objects
//     within a rebuild are visited in the partition manager's deterministic
//     order. No iteration over pointer-keyed/hashed containers.
//   * Uses engine Real (float) math identical on all machines - the same fp
//     model the rest of the simulation already relies on.
//   * No wall-clock, no unsynced RNG, no per-machine state. The whole grid is
//     a pure function of game state, so it does not need to be saved (it can be
//     recomputed on load - see STRATAGEM_PHASE0.md task D3).
//
// STATUS: PROTOTYPE. Not yet added to the build. Include paths and a couple of
// accessor names (marked TODO) should be reconciled against the real headers
// when wiring into D1's module scaffold.

#pragma once

#include "Common/GameType.h"      // Int, Real, Coord3D (via Lib/BaseType.h)
#include "Common/GameCommon.h"    // MAX_PLAYER_COUNT
#include "Common/STLTypedefs.h"   // std::vector
#include "Common/GameMemory.h"

class Object;
class Player;

//-----------------------------------------------------------------------------
// Tunables. These will move into AIData INI once the prototype proves out; kept
// as named constants here so the prototype is self-contained.
//-----------------------------------------------------------------------------

// World units per influence cell. Coarse on purpose: the strategic layer reasons
// about regions, not pixels. ~10 cells across a typical base footprint.
const Real STRATAGEM_INFLUENCE_CELL_SIZE = 50.0f;

// How often (in logic frames) the grid is fully rebuilt. 30 == once/sec at the
// engine's LOGICFRAMES_PER_SECOND. Strategic decisions don't need per-frame data.
const Int STRATAGEM_INFLUENCE_REBUILD_INTERVAL = 30;

// Default falloff radius (world units) a unit's influence is stamped over when a
// per-unit value (e.g. weapon range) is unavailable.
const Real STRATAGEM_INFLUENCE_DEFAULT_RADIUS = 200.0f;

//-----------------------------------------------------------------------------
// StratagemInfluenceMap
//-----------------------------------------------------------------------------
class StratagemInfluenceMap
{
public:
	StratagemInfluenceMap();
	~StratagemInfluenceMap();

	/// Allocate the grid from the current map extent. Call once after the map loads.
	void init();

	/// Free grid memory / return to pre-init state.
	void reset();

	/// Call every logic frame. Rebuilds the grid every REBUILD_INTERVAL frames.
	/// @param currentFrame  TheGameLogic->getFrame()
	void update( UnsignedInt currentFrame );

	/// Force an immediate full rebuild (e.g. for the debug overlay or a unit test).
	void rebuildNow();

	Bool isInitialized() const { return m_initialized; }

	//-- Queries -------------------------------------------------------------

	/// Raw military influence a single player projects at a world position.
	Real getMilitaryInfluence( Int playerIndex, const Coord3D *pos ) const;

	/// Sum of influence projected by every player that is an ENEMY of forPlayer.
	Real getThreatAt( const Player *forPlayer, const Coord3D *pos ) const;

	/// Net control for forPlayer at a position: (own + allied) - enemy.
	/// Positive == we dominate here; negative == enemy dominates here.
	Real getControlAt( const Player *forPlayer, const Coord3D *pos ) const;

	/// Dynamic-routing primitive (the anti-predictability payoff).
	/// Samples a ring of candidate approach points around @p targetCenter and
	/// returns the one with the LEAST enemy control - i.e. the softest way in.
	/// @param forPlayer     the attacker (defines who "enemy" is)
	/// @param targetCenter  the point we want to attack (e.g. enemy base center)
	/// @param ringRadius    how far out from the target to evaluate approaches
	/// @param outApproach   [out] best approach world position
	/// @return true if a viable approach was found
	Bool findWeakestApproach( const Player *forPlayer,
														const Coord3D *targetCenter,
														Real ringRadius,
														Coord3D *outApproach ) const;

	//-- Debug ---------------------------------------------------------------

	Int getGridWidth() const { return m_width; }
	Int getGridHeight() const { return m_height; }
	Real getCellSize() const { return m_cellSize; }
	/// Net control sampled at a cell center, for the A2 overlay heatmap.
	Real debugGetCellControl( const Player *forPlayer, Int cx, Int cy ) const;
	/// World center of a cell, for the A2 overlay heatmap (public wrapper over cellToWorld).
	void debugCellToWorld( Int cx, Int cy, Real *wx, Real *wy ) const { cellToWorld( cx, cy, wx, wy ); }
	/// Deterministic quantized hash of the whole grid state, for the regression/determinism
	/// harness. Identical inputs -> identical signature; robust to sub-1/16 float noise.
	UnsignedInt debugSignature() const;

private:
	// Grid geometry.
	Bool		m_initialized;
	Int			m_width;        ///< cells in X
	Int			m_height;       ///< cells in Y
	Real		m_originX;      ///< world coord of cell (0,0) lower corner
	Real		m_originY;
	Real		m_cellSize;     ///< world units per cell

	UnsignedInt	m_lastRebuildFrame;

	// One influence layer per player, flat row-major: index = y*m_width + x.
	// Sized [MAX_PLAYER_COUNT][m_width*m_height].
	std::vector<Real>	m_layer[MAX_PLAYER_COUNT];

	//-- internals -----------------------------------------------------------

	void clearLayers();

	/// Stamp one object's threat into its owner's layer with radial falloff.
	void stampObject( Object *obj );

	/// Threat weight a single object contributes. Proxy for now (KINDOF + health);
	/// TODO: replace with real weapon DPS * effective range once validated.
	Real computeUnitThreat( const Object *obj ) const;

	/// World position -> cell indices. Returns false if outside the grid.
	Bool worldToCell( Real wx, Real wy, Int *cx, Int *cy ) const;
	/// Cell center -> world position.
	void cellToWorld( Int cx, Int cy, Real *wx, Real *wy ) const;

	inline Int cellIndex( Int cx, Int cy ) const { return cy * m_width + cx; }
	inline Bool inBounds( Int cx, Int cy ) const
		{ return cx >= 0 && cx < m_width && cy >= 0 && cy < m_height; }

	/// Sample a layer at a world position (nearest cell). 0 if out of bounds.
	Real sampleLayer( Int playerIndex, Real wx, Real wy ) const;
};
