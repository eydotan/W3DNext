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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/FramePacer.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/OptionPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/Stratagem/StratagemBrain.h"
#include "GameLogic/Stratagem/StratagemStrategist.h"   // STRATAGEM: -stratagemSeed + persona-to-first-AI
#include "GameLogic/Stratagem/StratagemTemplates.h"     // STRATAGEM: roster-index-by-name for the opponent flag
#include "GameNetwork/GameInfo.h"      // STRATAGEM auto-capture: skirmish setup
#include "GameClient/Display.h"        // STRATAGEM auto-capture: takeScreenShot()
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/Object.h"                 // NAVAL: spawn + query the test gunboat
#include "GameLogic/TerrainLogic.h"           // NAVAL: isUnderwater()/getExtent() water probing
#include "GameLogic/Module/AIUpdate.h"        // NAVAL: aiMoveToPosition() programmatic move order
#include "GameLogic/Module/ProductionUpdate.h" // NAVAL sandbox: queueCreateUnit() at a War Factory
#include "Common/Player.h"                    // NAVAL: local player's default team
#include "Common/PlayerList.h"                // NAVAL sandbox: iterate players
#include "Common/Money.h"                     // NAVAL sandbox: deposit cash
#include "Common/KindOf.h"                    // NAVAL sandbox: KINDOF_STRUCTURE
#include "Common/ThingTemplate.h"             // NAVAL: ChinaGunboat template lookup
#include "GameClient/View.h"                  // NAVAL sandbox: TheTacticalView->lookAt() the lake

#include "GameClient/ClientInstance.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/version.h"
#include "gitinfo.h"


//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = nullptr;

void DeepCRCSanityCheck::reset()
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = nullptr;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = nullptr;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(
	SUBSYSTEM*& sysref,
	AsciiString name,
	SUBSYSTEM* sys,
	Xfer *pXfer,
	const char* path1 = nullptr,
	const char* path2 = nullptr)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern CComModule _Module;

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

//-------------------------------------------------------------------------------------------------
static void updateWindowTitle()
{
	// TheSuperHackers @tweak Now prints product and version information in the Window title.

	DEBUG_ASSERTCRASH(TheVersion != nullptr, ("TheVersion is null"));
	DEBUG_ASSERTCRASH(TheGameText != nullptr, ("TheGameText is null"));

	UnicodeString title;

	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		UnicodeString str;
		str.format(L"Instance:%.2u", rts::ClientInstance::getInstanceId());
		title.concat(str);
	}

	UnicodeString productString = TheVersion->getUnicodeProductString();

	if (!productString.isEmpty())
	{
		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(productString);
	}

#if RTS_GENERALS
	const WideChar* defaultGameTitle = L"Command and Conquer Generals";
#elif RTS_ZEROHOUR
	const WideChar* defaultGameTitle = L"Command and Conquer Generals Zero Hour";
