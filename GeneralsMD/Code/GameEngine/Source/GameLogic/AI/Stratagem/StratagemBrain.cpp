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

// StratagemBrain.cpp //
// Project STRATAGEM - strategic-AI subsystem singleton (SCAFFOLD, task D1).
// See StratagemBrain.h for the design/determinism contract.
//
// STATUS: D1 scaffold. Compiles, is instantiated as TheStratagemBrain, and is
// ticked from GameLogic::update() - but ships DISABLED (m_enabled == false), so
// it does nothing until task D2 turns it on behind the AI debug overlay.

#include "PreRTS.h"

#include "GameLogic/Stratagem/StratagemBrain.h"

#include "Common/GlobalData.h"      // TheGlobalData->m_debugAI / m_stratagemShot
#include "Common/GameEngine.h"      // TheGameEngine->setQuitting() (auto-capture quit)
#include "Common/Player.h"
#include "Common/PlayerList.h"      // ThePlayerList->getLocalPlayer()
#include "Common/ScoreKeeper.h"     // per-AI fitness metrics (score/economy/combat)
#include "GameLogic/AI.h"           // AI_DEBUG_STRATAGEM_INFLUENCE
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Stratagem/StratagemProfile.h"   // E1 self-test
#include "GameLogic/Stratagem/StratagemStrategist.h" // StratagemSumArmyValue / StratagemCountIdleArmy (L2 fitness)
#include "GameLogic/Stratagem/StratagemTemplates.h"  // E2 self-test
#include "GameLogic/Stratagem/StratagemPlannerParams.h"  // E4 self-test
#include "GameLogic/Stratagem/StratagemPlanner.h"         // planner self-test
#include "GameLogic/Stratagem/StratagemStrategist.h"      // StratagemAIGetShots() (-stratagemShots)

StratagemBrain *TheStratagemBrain = nullptr;

//-----------------------------------------------------------------------------
// -stratagemShot capture cadence (logic-side; same schedule the display used before).
namespace {
	const UnsignedInt STRAT_CAPTURE_WARMUP   = 150;   // ~5s warm-up at 30fps
	const UnsignedInt STRAT_CAPTURE_INTERVAL = 240;   // ~8s between samples
	// sample count is -stratagemShots (StratagemAIGetShots(), default 16)
}

//-----------------------------------------------------------------------------
StratagemBrain::StratagemBrain() :
	m_enabled(false),
	m_captureNextFrame(STRAT_CAPTURE_WARMUP),
	m_captureShotsTaken(0),
	m_captureThisFrame(false)
{
}

//-----------------------------------------------------------------------------
StratagemBrain::~StratagemBrain()
{
	// Release STRATAGEM statics before the debug leak detector runs at shutdown.
	m_influenceMap.reset();          // free the world-model buffers
	StratagemTemplatesShutdown();    // free the lazily-built base-template roster
}

//-----------------------------------------------------------------------------
void StratagemBrain::init()
{
	// NOTE: deliberately does NOT initialize the influence map here. init() runs
	// at engine startup, before any map is loaded, so the terrain extent is not
	// yet available. The grid is lazily initialized in update() once a game is
	// actually running.
	m_enabled = false;   // D1: shipped disabled. D2 flips this on.

#if defined(RTS_DEBUG)
	StratagemProfileSelfTest();        // E1: prove the profile resolver is a pure, deterministic function
	StratagemTemplatesSelfTest();      // E2: load + hash-verify + clone the base-template roster
	StratagemPlannerParamsSelfTest();  // E4: derive planner knobs from traits (legible divergence)
	StratagemPlannerSelfTest();        // Planner: archetypes decide differently on the same battlefield
#endif
}

//-----------------------------------------------------------------------------
void StratagemBrain::reset()
{
	// Called when a game ends / a new game begins. Drop the grid; it will lazily
	// re-init for the next map on the first enabled update().
	m_influenceMap.reset();
	m_captureNextFrame  = STRAT_CAPTURE_WARMUP;   // restart the auto-capture schedule per game
	m_captureShotsTaken = 0;
	m_captureThisFrame  = false;
}

