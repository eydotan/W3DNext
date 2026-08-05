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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameLogic/Stratagem/StratagemStrategist.h"	// STRATAGEM: -stratagemAI template config
#include "trim.h"




Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int DX8Wrapper_PreserveFPU;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_saveDebugCRCPerFrame = FALSE;
AsciiString g_saveDebugCRCPerFrameDir;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(RTS_DEBUG)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
typedef Int (*FuncPtr)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

static void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString actualpath;

	if ((path.find('\\') == nullptr) && (path.find('/') == nullptr))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	path.nextToken(&token, "\\/");
	while (!token.endsWithNoCase(".map") && (!token.isEmpty()))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
	}
	// remove the .map from the end.
	token.truncateBy(4);

	actualpath.concat(token);
	actualpath.concat('\\');
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build"));
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	TheWritableGlobalData->m_windowed = true;
	TheWritableGlobalData->m_borderless = false;	// true small windowed mode, not borderless-fullscreen

	return 1;
}

//=============================================================================
// TheSuperHackers @feature Allow several clients to run side by side for
// parallel testing. Each instance grabs the next free numbered mutex instead
// of bailing out (see rts::ClientInstance). Opt-in; normal launches still
// enforce a single instance.
//=============================================================================
Int parseMultiInstance(char *args[], int)
{
	rts::ClientInstance::setMultiInstance(TRUE);

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	TheWritableGlobalData->m_musicOn = false;

	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	TheWritableGlobalData->m_videoOn = false;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		DX8Wrapper_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFullViewport(char *args[], int num)
{
	TheWritableGlobalData->m_viewportHeightScale = 1.0f;

	return 1;
}

#if defined(RTS_DEBUG)

//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	TheWritableGlobalData->m_disableScriptedInputDisabling = true;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraFade = true;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	TheWritableGlobalData->m_disableMilitaryCaption = true;

	return 1;
}
#endif // RTS_DEBUG

//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseSaveDebugCRCPerFrame(char* args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		g_saveDebugCRCPerFrame = TRUE;
		g_saveDebugCRCPerFrameDir = args[1];
		if (TheCRCFirstFrameToLog == -1)
			TheCRCFirstFrameToLog = 0;
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	TheWritableGlobalData->m_noDraw = TRUE;
#endif
	return 1;
}

#if defined(RTS_DEBUG)

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
#endif
	return 1;
}

#endif // RTS_DEBUG

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	TheWritableGlobalData->m_audioOn = false;
	TheWritableGlobalData->m_speechOn = false;
	TheWritableGlobalData->m_soundsOn = false;
	TheWritableGlobalData->m_musicOn = false;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	TheWritableGlobalData->m_windowed = false;

	return 1;
}

//=============================================================================
// W3DNext: -exclusive forces true exclusive fullscreen (the old default).
// Borderless-fullscreen is the default now (safe on Win11 alt-tab); use this
// only to reclaim exclusive mode - e.g. squeezing max FPS on a weak GPU - and
// accept the DX8 device-lost freeze risk on focus loss.
//=============================================================================
Int parseExclusive(char *args[], int)
{
	TheWritableGlobalData->m_windowed = false;
	TheWritableGlobalData->m_borderless = false;

	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	TheWritableGlobalData->m_useShadowVolumes = false;
	TheWritableGlobalData->m_useShadowDecals = false;

	return 1;
}

Int parseMapName(char *args[], int num)
{
	if (num == 2)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
	}
	return 1;
}

Int parseHeadless(char *args[], int num)
{
	TheWritableGlobalData->m_headless = TRUE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;

	// TheSuperHackers @fix bobtista 03/02/2026 Set DX8Wrapper_IsWindowed to false in headless
	// mode so that ignoringAsserts() works correctly throughout the entire process lifetime,
	// including during shutdown after TheGlobalData has been destroyed.
	extern bool DX8Wrapper_IsWindowed;
	DX8Wrapper_IsWindowed = false;

	return 1;
}