#endif
	UnicodeString gameTitle = TheGameText->FETCH_OR_SUBSTITUTE("GUI:Command&ConquerGenerals", defaultGameTitle);

	if (!gameTitle.isEmpty())
	{
		UnicodeString gameTitleFinal;
		UnicodeString gameVersion = TheVersion->getUnicodeVersion();

		if (productString.isEmpty())
		{
			gameTitleFinal = gameTitle;
		}
		else
		{
			UnicodeString gameTitleFormat = TheGameText->FETCH_OR_SUBSTITUTE("Version:GameTitle", L"for %ls");
			gameTitleFinal.format(gameTitleFormat.str(), gameTitle.str());
		}

		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(gameTitleFinal.str());
		title.concat(L" ");
		title.concat(gameVersion.str());
	}

	// W3DNext: stamp the source git branch (and '*' when built from a
	// dirty tree) into the window title so game windows launched from
	// different worktree builds (e.g. main vs renderer-d3d11) are
	// distinguishable in the taskbar and alt-tab list. (Not gated on
	// GitHaveInfo: that flag is false whenever any optional git probe fails —
	// e.g. `git describe --tags` in a tagless repo — while the branch probe
	// itself has an empty-string fallback that this check already covers.)
	if (GitBranch[0] != '\0')
	{
		UnicodeString branchU;
		branchU.translate(AsciiString(GitBranch));
		UnicodeString branchTag;
		branchTag.format(L" [%ls%ls]", branchU.str(), GitUncommittedChanges ? L"*" : L"");
		title.concat(branchTag);
	}

	if (!title.isEmpty())
	{
		AsciiString titleA;
		titleA.translate(title);	//get ASCII version for Win 9x

		extern HWND ApplicationHWnd;  ///< our application window handle
		if (ApplicationHWnd) {
			//Set it twice because Win 9x does not support SetWindowTextW.
			::SetWindowText(ApplicationHWnd, titleA.str());
			::SetWindowTextW(ApplicationHWnd, title.str());
		}
	}
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine()
{
	// initialize to non garbage values
	m_logicTimeAccumulator = 0.0f;
	m_quitting = FALSE;
	m_isActive = FALSE;

	_Module.Init(nullptr, ApplicationHInstance, nullptr);
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	delete TheMapCache;
	TheMapCache = nullptr;

//	delete TheShell;
//	TheShell = nullptr;

	TheGameResultsQueue->endThreads();

	// TheSuperHackers @fix helmutbuhler 03/06/2025
	// Reset all subsystems before deletion to prevent crashing due to cross dependencies.
	reset();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = nullptr;

	delete TheSkirmishGameInfo;
	TheSkirmishGameInfo = nullptr;

	delete TheChallengeGameInfo;
	TheChallengeGameInfo = nullptr;

	delete TheNetwork;
	TheNetwork = nullptr;

	delete TheCommandList;
	TheCommandList = nullptr;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = nullptr;

	delete TheFileSystem;
	TheFileSystem = nullptr;

	delete TheGameLODManager;
	TheGameLODManager = nullptr;

	Drawable::killStaticImages();

	_Module.Term();

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isTimeFrozen()
{
	// TheSuperHackers @fix The time can no longer be frozen in Network games. It would disconnect the player.
	if (TheNetwork != nullptr)
		return false;

	if (TheTacticalView != nullptr)
	{
		if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
			return true;
	}

	if (TheScriptEngine != nullptr)
	{
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript())
			return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isGameHalted()
{
	if (TheNetwork != nullptr)
	{
		if (TheNetwork->isStalling())
			return true;
	}
	else
	{
		if (TheGameLogic != nullptr && TheGameLogic->isGamePaused())
			return true;
	}

	return false;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init()
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================"));
			DEBUG_LOG(("Generals version %s", TheVersion->getAsciiVersion().str()));
			DEBUG_LOG(("Build date: %s", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Build user: %s", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("Build git revision: %s", TheVersion->getAsciiGitCommitCount().str()));
			DEBUG_LOG(("Build git version: %s", TheVersion->getAsciiGitTagOrHash().str()));
			DEBUG_LOG(("Build git commit time: %s", TheVersion->getAsciiGitCommitTime().str()));
			DEBUG_LOG(("Build git commit author: %s", Version::getGitCommitAuthorName()));
			DEBUG_LOG(("================================================================================"));
		}
#endif

	#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers."));
		InitPrecisionTimer();
	#endif
	#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////


		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;

		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCommandList  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), nullptr);


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), nullptr); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		DEBUG_ASSERTCRASH(TheWritableGlobalData,("TheWritableGlobalData expected to be created"));
		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", TheWritableGlobalData, &xferCRC, "Data\\INI\\Default\\GameData", "Data\\INI\\GameData");
		TheWritableGlobalData->parseCustomDefinition();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(RTS_DEBUG)
		// If we're in Debug, load the Debug settings as well.
		ini.loadFileDirectory( "Data\\INI\\GameDataDebug", INI_LOAD_OVERWRITE, nullptr );
	#endif

		// special-case: parse command-line parameters after loading global data
		CommandLine::parseCommandLineForEngineInit();

		TheArchiveFileSystem->loadMods();

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();

		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.loadFileDirectory( "Data\\INI\\Default\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Default\\Weather", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Weather", INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After water INI's = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, nullptr);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), nullptr);
		updateWindowTitle();

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameText = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0xA1E7F8E6)
			TheNameKeyGenerator->verifyNameKeyID(1);
#endif

		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science", "Data\\INI\\Science");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer", "Data\\INI\\Multiplayer");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain", "Data\\INI\\Terrain");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads", "Data\\INI\\Roads");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, nullptr); // must be before the game text
		TheGlobalLanguageData->parseCustomDefinition();
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGlobalLanguageData = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", createAudioManager(TheGlobalData->m_headless), nullptr);

#if RTS_ZEROHOUR && RETAIL_COMPATIBLE_CRC
		TheNameKeyGenerator->syncNameKeyID();