//-----------------------------------------------------------------------------
void StratagemBrain::update()
{
	// Ticked from GameLogic::update() (the deterministic lockstep path), right
	// after TheAI. Active only when the (future) planner wants it, or when the
	// STRATAGEM debug-overlay mode is showing the heatmap; otherwise a hard no-op.
	Bool active = m_enabled;
	if (TheGlobalData->m_debugAI == AI_DEBUG_STRATAGEM_INFLUENCE)
		active = true;
	if (TheGlobalData->m_stratagemShot)   // auto-capture harness: keep the world model live (incl. -headless)
		active = true;
	if (!active)
		return;

	// Lazy init: wait until a map is genuinely loaded before sizing the grid.
	if (!m_influenceMap.isInitialized())
	{
		if (TheTerrainLogic == nullptr)
			return;
		Region3D extent;
		TheTerrainLogic->getExtent( &extent );
		if ((extent.hi.x - extent.lo.x) <= 0.0f)
			return;   // terrain not ready yet
		m_influenceMap.init();
	}

	// Read-only world-model rebuild (internally throttled). Drives no decisions.
	m_influenceMap.update( TheGameLogic->getFrame() );

	// Auto-capture harness: emit the deterministic signature + schedule shots + quit.
	// Logic-side so it runs headless; the windowed display reads captureThisFrame().
	if (TheGlobalData->m_stratagemShot)
		updateStratagemShotCapture();

#if defined(RTS_DEBUG)
	if (TheGlobalData->m_debugAI == AI_DEBUG_STRATAGEM_INFLUENCE)
	{
		debugDrawInfluence();
		debugLogLivePlanner();   // run the planner against the live world model
	}
#endif
}

//-----------------------------------------------------------------------------
// -stratagemShot auto-capture, logic-side. Emits the deterministic world-model
// signature on a fixed cadence (warm-up -> every INTERVAL frames, SHOTS samples)
// and quits after the last one. Runs in the logic tick, so it works under
// -headless (no rendering). The windowed display reads captureThisFrame() to also
// draw the heatmap + take the screenshot on these same frames.
//-----------------------------------------------------------------------------
void StratagemBrain::updateStratagemShotCapture()
{
	m_captureThisFrame = false;
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame()
			|| TheGameLogic->isInShellGame() || TheGameLogic->isLoadingMap())
		return;

	const Int totalShots = StratagemAIGetShots();   // -stratagemShots (default 16)
	UnsignedInt f = TheGameLogic->getFrame();
	if (f >= m_captureNextFrame && m_captureShotsTaken < totalShots)
	{
		m_captureThisFrame = true;
		m_captureNextFrame = f + STRAT_CAPTURE_INTERVAL;
		++m_captureShotsTaken;

		// Deterministic world-model fingerprint for the regression / optimization harness
		// (scripts/stratagem_*.ps1 grep these). DEBUG_LOG, so the debug build is required.
		UnsignedInt sig = m_influenceMap.isInitialized() ? m_influenceMap.debugSignature() : 0u;
		DEBUG_LOG(("STRATAGEM signature: frame=%d sig=%08x", f, sig));

		// Per-AI fitness snapshot for the optimization harness: economy + combat outcome
		// from each skirmish AI's ScoreKeeper. The persona (subject) vs the baseline is
		// identified by the script via the earlier "assigned Strategist ... player N" line;
		// the last sample (frame 3750) is the match outcome.
		if (ThePlayerList != nullptr)
		{
			Int aiCount = 0, activeCount = 0, lastActiveIdx = -1;
			for (Int pi = 0; pi < ThePlayerList->getPlayerCount(); ++pi)
			{
				Player *p = ThePlayerList->getNthPlayer( pi );
				if (p == nullptr || p->getPlayerType() != PLAYER_COMPUTER || !p->isSkirmishAIPlayer())
					continue;
				ScoreKeeper *sk = p->getScoreKeeper();
				const Bool active = p->isPlayerActive();
				++aiCount;
				if (active) { ++activeCount; lastActiveIdx = p->getPlayerIndex(); }
				// L2: armyValue + idleCount let the training loop see WHY a round went the way it did
				// (a big idle army while losing = the turtle failure mode the offense floor must fix).
				DEBUG_LOG(("STRATAGEM fitness: frame=%d player=%d active=%d score=%d earned=%d built=%d kills=%d lost=%d bldBuilt=%d bldLost=%d armyValue=%d idleCount=%d",
					f, p->getPlayerIndex(), active ? 1 : 0, sk->calculateScore(),
					sk->getTotalMoneyEarned(), sk->getTotalUnitsBuilt(), sk->getTotalUnitsDestroyed(),
					sk->getTotalUnitsLost(), sk->getTotalBuildingsBuilt(), sk->getTotalBuildingsLost(),
					(Int)StratagemSumArmyValue( p ), StratagemCountIdleArmy( p )));
			}
			// Decisive outcome: a skirmish AI was eliminated -> record the winner and end the
			// match early (saves wall-clock when a game actually resolves).
			if (aiCount >= 2 && activeCount <= 1)
			{
				DEBUG_LOG(("STRATAGEM outcome: frame=%d winner=%d activeAIs=%d", f, lastActiveIdx, activeCount));
				if (TheGameEngine != nullptr)
					TheGameEngine->setQuitting( TRUE );
			}
		}

		if (m_captureShotsTaken >= totalShots && TheGameEngine != nullptr)
			TheGameEngine->setQuitting( TRUE );
	}
}