// Project STRATAGEM auto-capture: boot straight into an AI skirmish with the
// influence overlay on, screenshot it, and exit - fully unattended. Optional
// next arg is a map path; otherwise the first multiplayer map in the cache is used.
Int parseStratagemShot(char *args[], int num)
{
	TheWritableGlobalData->m_stratagemShot = TRUE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	TheWritableGlobalData->m_shellMapOn = FALSE;
	if (num > 1 && args[1] != nullptr && args[1][0] != '-')
	{
		TheWritableGlobalData->m_stratagemShotMap.set( args[1] );
		return 2;
	}
	return 1;
}

// NAVAL Tier-1 verification: boot straight into a skirmish on a water map, spawn a
// ChinaGunboat, order it across the water, DEBUG_LOG whether it traversed, then exit -
// fully unattended. Reuses the -stratagemShot auto-skirmish boot path. Optional next
// arg is the map path (else the first multiplayer map in the cache is used).
Int parseNavalShot(char *args[], int num)
{
	TheWritableGlobalData->m_navalShot = TRUE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	TheWritableGlobalData->m_shellMapOn = FALSE;

	// Run as a non-primary extra instance so this verification can co-exist with a
	// retail / Stratagem instance already running (and so it writes its own
	// DebugLogFileD_InstanceNN.txt instead of clobbering the primary log).
	rts::ClientInstance::setMultiInstance(TRUE);
	rts::ClientInstance::skipPrimaryInstance();

	// parseCommandLine() tokenizes the command line on spaces AND quotes, so a map
	// path such as  Maps\Tournament Lake\Tournament Lake.map  arrives as several
	// tokens. Re-join the consecutive non-flag tokens (single-space separated, which
	// is how map paths are spelled) back into the full map name.
	Int consumed = 1;
	AsciiString mapName;
	for (Int i = 1; i < num && args[i] != nullptr && args[i][0] != '-'; ++i)
	{
		if (mapName.isNotEmpty())
			mapName.concat(' ');
		mapName.concat( args[i] );
		++consumed;
	}
	if (mapName.isNotEmpty())
		TheWritableGlobalData->m_navalShotMap.set( mapName );
	return consumed;
}

// NAVAL demo: AI-vs-AI water sandbox. Boots a skirmish on a water map; an in-engine
// driver gives two allied sides a War Factory that keeps PRODUCING ChinaGunboats and
// patrols them around the lake - no combat. Windowed, runs until killed (for watching
// / screen-capture). Optional next arg is the map path. Co-runs as a non-primary
// instance like -navalShot.
Int parseNavalSandbox(char *args[], int num)
{
	TheWritableGlobalData->m_navalSandbox = TRUE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	TheWritableGlobalData->m_shellMapOn = FALSE;

	rts::ClientInstance::setMultiInstance(TRUE);
	rts::ClientInstance::skipPrimaryInstance();

	// Re-join the space-split map-path tokens (see parseNavalShot for why).
	Int consumed = 1;
	AsciiString mapName;
	for (Int i = 1; i < num && args[i] != nullptr && args[i][0] != '-'; ++i)
	{
		if (mapName.isNotEmpty())
			mapName.concat(' ');
		mapName.concat( args[i] );
		++consumed;
	}
	if (mapName.isNotEmpty())
		TheWritableGlobalData->m_navalSandboxMap.set( mapName );
	return consumed;
}

// W3DNext renderer port (RENDERER_PORT.md step 10): choose the render backend.
// Usage: -gfxBackend d3d11   (aliases: dx11). Anything else (dx8/d3d8/omitted)
// keeps the default DX8 backend. Default OFF -> zero behavior change; when set,
// Init_Render_Backend constructs D3D11Backend instead of DX8Backend and brings up
// its own device on the game HWND. Parsed before window creation like -win.
Int parseGfxBackend(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr && args[1][0] != '-')
	{
		if (strcmp(args[1], "d3d11") == 0 || strcmp(args[1], "dx11") == 0)
			TheWritableGlobalData->m_gfxBackendD3D11 = TRUE;
		else
			TheWritableGlobalData->m_gfxBackendD3D11 = FALSE;
		return 2; // consumed the flag and its value token
	}
	return 1;
}