#endif

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), nullptr);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), nullptr);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), nullptr);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), nullptr);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), nullptr);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, nullptr, "Data\\INI\\Rank");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(TheGlobalData->m_headless), nullptr);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, nullptr, "Data\\INI\\Weapon");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, nullptr, "Data\\INI\\Locomotor");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower");
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, nullptr, "Data\\INI\\DamageFX");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, nullptr, "Data\\INI\\Armor");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheBuildAssistant = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object", "Data\\INI\\Object");

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheThingFactory = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0x6209AF6E)
			TheNameKeyGenerator->verifyNameKeyID(2265);
#endif

		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameClient = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData", "Data\\INI\\AIData");
		// Project STRATAGEM strategic-AI subsystem. No INI/save state (the world model
		// is recomputable); ships disabled, so this is inert until task D2.
		initSubsystem(TheStratagemBrain,"TheStratagemBrain", MSGNEW("GameEngineSubsystem") StratagemBrain(), nullptr);
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(), nullptr);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), nullptr);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), nullptr);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), nullptr);
		initSubsystem(TheRadar,"TheRadar", createRadar(TheGlobalData->m_headless), nullptr);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), nullptr);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheVictoryConditions = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), nullptr, fname.str(), "Data\\INI\\CommandMap");

#if defined(RTS_DEBUG)
		ini.loadFileDirectory("Data\\INI\\CommandMapDebug", INI_LOAD_MULTIFILE, nullptr);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.loadFileDirectory("Data\\INI\\CommandMapDemo", INI_LOAD_MULTIFILE, nullptr);