#if defined(RTS_DEBUG)
//-----------------------------------------------------------------------------
// Debug heatmap overlay. Mirrors Pathfinder::doDebugIcons(): enqueues one
// alpha-blended colored quad per non-neutral cell via the shared addIcon()
// debug-icon list, which the W3DDebugIcons scene render object draws each client
// frame. Read-only - touches no game state, so it is safe to enqueue from the
// logic tick (exactly as the pathfinder does).
//-----------------------------------------------------------------------------
void StratagemBrain::debugDrawInfluence() const
{
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

	if (!m_influenceMap.isInitialized())
		return;
	Player *me = ThePlayerList->getLocalPlayer();
	if (me == nullptr)
		return;

	RGBColor color;
	color.red = color.green = color.blue = 0.0f;
	addIcon( nullptr, 0, 0, color );   // clear last frame's icons

	const Int  w        = m_influenceMap.getGridWidth();
	const Int  h        = m_influenceMap.getGridHeight();
	const Real cellSize = m_influenceMap.getCellSize();

	// First pass: find the strongest |control| so intensity stays legible across maps.
	Real maxAbs = 1.0f;
	for (Int cy = 0; cy < h; ++cy)
		for (Int cx = 0; cx < w; ++cx)
		{
			Real v = m_influenceMap.debugGetCellControl( me, cx, cy );
			Real a = (v < 0.0f) ? -v : v;
			if (a > maxAbs)
				maxAbs = a;
		}

	// Second pass: stamp blue (we control) / red (enemy controls), intensity by magnitude.
	for (Int cy = 0; cy < h; ++cy)
	{
		for (Int cx = 0; cx < w; ++cx)
		{
			Real control = m_influenceMap.debugGetCellControl( me, cx, cy );
			if (control == 0.0f)
				continue;   // neutral - draw nothing

			Real wx, wy;
			m_influenceMap.debugCellToWorld( cx, cy, &wx, &wy );

			Coord3D loc;
			loc.x = wx;
			loc.y = wy;
			loc.z = TheTerrainLogic->getGroundHeight( wx, wy ) + 0.5f;

			Real intensity = ((control < 0.0f) ? -control : control) / maxAbs;
			if (intensity > 1.0f)
				intensity = 1.0f;

			RGBColor col;
			col.red = col.green = col.blue = 0.0f;
			if (control > 0.0f)
				col.blue = intensity;   // friendly-controlled
			else
				col.red = intensity;    // enemy-controlled

			addIcon( &loc, cellSize * 0.9f, 2, col );   // short life -> refreshed each frame
		}
	}
}

//-----------------------------------------------------------------------------
void StratagemBrain::debugLogLivePlanner() const
{
	UnsignedInt f = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if ((f % 150) != 0)   // ~every 5s
		return;
	Player *me = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (me == nullptr || !m_influenceMap.isInitialized())
		return;

	// Digest the live influence map into world signals (local-player perspective).
	Real ownSum = 0.0f, enemySum = 0.0f;
	const Int w = m_influenceMap.getGridWidth(), h = m_influenceMap.getGridHeight();
	for (Int cy = 0; cy < h; ++cy)
		for (Int cx = 0; cx < w; ++cx)
		{
			Real c = m_influenceMap.debugGetCellControl( me, cx, cy );
			if (c > 0.0f) ownSum += c; else enemySum += -c;
		}
	Real total = ownSum + enemySum + 0.001f;
	StratagemWorldSignals sig;
	sig.clear();
	sig.ownControl    = ownSum / total;
	sig.threatAtBase  = enemySum / total;            // proxy: enemy share of contested influence
	// L0/L1: real army strength + match progress (was the frame/1800 clock stand-in). This is a
	// RTS_DEBUG demo of the planner over the LOCAL player's perspective; consumes the same signals as live AIs.
	sig.armyReadiness = StratagemSumArmyValue( me ) / 6000.0f;   // STRAT_READY_ARMY_VALUE
	if (sig.armyReadiness > 1.0f) sig.armyReadiness = 1.0f;
	sig.gameProgress  = (Real)f / 10000.0f;          // STRAT_LATEGAME_FRAMES
	if (sig.gameProgress > 1.0f) sig.gameProgress = 1.0f;

	const char *ids[4]   = { "tmpl.rommel@1", "tmpl.montgomery@1", "tmpl.eisenhower@1", "tmpl.giap@1" };
	const char *names[4] = { "Rommel", "Montgomery", "Eisenhower", "Giap" };
	for (Int i = 0; i < 4; ++i)
	{
		StratagemProfile p;
		if (!StratagemCloneTemplate( ids[i], &p )) continue;
		StratagemPlanner pl;
		pl.setProfile( p );
		StratagemDirective d = pl.decide( sig );
		DEBUG_LOG(("STRATAGEM LIVEPLAN f=%d own=%.2f threat=%.2f ready=%.2f | %-9s -> %-7s commit=%d",
			f, sig.ownControl, sig.threatAtBase, sig.armyReadiness, names[i],
			StratagemPostureName(d.posture), d.commitAttack ? 1 : 0));
	}
}
#endif // RTS_DEBUG
