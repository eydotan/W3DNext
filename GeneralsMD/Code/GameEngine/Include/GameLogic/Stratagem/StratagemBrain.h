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

// StratagemBrain.h //
// Project STRATAGEM - strategic-AI subsystem singleton (SCAFFOLD, task D1).
//
// `TheStratagemBrain` is the home for the STRATAGEM strategic-AI world model and
// (later) its utility planner. For now it owns nothing but the read-only
// influence map (see StratagemInfluenceMap.h) and is SHIPPED DISABLED: with
// m_enabled == false, update() is a hard no-op, so wiring it into the build and
// the deterministic tick changes nothing observable. Task D2 flips it on behind
// the AI debug overlay; nothing here drives any AI decision yet.
//
// DETERMINISM: this is a SubsystemInterface, registered like TheAI. Its update()
// is called from inside GameLogic::update() (the lockstep logic path), right
// after TheAI->UPDATE() - never from the client-side GameEngine::update(). It is
// NOT a Snapshot: the influence map is fully recomputable from game state, so
// there is no save/load state to serialize (the grid lazily re-inits on the next
// map). See AI_REVAMP.md §3 and StratagemInfluenceMap.h for the full contract.

#pragma once

#include "Common/SubsystemInterface.h"
#include "GameLogic/Stratagem/StratagemInfluenceMap.h"

//-----------------------------------------------------------------------------
class StratagemBrain : public SubsystemInterface
{
public:
	StratagemBrain();
	virtual ~StratagemBrain() override;

	// SubsystemInterface
	virtual void init() override;
	virtual void reset() override;
	virtual void update() override;
	virtual void draw() override { }   ///< STRATAGEM has no draw pass; override the crashing base stub.

	// Master switch. D1 ships OFF (update() is a no-op). D2 turns it on.
	Bool isEnabled() const { return m_enabled; }
	void setEnabled( Bool enabled ) { m_enabled = enabled; }

	// Read-only access for the debug overlay (D2) and, later, the planner.
	const StratagemInfluenceMap *getInfluenceMap() const { return &m_influenceMap; }
	StratagemInfluenceMap *getInfluenceMap() { return &m_influenceMap; }

	// STRATAGEM auto-capture (-stratagemShot): the sample schedule, the deterministic
	// world-model signature, and the quit-after-N are driven HERE in the logic tick, so
	// the harness runs under -headless (where W3DDisplay::draw() hard-returns). The
	// windowed display calls captureThisFrame() to also draw the heatmap + screenshot.
	Bool captureThisFrame() const { return m_captureThisFrame; }

private:
	/// -stratagemShot: emit the world-model signature on a fixed sample cadence and quit
	/// after N samples. Headless-safe (no rendering). No-op unless m_stratagemShot is set.
	void updateStratagemShotCapture();

#if defined(RTS_DEBUG)
	/// Enqueue the influence map as colored debug icons (heatmap overlay). RTS_DEBUG only;
	/// gated behind the AI_DEBUG_STRATAGEM_INFLUENCE debug-AI mode. Drives no game state.
	void debugDrawInfluence() const;
	/// Run the planner against the LIVE influence map (world signals) for the archetypes and
	/// DEBUG_LOG their directives - shows decisions evolving with the real battlefield. Read-only.
	void debugLogLivePlanner() const;
#endif

	Bool						m_enabled;        ///< when false (and overlay off), update() does nothing
	StratagemInfluenceMap		m_influenceMap;   ///< the world-model layer (read-only for now)

	// -stratagemShot capture schedule (logic-side, headless-safe).
	UnsignedInt					m_captureNextFrame;   ///< next sample frame
	Int							m_captureShotsTaken;  ///< samples emitted so far
	Bool						m_captureThisFrame;   ///< true on a sample frame (display also screenshots)
};

extern StratagemBrain *TheStratagemBrain;   ///< the STRATAGEM strategic-AI singleton