#endif

		TheMetaMap->generateMetaMap();
		TheMetaMap->verifyMetaMap();


		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), nullptr);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), nullptr);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, nullptr );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, nullptr );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		TheFramePacer->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);

		// We're not in a network game yet, so set the network singleton to nullptr.
		TheNetwork = nullptr;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(nullptr, true, true);
			m_quitting = TRUE;
		}

		// load the initial shell screen
		//TheShell->push( "Menus/MainMenu.wnd" );

		// This allows us to run a map from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
		}

		// Project STRATAGEM auto-capture (-stratagemShot): construct an AI skirmish
		// (local idle human vs. one Brutal AI) without the menus and launch it, so the
		// influence-overlay heatmap can be screenshotted unattended. Mirrors the non-GUI
		// parts of SkirmishGameOptionsMenuInit + reallyDoStart. Degrades gracefully (no
		// crash) if no valid multiplayer map is available.
		if ((TheGlobalData->m_stratagemShot || TheGlobalData->m_navalShot || TheGlobalData->m_navalSandbox) && TheMapCache)
		{
			AsciiString mapName = TheGlobalData->m_stratagemShotMap;
			if (TheGlobalData->m_navalSandbox)   mapName = TheGlobalData->m_navalSandboxMap;
			else if (TheGlobalData->m_navalShot) mapName = TheGlobalData->m_navalShotMap;
			const MapMetaData *md = mapName.isEmpty() ? nullptr : TheMapCache->findMap(mapName);
			if (md == nullptr)
			{
				// pick the first multiplayer map with room for 2 players
				for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
				{
					if (it->second.m_isMultiplayer && it->second.m_numPlayers >= 2)
					{
						mapName = it->first;
						md = &it->second;
						break;
					}
				}
			}

			if (md != nullptr)
			{
				if (TheSkirmishGameInfo == nullptr)
					TheSkirmishGameInfo = NEW SkirmishGameInfo;
				TheSkirmishGameInfo->init();
				TheSkirmishGameInfo->clearSlotList();
				TheSkirmishGameInfo->reset();
				TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot(0)->getIP() );
				TheSkirmishGameInfo->enterGame();

				// Slot 0 is the local slot - the engine requires a valid local HUMAN player,
				// so keep it human but make it a non-participating OBSERVER. That frees slots
				// 1 and 2 to be a clean 1v1 AI-vs-AI: slot 1 gets the -stratagemAI persona
				// (first skirmish AI), slot 2 stays stock = the baseline opponent.
				// STRATAGEM training harness: optional faction pin (-stratagemFaction China) so both
				// AIs play the same faction for a controlled matchup; else RANDOM (default).
				Int stratFactionTmpl = PLAYERTEMPLATE_RANDOM;
				if (StratagemAIGetFaction()[0] != 0 && ThePlayerTemplateStore != nullptr)
				{
					AsciiString fn; fn.format( "Faction%s", StratagemAIGetFaction() );
					Int idx = ThePlayerTemplateStore->getTemplateNumByName( fn );
					if (idx >= 0) stratFactionTmpl = idx;
				}

				GameSlot slot0;
				slot0.setState( SLOT_PLAYER, UnicodeString(L"Observer") );
				slot0.setPlayerTemplate( PLAYERTEMPLATE_OBSERVER );
				slot0.setColor( 0 );
				slot0.setStartPos( -1 );
				TheSkirmishGameInfo->setSlot( 0, slot0 );

				GameSlot slot1;
				slot1.setState( SLOT_BRUTAL_AI, UnicodeString(L"Stratagem") );
				slot1.setPlayerTemplate( stratFactionTmpl );
				slot1.setColor( 1 );
				slot1.setStartPos( 0 );
				slot1.setStrategistId( StratagemAIGetSlotStrategist() );   // -stratagemSlotAI test hook (per-slot path)
				TheSkirmishGameInfo->setSlot( 1, slot1 );

				GameSlot slot2;
				slot2.setState( SLOT_BRUTAL_AI, UnicodeString(L"Baseline") );
				slot2.setPlayerTemplate( stratFactionTmpl );
				slot2.setColor( 2 );
				slot2.setStartPos( 1 );
				// -stratagemOpponentAI: give slot 2 a chosen general (the sparring opponent for training).
				if (StratagemAIGetOpponent()[0] != 0)
				{
					Int oppIdx = StratagemRosterIndexByName( AsciiString( StratagemAIGetOpponent() ) );
					if (oppIdx >= 0) slot2.setStrategistId( oppIdx );
				}
				TheSkirmishGameInfo->setSlot( 2, slot2 );

				// The persona goes to the first skirmish AI created (slot 1); reset the claim
				// so this match assigns it fresh (and slot 2 stays baseline).
				StratagemAIResetAssignment();

				// Fixed seed (configurable via -stratagemSeed) so the whole match - RANDOM
				// faction resolution, start positions, AI decisions - replays identically.
				// Varying the seed gives independent samples for parallel fleets.
				TheSkirmishGameInfo->setSeed( StratagemAIGetSeed() );
				TheSkirmishGameInfo->setMap( mapName );
				TheSkirmishGameInfo->setMapCRC( md->m_CRC );
				TheSkirmishGameInfo->setMapSize( md->m_filesize );

				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
				TheSkirmishGameInfo->startGame( 0 );
				InitRandom( TheSkirmishGameInfo->getSeed() );

				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument( GAME_SKIRMISH );
				msg->appendIntegerArgument( DIFFICULTY_NORMAL );
				msg->appendIntegerArgument( 0 );
				msg->appendIntegerArgument( 30 );   // FPS limit

				DEBUG_LOG(("STRATAGEM auto-capture: starting AI skirmish on '%s'", mapName.str()));
			}
			else
			{
				DEBUG_LOG(("STRATAGEM auto-capture: no valid multiplayer map in cache; not starting a game."));
			}
		}

		//
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if(!TheGlobalData->m_playIntro)
			TheWritableGlobalData->m_afterIntro = TRUE;

	}
	catch (ErrorCode ec)
	{
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if(!TheGlobalData->m_playIntro)
		TheWritableGlobalData->m_afterIntro = TRUE;

	resetSubsystems();

	HideControlBar();
}

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data
	*/
void GameEngine::reset()
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	resetSubsystems();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting null TheNetwork!"));
		delete TheNetwork;
		TheNetwork = nullptr;
	}
	if(background)
	{
		background->destroyWindows();
		deleteInstance(background);
		background = nullptr;
	}
}