// W3DNext: bare boolean alias for -gfxBackend d3d11.
// Registered as both -d3d11 and -dx11 (DX8->D3D11 migration shorthand).
Int parseD3D11(char *args[], int)
{
	TheWritableGlobalData->m_gfxBackendD3D11 = TRUE;
	return 1;
}

// W3DNext: bare boolean alias for -gfxBackend dx8 (explicitly select the
// default DX8 backend; wins over an earlier -dx11/-d3d11 on the same line
// because later flags overwrite the same GlobalData field).
Int parseDX8(char *args[], int)
{
	TheWritableGlobalData->m_gfxBackendD3D11 = FALSE;
	return 1;
}

// W3DNext renderer port: -w3dNextBWFilter <logicframe> deterministically engages
// the mission BW screen filter at that game-logic frame (screen-filter oracle
// hook; see W3DDisplay::draw).
Int parseZPBWFilter(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr && args[1][0] != '-')
	{
		TheWritableGlobalData->m_w3dNextBWFilterAtFrame = atoi(args[1]);
		return 2;
	}
	TheWritableGlobalData->m_w3dNextBWFilterAtFrame = 60;
	return 1;
}

// Project STRATAGEM: assign a Strategist personality template to skirmish AIs.
// Usage: -stratagemAI <name>  (rommel | montgomery | eisenhower | giap | zhukov).
// When set, each skirmish AI gets that personality+planner (see Player::setPlayerType);
// when unset, AIs are unchanged stock behavior.
Int parseStratagemAI(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr && args[1][0] != '-')
	{
		StratagemAISetTemplateId( args[1] );
		return 2;
	}
	return 1;
}

// Project STRATAGEM: fixed match seed for the auto-capture harness (default 1337).
// Vary it across -multiInstance runs to get independent deterministic samples.
Int parseStratagemSeed(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr)
	{
		StratagemAISetSeed( atoi( args[1] ) );
		return 2;
	}
	return 1;
}

// Project STRATAGEM: match length in capture samples (default 16). Larger values run
// the AI-vs-baseline match long enough to reach combat for the fitness metric.
Int parseStratagemShots(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr)
	{
		StratagemAISetShots( atoi( args[1] ) );
		return 2;
	}
	return 1;
}

// Project STRATAGEM: assign the subject skirmish AI an ARBITRARY 14-trait vector (the
// optimization search space) as comma-separated 0..1 values, in StratagemTraits order:
//   aggression,economy,techPriority,riskTolerance,harassment,expansion,defensiveness,
//   adaptiveness,scouting,commitDiscipline,randomness,buildRigidity,tempo,grudge
// Takes precedence over -stratagemAI. e.g. -stratagemTraits 0.85,0.3,0.3,0.7,0.8,...(14)
Int parseStratagemTraits(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr)
	{
		Real t[14];
		Int n = 0;
		const char *p = args[1];
		while (*p && n < 14)
		{
			t[n++] = (Real)atof( p );
			while (*p && *p != ',') ++p;   // skip to next comma
			if (*p == ',') ++p;
		}
		if (n == 14)
			StratagemAISetTraits( t, 14 );
		return 2;
	}
	return 1;
}

// Project STRATAGEM: test hook - stamp a Strategist roster index (0..N-1, or -1 none) onto
// the harness's skirmish slot 1, exercising the per-slot lobby assignment path headlessly.
Int parseStratagemSlotAI(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr)
	{
		StratagemAISetSlotStrategist( atoi( args[1] ) );
		return 2;
	}
	return 1;
}