/// -----------------------------------------------------------------------------------------------
void GameEngine::resetSubsystems()
{
	// TheSuperHackers @fix xezon 09/06/2025 Reset GameLogic first to purge all world objects early.
	// This avoids potentially catastrophic issues when objects and subsystems have cross dependencies.
	TheGameLogic->reset();

	TheSubsystemList->resetAll();
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateGameLogic(UnsignedInt logicTimeQueryFlags)
{
	// Must be first.
	TheGameLogic->preUpdate();

	TheFramePacer->setTimeFrozen(isTimeFrozen());
	TheFramePacer->setGameHalted(isGameHalted());

	if (TheNetwork != nullptr)
	{
		return canUpdateNetworkGameLogic();
	}
	else
	{
		return canUpdateRegularGameLogic(logicTimeQueryFlags);
	}
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateNetworkGameLogic()
{
	DEBUG_ASSERTCRASH(TheNetwork != nullptr, ("TheNetwork is null"));

	if (TheNetwork->isFrameDataReady())
	{
		// Important: The Network is definitely no longer stalling.
		TheFramePacer->setGameHalted(false);

		return true;
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateRegularGameLogic(UnsignedInt logicTimeQueryFlags)
{
	const Int logicTimeScaleFps = TheFramePacer->getActualLogicTimeScaleFps(logicTimeQueryFlags);
	const Int maxRenderFps = TheFramePacer->getActualFramesPerSecondLimit();

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode;
#else	//always allow this cheat key if we're in a replay game.
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif

	if (useFastMode || logicTimeScaleFps >= maxRenderFps)
	{
		// Logic time scale is uncapped or larger equal Render FPS. Update straight away.
		return true;
	}
	else
	{
		// TheSuperHackers @tweak xezon 06/08/2025
		// The logic time step is now decoupled from the render update.
		const Real targetFrameTime = 1.0f / logicTimeScaleFps;
		m_logicTimeAccumulator += min(TheFramePacer->getUpdateTime(), targetFrameTime);

		if (m_logicTimeAccumulator >= targetFrameTime)
		{
			m_logicTimeAccumulator -= targetFrameTime;
			return true;
		}
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 */
void GameEngine::update()
{
	USE_PERF_TIMER(GameEngine_update)
	{
		{
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->UPDATE();

			/// @todo Move audio init, update, etc, into GameClient update

			TheAudio->UPDATE();
			TheGameClient->UPDATE();
			TheMessageStream->propagateMessages();

			if (TheNetwork != nullptr)
			{
				TheNetwork->UPDATE();
			}
		}

		const Bool canUpdate = canUpdateGameLogic(FramePacer::IgnoreFrozenTime | FramePacer::IgnoreHaltedGame);
		const Bool canUpdateLogic = canUpdate && !TheFramePacer->isGameHalted() && !TheFramePacer->isTimeFrozen();
		const Bool canUpdateScript = canUpdate && !TheFramePacer->isGameHalted();

		if (canUpdateLogic)
		{
			TheGameClient->step();
			TheGameLogic->UPDATE();
		}
		else if (canUpdateScript)
		{
			// TheSuperHackers @info Still update the Script Engine to allow
			// for scripted camera movements while the time is frozen.
			TheScriptEngine->UPDATE();
		}
	}
}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits.
 */
void GameEngine::execute()
{
#if defined(RTS_DEBUG)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	while( !m_quitting )
	{

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(RTS_DEBUG)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif

			// Project STRATAGEM auto-capture: once a real match is running, force the
			// influence overlay on. The actual screenshot capture + auto-exit happen in
			// W3DDisplay::draw() right after WW3D::End_Render(), because the debug-icon
			// overlay is flushed via the static sort list during the scene render and is
			// NOT yet in the back buffer when the game loop reads it here.
			if (TheGlobalData->m_stratagemShot && TheGameLogic && TheGameLogic->isInGame()
					&& !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap())
			{
				TheWritableGlobalData->m_debugAI = AI_DEBUG_STRATAGEM_INFLUENCE;
			}

			// NAVAL Tier-1 verification (-navalShot): once the match is live, spawn a
			// ChinaGunboat on a water cell, order it across the lake, and DEBUG_LOG whether
			// it actually traverses water - then quit. All result lines are prefixed
			// "NAVALSHOT" so scripts/naval_shot.ps1 can grep the verdict. Fully unattended.
			if (TheGlobalData->m_navalShot && TheGameLogic && TheGameLogic->isInGame()
					&& !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap())
			{
				enum NavalPhase { NAVAL_SETTLE, NAVAL_MOVING, NAVAL_FINISHED };
				static NavalPhase navalPhase = NAVAL_SETTLE;
				static UnsignedInt navalPhaseStart = 0;
				static UnsignedInt navalLastLog = 0;
				static ObjectID navalUnit = INVALID_ID;
				static Coord3D navalSpawn, navalDest;
				// Set once the boat is demonstrably out on open water, well away from the
				// shore it launched from - this is the real proof of water traversal, and
				// avoids an edge-effect false negative when the destination water cell sits
				// right at the lake's rim (the boat resting there can read underwater=0).
				static Bool navalSawWaterTransit = FALSE;

				const UnsignedInt frame = TheGameLogic->getFrame();
				if (navalPhaseStart == 0)
					navalPhaseStart = frame;

				if (navalPhase == NAVAL_SETTLE)
				{
					// give the match ~1s to finish loading objects/players before we poke it
					if (frame > navalPhaseStart + 30)
					{
						// Scan the playable extent for water cells: nearest = spawn, farthest = dest.
						Region3D ext;
						TheTerrainLogic->getExtent(&ext);
						const Real step = 40.0f;
						Bool found = FALSE;
						Real bestDistSq = -1.0f;
						for (Real y = ext.lo.y; y <= ext.hi.y; y += step)
						{
							for (Real x = ext.lo.x; x <= ext.hi.x; x += step)
							{
								Real waterZ = 0.0f, terrainZ = 0.0f;
								if (TheTerrainLogic->isUnderwater(x, y, &waterZ, &terrainZ))
								{
									if (!found)
									{
										navalSpawn.x = x; navalSpawn.y = y; navalSpawn.z = waterZ;
										navalDest = navalSpawn;
										found = TRUE;
										bestDistSq = 0.0f;
									}
									else
									{
										Real dx = x - navalSpawn.x, dy = y - navalSpawn.y;
										Real d2 = dx*dx + dy*dy;
										if (d2 > bestDistSq)
										{
											bestDistSq = d2;
											navalDest.x = x; navalDest.y = y; navalDest.z = waterZ;
										}
									}
								}
							}
						}

						if (!found)
						{
							DEBUG_LOG(("NAVALSHOT RESULT: FAIL - no water cells found on map '%s'", TheGlobalData->m_mapName.str()));
							navalPhase = NAVAL_FINISHED;
							TheGameEngine->setQuitting(TRUE);
						}
						else
						{
							const ThingTemplate *tmpl = TheThingFactory->findTemplate("ChinaGunboat");
							if (tmpl == nullptr)
							{
								DEBUG_LOG(("NAVALSHOT RESULT: FAIL - ChinaGunboat template not found (map.ini override not loaded?)"));
								navalPhase = NAVAL_FINISHED;
								TheGameEngine->setQuitting(TRUE);
							}
							else
							{
								Player *p = ThePlayerList->getLocalPlayer();
								Object *boat = TheThingFactory->newObject(tmpl, p->getDefaultTeam());
								boat->setPosition(&navalSpawn);
								navalUnit = boat->getID();

								Real wz = 0.0f, tz = 0.0f;
								Bool uw = TheTerrainLogic->isUnderwater(navalSpawn.x, navalSpawn.y, &wz, &tz);
								Real span = (Real)sqrt((double)bestDistSq);
								DEBUG_LOG(("NAVALSHOT: spawned ChinaGunboat id=%d at (%.0f,%.0f) underwater=%d layer=%d; dest=(%.0f,%.0f) waterSpan=%.0f",
									(Int)navalUnit, navalSpawn.x, navalSpawn.y, uw ? 1 : 0, (Int)boat->getLayer(),
									navalDest.x, navalDest.y, span));
								(void)uw; (void)span;	// only referenced by DEBUG_LOG, which compiles out in non-logging builds

								AIUpdateInterface *ai = boat->getAIUpdateInterface();
								if (ai)
									ai->aiMoveToPosition(&navalDest, CMD_FROM_AI);
								else
									DEBUG_LOG(("NAVALSHOT: WARNING - gunboat has no AIUpdateInterface; cannot issue move"));

								navalPhase = NAVAL_MOVING;
								navalPhaseStart = frame;
								navalLastLog = frame;
							}
						}
					}
				}
				else if (navalPhase == NAVAL_MOVING)
				{
					Object *boat = TheGameLogic->findObjectByID(navalUnit);
					if (boat == nullptr)
					{
						DEBUG_LOG(("NAVALSHOT RESULT: FAIL - gunboat destroyed/removed at frame %d", frame));
						navalPhase = NAVAL_FINISHED;
						TheGameEngine->setQuitting(TRUE);
					}
					else
					{
						const Coord3D *pos = boat->getPosition();
						Real wz = 0.0f, tz = 0.0f;
						Bool uw = TheTerrainLogic->isUnderwater(pos->x, pos->y, &wz, &tz);
						Real ddx = pos->x - navalDest.x, ddy = pos->y - navalDest.y;
						Real distToDest = (Real)sqrt((double)(ddx*ddx + ddy*ddy));
						Real mdx = pos->x - navalSpawn.x, mdy = pos->y - navalSpawn.y;
						Real moved = (Real)sqrt((double)(mdx*mdx + mdy*mdy));

						// Mark the moment the boat is genuinely under way on water (on a water
						// cell, well clear of its launch point). One such sample is conclusive.
						if (uw && moved > 120.0f)
							navalSawWaterTransit = TRUE;

						// progress trace every ~1s
						if (frame - navalLastLog >= 30)
						{
							DEBUG_LOG(("NAVALSHOT: frame=%d pos=(%.0f,%.0f) underwater=%d layer=%d distToDest=%.0f",
								frame, pos->x, pos->y, uw ? 1 : 0, (Int)boat->getLayer(), distToDest));
							navalLastLog = frame;
						}

						// verdict after ~20s of travel: it traversed open water (sawWaterTransit)
						// AND arrived at the far water destination it was ordered to.
						if (frame > navalPhaseStart + 600)
						{
							Bool reachedWaterDest = (distToDest < 120.0f) && (moved > 80.0f);
							Bool pass = navalSawWaterTransit && reachedWaterDest;
							DEBUG_LOG(("NAVALSHOT RESULT: %s - finalPos=(%.0f,%.0f) sawWaterTransit=%d reachedWaterDest=%d distToDest=%.0f movedFromSpawn=%.0f",
								pass ? "PASS" : "FAIL", pos->x, pos->y, navalSawWaterTransit ? 1 : 0, reachedWaterDest ? 1 : 0, distToDest, moved));
							(void)pass;	// only referenced by DEBUG_LOG, which compiles out in non-logging builds
							navalPhase = NAVAL_FINISHED;
							TheGameEngine->setQuitting(TRUE);
						}
					}
				}
			}

			// NAVAL demo (-navalSandbox): give every active side a War Factory that keeps
			// PRODUCING ChinaGunboats, ally all sides (so there's no combat), and patrol the
			// gunboats around the lake. Runs until the process is killed - meant to be watched
			// / screen-captured in a windowed instance.
			if (TheGlobalData->m_navalSandbox && TheGameLogic && TheGameLogic->isInGame()
					&& !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap())
			{
				enum SbPhase { SB_SETTLE, SB_RUN };
				static SbPhase     sbPhase = SB_SETTLE;
				static UnsignedInt sbStart = 0, sbLastProd = 0, sbLastPatrol = 0;
				static Coord3D     sbWater[2048];
				static Int         sbWaterCount = 0;
				static ObjectID    sbFactory[8];
				static Coord3D     sbCenter;     // centre of the water - where we aim the camera

				const UnsignedInt frame = TheGameLogic->getFrame();
				if (sbStart == 0) sbStart = frame;

				if (sbPhase == SB_SETTLE)
				{
					// let the skirmish place each side's starting base first (~3s)
					if (frame > sbStart + 90)
					{
						// Never end this demo: once we ally all sides below, the "defeat all
						// enemies" check would be satisfied and pop the scoreboard immediately.
						if (TheVictoryConditions) TheVictoryConditions->setVictoryConditions(0);

						Region3D ext;
						TheTerrainLogic->getExtent(&ext);
						for (Real y = ext.lo.y; y <= ext.hi.y && sbWaterCount < 2048; y += 60.0f)
							for (Real x = ext.lo.x; x <= ext.hi.x && sbWaterCount < 2048; x += 60.0f)
							{
								Real wz = 0.0f, tz = 0.0f;
								if (TheTerrainLogic->isUnderwater(x, y, &wz, &tz))
								{
									sbWater[sbWaterCount].x = x; sbWater[sbWaterCount].y = y; sbWater[sbWaterCount].z = wz;
									++sbWaterCount;
								}
							}

						// camera target = average of the water cells (the lake centre)
						sbCenter.x = sbCenter.y = sbCenter.z = 0.0f;
						if (sbWaterCount > 0)
						{
							for (Int wi = 0; wi < sbWaterCount; ++wi)
							{ sbCenter.x += sbWater[wi].x; sbCenter.y += sbWater[wi].y; sbCenter.z += sbWater[wi].z; }
							sbCenter.x /= sbWaterCount; sbCenter.y /= sbWaterCount; sbCenter.z /= sbWaterCount;
						}

						for (Int i = 0; i < 8; ++i) sbFactory[i] = INVALID_ID;
						const ThingTemplate *wfT = TheThingFactory->findTemplate("ChinaWarFactory");
						const ThingTemplate *ppT = TheThingFactory->findTemplate("ChinaPowerPlant");
						Player *neutral = ThePlayerList->getNeutralPlayer();
						Int count = ThePlayerList->getPlayerCount();

						for (Int pi = 0; pi < count && pi < 8; ++pi)
						{
							Player *p = ThePlayerList->getNthPlayer(pi);
							if (!p || p == neutral || !p->isPlayerActive()) continue;

							// base location = first object this side controls (prefer a structure)
							Coord3D basePos; Bool found = FALSE;
							for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
							{
								if (o->getControllingPlayer() == p)
								{
									basePos = *o->getPosition(); found = TRUE;
									if (o->isKindOf(KINDOF_STRUCTURE)) break;
								}
							}
							if (!found) continue;

							p->getMoney()->deposit(1000000, FALSE, FALSE);
							if (ppT)
								for (Int k = 0; k < 2; ++k)
								{
									Object *pw = TheThingFactory->newObject(ppT, p->getDefaultTeam());
									Coord3D pwp = basePos; pwp.x += 60.0f; pwp.y += (k ? 90.0f : -90.0f);
									pw->setPosition(&pwp);
								}
							if (wfT)
							{
								Object *f = TheThingFactory->newObject(wfT, p->getDefaultTeam());
								Coord3D fp = basePos; fp.x += 140.0f;
								f->setPosition(&fp);
								sbFactory[pi] = f->getID();
							}
						}

						// Make every side NEUTRAL to every other (not ALLIES): neutral units don't
						// auto-acquire/shoot each other (so there's no combat), AND neutral sides
						// stay separate "alliances" - allying everyone into one alliance would trip
						// the "single alliance remaining" check and end the match immediately.
						for (Int a = 0; a < count; ++a)
						{
							Player *pa = ThePlayerList->getNthPlayer(a);
							if (!pa || pa == neutral || !pa->isPlayerActive()) continue;
							for (Int b = 0; b < count; ++b)
							{
								if (a == b) continue;
								Player *pb = ThePlayerList->getNthPlayer(b);
								if (!pb || pb == neutral || !pb->isPlayerActive()) continue;
								pa->setPlayerRelationship(pb, NEUTRAL);
							}
						}

						DEBUG_LOG(("NAVALSANDBOX: setup complete - %d water cells", sbWaterCount));
						if (TheTacticalView) TheTacticalView->lookAt(&sbCenter);
						sbPhase = SB_RUN; sbLastProd = frame; sbLastPatrol = frame;
					}
				}
				else // SB_RUN
				{
					// keep the camera looking at the lake (re-aim every ~10s)
					if (TheTacticalView && (frame % 300) == 0) TheTacticalView->lookAt(&sbCenter);

					const ThingTemplate *gbT = TheThingFactory->findTemplate("ChinaGunboat");

					// keep each War Factory producing gunboats (cap ~12 alive per side)
					if (gbT && frame - sbLastProd >= 150)
					{
						for (Int pi = 0; pi < 8; ++pi)
						{
							if (sbFactory[pi] == INVALID_ID) continue;
							Object *f = TheGameLogic->findObjectByID(sbFactory[pi]);
							if (!f) continue;
							Player *owner = f->getControllingPlayer();
							Int mine = 0;
							for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
								if (o->getControllingPlayer() == owner && o->getTemplate() == gbT) ++mine;
							if (mine >= 12) continue;
							ProductionUpdateInterface *pu = f->getProductionUpdateInterface();
							if (pu) pu->queueCreateUnit(gbT, pu->requestUniqueUnitID());
						}
						sbLastProd = frame;
					}

					// patrol: re-task every gunboat to a fresh water cell every ~6s
					if (gbT && sbWaterCount > 0 && frame - sbLastPatrol >= 180)
					{
						for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
						{
							if (o->getTemplate() != gbT) continue;
							AIUpdateInterface *ai = o->getAIUpdateInterface();
							if (!ai) continue;
							// gather the fleet around the lake centre (with a per-boat offset so
							// they spread into a visible flotilla rather than stacking on one cell)
							UnsignedInt id = (UnsignedInt)o->getID();
							Coord3D t = sbCenter;
							t.x += (Real)((id * 53 + (frame / 180) * 31) % 280) - 140.0f;
							t.y += (Real)((id * 89 + (frame / 180) * 17) % 280) - 140.0f;
							ai->aiMoveToPosition(&t, CMD_FROM_AI);
						}
						sbLastPatrol = frame;
					}
				}
			}

			{
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
				catch (...)
				{
					// try to save info off
					try
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
			}

			TheFramePacer->update();
		}

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}
}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream()
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem()
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession()
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there
	// are, then we will re-run the compression tool on them.

	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.truncateBy(3); // tga
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh ||
					(infoTGA.timestampHigh == infoDDS.timestampHigh &&
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}