// Project STRATAGEM: pin both harness AIs to one faction (e.g. "China") for controlled matchups.
Int parseStratagemFaction(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr && args[1][0] != '-') { StratagemAISetFaction( args[1] ); return 2; }
	return 1;
}

// Project STRATAGEM: set the slot-2 opponent general by name (training harness, e.g. "rommel").
Int parseStratagemOpponentAI(char *args[], int num)
{
	if (num > 1 && args[1] != nullptr && args[1][0] != '-') { StratagemAISetOpponent( args[1] ); return 2; }
	return 1;
}

Int parseReplay(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString filename = args[1];
		if (!filename.endsWithNoCase(RecorderClass::getReplayExtention()))
		{
			printf("Invalid replay name \"%s\"\n", filename.str());
			exit(1);
		}
		TheWritableGlobalData->m_simulateReplays.push_back(filename);

		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
		TheWritableGlobalData->m_shellMapOn = FALSE;

		// Make replay playback possible while other clients (possible retail) are running
		rts::ClientInstance::setMultiInstance(TRUE);
		rts::ClientInstance::skipPrimaryInstance();

		return 2;
	}
	return 1;
}

Int parseJobs(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_simulateReplayJobs = atoi(args[1]);
		if (TheGlobalData->m_simulateReplayJobs < SIMULATE_REPLAYS_SEQUENTIAL || TheGlobalData->m_simulateReplayJobs == 0)
		{
			printf("Invalid number of jobs: %d\n", TheGlobalData->m_simulateReplayJobs);
			exit(1);
		}
		return 2;
	}
	return 1;
}

Int parseXRes(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

#if defined(RTS_DEBUG)
//=============================================================================
//=============================================================================
Int parseLatencyAverage(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyAverage = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyAmplitude(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyAmplitude = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyPeriod(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyPeriod = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyNoise(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyNoise = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parsePacketLoss(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_packetLoss = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoDynamicLOD(char *args[], int num)
{
	TheWritableGlobalData->m_enableDynamicLOD = FALSE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	TheWritableGlobalData->m_enableStaticLOD = FALSE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPSLimit(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	TheWritableGlobalData->m_useCameraConstraints = FALSE;

	return 1;
}

Int parseWireframe(char *args[], int)
{
	TheWritableGlobalData->m_wireframe = TRUE;

	return 1;
}

Int parseShowCollision(char *args[], int)
{
	TheWritableGlobalData->m_showCollisionExtents = TRUE;

	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	TheWritableGlobalData->m_showClientPhysics = FALSE;

	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	TheWritableGlobalData->m_showTerrainNormals = TRUE;

	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	TheWritableGlobalData->m_stateMachineDebug = TRUE;

	return 1;
}

Int parseJabber(char *args[], int)
{
	TheWritableGlobalData->m_jabberOn = TRUE;

	return 1;
}

Int parseMunkee(char *args[], int)
{
	TheWritableGlobalData->m_munkeeOn = TRUE;

	return 1;
}
#endif // defined(RTS_DEBUG)

Int parseScriptDebug(char *args[], int)
{
	TheWritableGlobalData->m_scriptDebug = TRUE;
	TheWritableGlobalData->m_winCursors = TRUE;

	return 1;
}

Int parseParticleEdit(char *args[], int)
{
	TheWritableGlobalData->m_particleEdit = TRUE;
	TheWritableGlobalData->m_winCursors = TRUE;
	TheWritableGlobalData->m_windowed = TRUE;

	return 1;
}


Int parseBuildMapCache(char *args[], int)
{
	TheWritableGlobalData->m_buildMapCache = true;

	return 1;
}


#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
Int parsePreload( char *args[], int num )
{
	TheWritableGlobalData->m_preloadAssets = TRUE;

	return 1;
}
#endif


#if defined(RTS_DEBUG)
Int parseDisplayDebug(char *args[], int)
{
	TheWritableGlobalData->m_displayDebug = TRUE;

	return 1;
}

Int parseFile(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}


Int parsePreloadEverything( char *args[], int num )
{
	TheWritableGlobalData->m_preloadAssets = TRUE;
	TheWritableGlobalData->m_preloadEverything = TRUE;

	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	FILE *logfile=fopen("PreloadedAssets.txt","w");
	if (logfile)	//clear the file
		fclose(logfile);
	TheWritableGlobalData->m_preloadReport = TRUE;

	return 1;
}

/// begin stuff for VTUNE
Int parseVTune ( char *args[], int num )
{
	TheWritableGlobalData->m_vTune = TRUE;

	return 1;
}
/// end stuff for VTUNE

#endif // defined(RTS_DEBUG)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	TheWritableGlobalData->m_useFX = FALSE;

	return 1;
}

#if defined(RTS_DEBUG) && ENABLE_CONFIGURABLE_SHROUD
Int parseNoShroud(char *args[], int)
{
	TheWritableGlobalData->m_shroudOn = FALSE;

	return 1;
}
#endif

Int parseForceBenchmark(char *args[], int)
{
	TheWritableGlobalData->m_forceBenchmark = TRUE;

	return 1;
}

Int parseNoMoveCamera(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraMovement = true;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseNoCinematic(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraMovement = true;
	TheWritableGlobalData->m_disableMilitaryCaption = true;
	TheWritableGlobalData->m_disableCameraFade = true;
	TheWritableGlobalData->m_disableScriptedInputDisabling = true;

	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	TheDebugIgnoreSyncErrors = true;

	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	TheWritableGlobalData->m_shellMapOn = FALSE;

	return 1;
}

Int parseNoShaders(char *args[], int)
{
	TheWritableGlobalData->m_chipSetType = 1;	//force to a voodoo card which uses least amount of features.

	return 1;
}

Int parseNoLogo(char *args[], int)
{
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;

	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	TheWritableGlobalData->m_animateWindows = FALSE;

	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	TheWritableGlobalData->m_winCursors = TRUE;

	return 1;
}

Int parseQuickStart( char *args[], int num )
{
	parseNoLogo( args, num );
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	TheWritableGlobalData->m_constantDebugUpdate = TRUE;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseExtraLogging( char *args[], int num )
{
	TheWritableGlobalData->m_extraLogging = TRUE;

	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	TheWritableGlobalData->m_allAdvice = TRUE;

	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	TheWritableGlobalData->m_showTeamDot = TRUE;

	return 1;
}


#if defined(RTS_DEBUG)
Int parseSelectAll( char *args[], int num )
{
	TheWritableGlobalData->m_allowUnselectableSelection = TRUE;

	return 1;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	TheWritableGlobalData->m_incrementalAGPBuf = TRUE;

	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	TheWritableGlobalData->m_loadScreenDemo = TRUE;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseSaveStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseSaveAllStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseLocalMOTD(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseCameraDebug(char *args[], int num)
{
	TheWritableGlobalData->m_debugCamera = TRUE;

	return 1;
}
#endif

#if defined(RTS_DEBUG)
Int parseBenchmark(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_dumpStatsAtInterval = TRUE;
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
	}
	return 2;
}
#endif
#endif

#ifdef DEBUG_CRASHING
Int parseIgnoreAsserts(char *args[], int num)
{
	if (num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#ifdef DEBUG_STACKTRACE
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

Int parseNoFPSLimit(char *args[], int num)
{
	TheWritableGlobalData->m_useFpsLimit = false;
	TheWritableGlobalData->m_framesPerSecondLimit = 30000;

	return 1;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	TheWritableGlobalData->m_dumpAssetUsage = true;

	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;

	return 1;
}

Int parseMod(char *args[], Int num)
{
	if (num > 1)
	{
		AsciiString modPath = args[1];
		if (strchr(modPath.str(), ':') || modPath.startsWith("/") || modPath.startsWith("\\"))
		{
			// full path passed in.  Don't append base path.
		}
		else
		{
			modPath.format("%s%s", TheGlobalData->getPath_UserData().str(), args[1]);
		}
		DEBUG_LOG(("Looking for mod '%s'", modPath.str()));

		if (!TheLocalFileSystem->doesFileExist(modPath.str()))
		{
			DEBUG_LOG(("Mod does not exist."));
			return 2; // no such file/dir.
		}

		// now check for dir-ness
		struct _stat statBuf;
		if (_stat(modPath.str(), &statBuf) != 0)
		{
			DEBUG_LOG(("Could not _stat() mod."));
			return 2; // could not stat the file/dir.
		}

		if (statBuf.st_mode & _S_IFDIR)
		{
			if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
				modPath.concat('\\');
			DEBUG_LOG(("Mod dir is '%s'.", modPath.str()));
			TheWritableGlobalData->m_modDir = modPath;
		}
		else
		{
			DEBUG_LOG(("Mod file is '%s'.", modPath.str()));
			TheWritableGlobalData->m_modBIG = modPath;
		}

		return 2;
	}
	return 1;
}

#ifdef DEBUG_LOGGING
Int parseSetDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask |= 1<<i;
				break;
			}
		}
	}
	return 2;
}

Int parseClearDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask &= ~(1<<i);
				break;
			}
		}
	}
	return 2;
}
#endif

// Initial Params are parsed before Windows Creation.
// Note that except for TheGlobalData, no other global objects exist yet when these are parsed.
static CommandLineParam paramsForStartup[] =
{
	{ "-win", parseWin },
	{ "-exclusive", parseExclusive },
	{ "-multiInstance", parseMultiInstance },
	{ "-fullscreen", parseNoWin },

	// TheSuperHackers @feature helmutbuhler 11/04/2025
	// This runs the game without a window, graphics, input and audio. You can combine this with -replay
	{ "-headless", parseHeadless },

	// TheSuperHackers @feature helmutbuhler 13/04/2025
	// Play back a replay. Pass the filename including .rep afterwards.
	// You can pass this multiple times to play back multiple replays.
	// You can also include wildcards. The file must be in the replay folder or in a subfolder.
	{ "-replay", parseReplay },

	// TheSuperHackers @feature helmutbuhler 23/05/2025
	// Simulate each replay in a separate process and use 1..N processes at the same time.
	// (If you have 4 cores, call it with -jobs 4)
	// If you do not call this, all replays will be simulated in sequence in the same process.
	{ "-jobs", parseJobs },

	// Project STRATAGEM: unattended AI-skirmish + influence-overlay screenshot capture.
	{ "-stratagemShot", parseStratagemShot },
	// NAVAL Tier-1: unattended skirmish that spawns a gunboat and verifies water movement.
	{ "-navalShot", parseNavalShot },
	// NAVAL demo: AI-vs-AI water sandbox - two allied sides produce + patrol gunboats.
	{ "-navalSandbox", parseNavalSandbox },
	// W3DNext renderer port: pick the render backend (default DX8). -gfxBackend d3d11
	// flips to the D3D11 backend; -d3d11/-dx11 are bare boolean aliases, -dx8
	// explicitly selects the default DX8 backend.
	{ "-gfxBackend", parseGfxBackend },
	{ "-d3d11", parseD3D11 },
	{ "-dx11", parseD3D11 },
	{ "-dx8", parseDX8 },
	// W3DNext renderer port: deterministic BW screen-filter engage (oracle hook).
	{ "-w3dNextBWFilter", parseZPBWFilter },
	// Project STRATAGEM: assign a Strategist personality to skirmish AIs.
	{ "-stratagemAI", parseStratagemAI },
	// Project STRATAGEM: fixed match seed for the auto-capture harness.
	{ "-stratagemSeed", parseStratagemSeed },
	// Project STRATAGEM: match length (capture samples); larger = runs to combat for fitness.
	{ "-stratagemShots", parseStratagemShots },
	// Project STRATAGEM: arbitrary 14-trait vector for the subject AI (sweep search space).
	{ "-stratagemTraits", parseStratagemTraits },
	// Project STRATAGEM: per-slot lobby-path test hook (roster index onto harness slot 1).
	{ "-stratagemSlotAI", parseStratagemSlotAI },
	// Project STRATAGEM: pin both harness AIs to a faction (controlled matchup, e.g. China-v-China).
	{ "-stratagemFaction", parseStratagemFaction },
	// Project STRATAGEM: slot-2 opponent general by name (training harness).
	{ "-stratagemOpponentAI", parseStratagemOpponentAI },
};

// These Params are parsed during Engine Init before INI data is loaded
static CommandLineParam paramsForEngineInit[] =
{
	{ "-nologo", parseNoLogo }, // TheSuperHackers @tweak Is now available in Release builds.
	{ "-noshellmap", parseNoShellMap },
	{ "-noShellAnim", parseNoWindowAnimation }, // TheSuperHackers @tweak Is now available in Release builds.
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullVersion", parseFullVersion },
	{ "-particleEdit", parseParticleEdit },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-mod", parseMod },
	{ "-noshaders", parseNoShaders },
	{ "-quickstart", parseQuickStart },
	{ "-useWaveEditor", parseUseWaveEditor },

	// TheSuperHackers @feature xezon 03/08/2025 Force full viewport for 'Control Bar Pro' Addons like GenTool did it.
	{ "-forcefullviewport", parseFullViewport },

#if defined(RTS_DEBUG)
	{ "-noaudio", parseNoAudio },
	{ "-map", parseMapName },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats },
#endif
	{ "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
#endif
#ifdef DEBUG_CRC
	// TheSuperHackers @info helmutbuhler 04/09/2025
	// The following arguments are useful for CRC debugging.
	// Note that you need to have a debug or internal configuration build in order to use this.
	// Release configuration also works if RELEASE_DEBUG_LOGGING is defined in Debug.h
	// Also note that all players need to play in the same configuration, otherwise mismatch will
	// occur almost immediately.
	// Try this if you want to play the game and have useful debug information in case mismatch occurs:
	// -ignoreAsserts -DebugCRCFromFrame 0 -VerifyClientCRC -LogObjectCRCs -NetCRCInterval 1
	// After mismatch occurs, you can examine the logfile and also reproduce the crc from the replay with this (and diff that with the log):
	// -ignoreAsserts -DebugCRCFromFrame xxx -LogObjectCRCs -SaveDebugCRCPerFrame crc

	// After which frame to log crc logging. Call with 0 to log all frames and with -1 to log none (default).
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },

	// Last frame to log
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },

	// Save data involving CRC calculation to a binary file. (This isn't that useful.)
	{ "-KeepCRCSaves", parseKeepCRCSave },

	// TheSuperHackers @feature helmutbuhler 04/09/2025
	// Store CRC Debug Logging into a separate file for each frame.
	// Pass the foldername after this where those files are to be stored.
	// This is useful for replay analysis.
	// Note that the passed folder is deleted if it already exists for every started game.
	{ "-SaveDebugCRCPerFrame", parseSaveDebugCRCPerFrame },

	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },

	// Verify that Game Logic CRC doesn't change during client update.
	// Client update is only for visuals and not supposed to change the crc.
	// (This is implemented using CRCVerification class in GameEngine::update)
	{ "-VerifyClientCRC", parseVerifyClientCRC },

	// Write out binary crc data pre and post client update to "clientPre.crc" and "clientPost.crc"
	{ "-ClientDeepCRC", parseClientDeepCRC },

	// Log CRC of Objects and Weapons (See Object::crc and Weapon::crc)
	{ "-LogObjectCRCs", parseLogObjectCRCs },

	// Number of frames between each CRC check between all players in multiplayer games
	// (if not all crcs are equal, mismatch occurs).
	{ "-NetCRCInterval", parseNetCRCInterval },

	// Number of frames between each CRC that is written to replay files in singleplayer games.
	{ "-ReplayCRCInterval", parseReplayCRCInterval },
#endif
#if defined(RTS_DEBUG)
	{ "-saveAllStats", parseSaveAllStats },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-packetloss", parsePacketLoss },
	{ "-latAvg", parseLatencyAverage },
	{ "-latAmp", parseLatencyAmplitude },
	{ "-latPeriod", parseLatencyPeriod },
	{ "-latNoise", parseLatencyNoise },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-fps", parseFPSLimit },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },
	{ "-file", parseFile },

//	{ "-preload", parsePreload },

	{ "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-logToCon", parseLogToConsole },
	{ "-vTune", parseVTune },
	{ "-selectTheUnselectable", parseSelectAll },
#if ENABLE_CONFIGURABLE_SHROUD
	{ "-noshroud", parseNoShroud },
#endif
	{ "-forceBenchmark", parseForceBenchmark },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-shellmap", parseShellMap },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-seed", parseSeed },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },
#endif

#ifdef DEBUG_LOGGING
	{ "-setDebugLevel", parseSetDebugLevel },
	{ "-clearDebugLevel", parseClearDebugLevel },
#endif

#ifdef DEBUG_CRASHING
	{ "-ignoreAsserts", parseIgnoreAsserts },
#endif

#ifdef DEBUG_STACKTRACE
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
#endif

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	{ "-preload", parsePreload },
#endif


};

char *nextParam(char *newSource, const char *seps)
{
	static char *source = nullptr;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return nullptr;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = nullptr;
		}
		else
		{
			source = nullptr;
		}

		if (first && !*first)
			first = nullptr;
	}

	return first;
}

static void parseCommandLine(const CommandLineParam* params, int numParams)
{
	std::vector<char*> argv;

	std::string cmdLine = GetCommandLineA();
	char *token = nextParam(&cmdLine[0], "\" ");
	while (token != nullptr)
	{
		argv.push_back(strtrim(token));
		token = nextParam(nullptr, "\" ");
	}
	int argc = argv.size();

	int arg = 1;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG_RAW(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accommodate multi-arg parameters, e.g. "-p 1234".
	while (arg<argc)
	{
		// Look at arg #i
		Bool found = false;
		for (int param=0; !found && param<numParams; ++param)
		{
			int len = strlen(params[param].name);
			int len2 = strlen(argv[arg]);
			if (len2 != len)
				continue;
			if (strnicmp(argv[arg], params[param].name, len) == 0)
			{
				arg += params[param].func(&argv[0]+arg, argc-arg);
				found = true;
				break;
			}
		}
		if (!found)
		{
			arg++;
		}
	}
}

void createGlobalData()
{
	if (TheGlobalData == nullptr)
		TheWritableGlobalData = NEW GlobalData;
}

void CommandLine::parseCommandLineForStartup()
{
	// We need the GlobalData initialized before parsing the command line.
	// Note that this function is potentially called multiple times and only initializes the first time.
	createGlobalData();

	if (TheGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup)
		return;
	TheWritableGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup = true;

	parseCommandLine(paramsForStartup, ARRAY_SIZE(paramsForStartup));
}

void CommandLine::parseCommandLineForEngineInit()
{
	createGlobalData();

	DEBUG_ASSERTCRASH(TheGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup,
		("parseCommandLineForStartup is expected to be called before parseCommandLineForEngineInit\n"));
	DEBUG_ASSERTCRASH(!TheGlobalData->m_commandLineData.m_hasParsedCommandLineForEngineInit,
		("parseCommandLineForEngineInit is expected to be called once only\n"));
	TheWritableGlobalData->m_commandLineData.m_hasParsedCommandLineForEngineInit = true;

	parseCommandLine(paramsForEngineInit, ARRAY_SIZE(paramsForEngineInit));
}
