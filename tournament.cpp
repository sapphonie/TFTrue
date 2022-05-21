/*
 * Copyright (C) 2015 AnAkkk <anakin.cs@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "logs.h"
#include "sourcetv.h"
#include "tournament.h"
#include "editablecommands.h"
#include "utils.h"
#include "iclient.h"

#include <chrono>

CTournament g_Tournament;

int *g_sv_pure_mode                    = nullptr;
int *cmd_source                        = nullptr;
int *cmd_clientslot                    = nullptr;
EditableConCommand *sv_pure            = nullptr;
ConCommand *status                     = nullptr;
ConCommand *mp_tournament_restart      = nullptr;
ConCommand *pause_                     = nullptr;

ConVar tftrue_tournament_config("tftrue_tournament_config", "0", FCVAR_NOTIFY,
	"Available configs:\n"
	"0: Disabled\n"
	"1: ETF2L 6on6\n"
	"2: ETF2L 9on9",
	true, 0, true, 2,
	&CTournament::Tournament_Config_Callback);

ConVar tftrue_unpause_delay("tftrue_unpause_delay", "2", FCVAR_NOTIFY,
	"Set the delay before someone can unpause the game after it has been paused.",
	true, 0, false, 0);

ConVar tftrue_log_verbose_pauses("tftrue_log_verbose_pauses", "1", FCVAR_NOTIFY,
	"Log verbose pause info, in addition to simple Game_Paused and Unpaused events.",
	true, 0, true, 1);

bool CTournament::Init(const CModuleScanner& EngineModule, const CModuleScanner& ServerModule)
{
	gameeventmanager->AddListener(this, "teamplay_game_over", true);
	gameeventmanager->AddListener(this, "tf_game_over", true);
	gameeventmanager->AddListener(this, "teamplay_round_win", true);
	gameeventmanager->AddListener(this, "teamplay_round_stalemate", true);

	ConVarRef mp_tournament("mp_tournament");

	mp_tournament_restart              = g_pCVar->FindCommand("mp_tournament_restart");
	sv_pure       = (EditableConCommand*)g_pCVar->FindCommand("sv_pure");
	status                             = g_pCVar->FindCommand("status");
	pause_                             = g_pCVar->FindCommand("pause");
	ConVarRef                            tf_gamemode_arena   ("tf_gamemode_arena");
	ConVarRef                            tf_gamemode_cp      ("tf_gamemode_cp");
	ConVarRef                            tf_gamemode_ctf     ("tf_gamemode_ctf");
	ConVarRef                            tf_gamemode_payload ("tf_gamemode_payload");
	ConVarRef                            tf_gamemode_mvm     ("tf_gamemode_mvm");
	ConVarRef                            sv_pausable         ("sv_pausable");

	if(!(mp_tournament.IsValid() && tf_gamemode_arena.IsValid() && tf_gamemode_cp.IsValid() && tf_gamemode_ctf.IsValid() && tf_gamemode_payload.IsValid() &&
		 tf_gamemode_mvm.IsValid() && sv_pausable.IsValid() && mp_tournament_restart && sv_pure && status && pause_))
	{
		Warning("[TFTruer] Can't find tournament cvars\n");
		return false;
	}

	ConCommand *mp_tournament_restart = g_pCVar->FindCommand("mp_tournament_restart");
	if(mp_tournament_restart)
		m_DispatchTournamentRestartRoute.RouteVirtualFunction(mp_tournament_restart, &ConCommand::Dispatch, &CTournament::Tournament_Restart_Callback, false);

	EditableConCommand *sv_pure = (EditableConCommand*)g_pCVar->FindCommand("sv_pure");
	if(sv_pure)
		m_DispatchPureRoute.RouteVirtualFunction(sv_pure, &ConCommand::Dispatch, &CTournament::Pure_Callback, false);

	EditableConCommand *status = (EditableConCommand*)g_pCVar->FindCommand("status");
	if(status)
		m_DispatchStatusRoute.RouteVirtualFunction(status, &ConCommand::Dispatch, &CTournament::Status_Callback, false);

	ConCommand *pause = g_pCVar->FindCommand("pause");
	if(pause)
		m_DispatchPauseRoute.RouteVirtualFunction(pause, &ConCommand::Dispatch, &CTournament::Pause_Callback, false);

	char* os;

	os = (char*)"Linux";


#ifdef _LINUX

	os = (char*)"Linux";

	void *pStartCompetitiveMatch                                = ServerModule.FindSymbol(
	"_ZN12CTFGameRules21StartCompetitiveMatchEv");
	void *CanPlayerChooseClass                                  = ServerModule.FindSymbol(
	"_ZN12CTFGameRules20CanPlayerChooseClassEP11CBasePlayeri");

	cmd_source                                                  = (int*)EngineModule.FindSymbol(
	"cmd_source");
	cmd_clientslot                                              = (int*)EngineModule.FindSymbol(
	"cmd_clientslot");
	// _Z13GetSvPureModev -> _ZL14g_sv_pure_mode
	g_sv_pure_mode                                              = (int*)EngineModule.FindSymbol(
	"_ZL14g_sv_pure_mode");
	void *CTFGameRules_GetClassLimit = ServerModule.FindSymbol("_ZN12CTFGameRules13GetClassLimitEi");
	PatchAddress((void*)CTFGameRules_GetClassLimit, 0x18, 1, (unsigned char*)"\xEB");

#else

	os = (char*)"Windows";

	void *pStartCompetitiveMatch                                = ServerModule.FindSignature(
	"\x56\x8B\xF1\x6A\x00\xC7\x86", "xxxxxxx");
	void *CanPlayerChooseClass                                  = ServerModule.FindSignature((unsigned char*)
	"\x55\x8B\xEC\x83\xEC\x08\xFF\x75\x0C\xE8\x00","xxxxxxxxxx?");

	// _ZL6statusRK8CCommand on linux (status concommand)
	// uniq string "(secure mode enabled, disconnected from Steam3)"

	// offset 0x09 + 2 (operator)
	// dword_1060AC60
	// 83 3D	60 AC 60 10
	cmd_source                                                  = *(int**)((unsigned char*)
	status->m_fnCommandCallback + 0xB);

	// offset 0x41 + 2 (operator)
	// dword_103ACC54
	// 8B 0D	54 CC 3A 10
	cmd_clientslot                                              = *(int**)((unsigned char*)
	status->m_fnCommandCallback + 0x43);


	// SV_Pure_f / _ZL9SV_Pure_fRK8CCommand on linux (sv_pure concommand)
	// uniq string "sv_pure set to %d"
	// immediately after is our g_sv_pure_mode offset

	// offset 0x56 + 2 (operator)
	// dword_105D8A2C
	// 89 3D	2C 8A 5D 10
	g_sv_pure_mode                                              = *(int**)((unsigned char*)
	sv_pure->m_fnCommandCallback + 0x58);

	// CanPlayerChooseClass calls another function that calls IsInTournamentMode, so we need the address of that other function
	unsigned long CanPlayerChooseClass_TournamentCall   = (unsigned long)((unsigned char*)CanPlayerChooseClass + 0xA);
	unsigned long CanPlayerChooseClass_TournamentOffset = *(unsigned long*)(CanPlayerChooseClass_TournamentCall);
	unsigned long CanPlayerChooseClass_Tournament       = CanPlayerChooseClass_TournamentOffset + CanPlayerChooseClass_TournamentCall + 4;

	PatchAddress((void*)CanPlayerChooseClass_Tournament, 0xD, 1, (unsigned char*)"\xEB");

#endif

	if(pStartCompetitiveMatch)
	{
		m_StartCompetitiveMatchRoute.RouteFunction(pStartCompetitiveMatch, (void*)&CTournament::StartCompetitiveMatch);
	}
	else
	{
		Warning("[TFTruer] Couldn't get sig for pStartCompetitiveMatch! OS: %s\n", os);
	}

	if (!CanPlayerChooseClass)
	{
		Warning("[TFTruer] Couldn't get sig for CanPlayerChooseClass! OS: %s\n", os);
		return false;
	}
	if (!cmd_source)
	{
		Warning("[TFTruer] Couldn't get sig for cmd_source! OS: %s\n", os);
		return false;
	}
	if (!cmd_clientslot)
	{
		Warning("[TFTruer] Couldn't get sig for cmd_clientslot! OS: %s\n", os);
		return false;
	}
	if (!g_sv_pure_mode)
	{
		Warning("[TFTruer] Couldn't get sig for g_sv_pure_mode! OS: %s\n", os);
		return false;
	}

	((ConVar*)mp_tournament.GetLinkedConVar())->InstallChangeCallback(&CTournament::Tournament_Callback);
	((ConVar*)tf_gamemode_arena.GetLinkedConVar())->InstallChangeCallback(&CTournament::GameMode_Callback);
	((ConVar*)tf_gamemode_cp.GetLinkedConVar())->InstallChangeCallback(&CTournament::GameMode_Callback);
	((ConVar*)tf_gamemode_ctf.GetLinkedConVar())->InstallChangeCallback(&CTournament::GameMode_Callback);
	((ConVar*)tf_gamemode_payload.GetLinkedConVar())->InstallChangeCallback(&CTournament::GameMode_Callback);
	((ConVar*)tf_gamemode_mvm.GetLinkedConVar())->InstallChangeCallback(&CTournament::GameMode_Callback);
	((ConVar*)mp_tournament.GetLinkedConVar())->InstallChangeCallback(&CTournament::Tournament_Callback);


	return true;
}

void CTournament::OnUnload()
{
	gameeventmanager->RemoveListener(this);

	ConVarRef mp_tournament("mp_tournament");
	ConVarRef tf_gamemode_arena("tf_gamemode_arena");
	ConVarRef tf_gamemode_cp("tf_gamemode_cp");
	ConVarRef tf_gamemode_ctf("tf_gamemode_ctf");
	ConVarRef tf_gamemode_payload("tf_gamemode_payload");
	ConVarRef tf_gamemode_mvm("tf_gamemode_mvm");

	((ConVar*)mp_tournament.GetLinkedConVar())->InstallChangeCallback(nullptr);
	((ConVar*)tf_gamemode_arena.GetLinkedConVar())->InstallChangeCallback(nullptr);
	((ConVar*)tf_gamemode_cp.GetLinkedConVar())->InstallChangeCallback(nullptr);
	((ConVar*)tf_gamemode_ctf.GetLinkedConVar())->InstallChangeCallback(nullptr);
	((ConVar*)tf_gamemode_payload.GetLinkedConVar())->InstallChangeCallback(nullptr);
	((ConVar*)tf_gamemode_mvm.GetLinkedConVar())->InstallChangeCallback(nullptr);
}

void CTournament::OnServerActivate()
{
	m_bTournamentStarted = false;
}

void CTournament::StartCompetitiveMatch(void *pGameRules EDX2)
{
	typedef void (__thiscall *StartCompetitiveMatch_t)(void *pGameRules);
	g_Tournament.m_StartCompetitiveMatchRoute.CallOriginalFunction<StartCompetitiveMatch_t>()(pGameRules);

	static ConVarRef mp_tournament_blueteamname("mp_tournament_blueteamname");
	static ConVarRef mp_tournament_redteamname("mp_tournament_redteamname");

	std::string strBlueTeamName = mp_tournament_blueteamname.GetString();
	std::string strRedTeamName  = mp_tournament_redteamname.GetString();

	ReplaceAlphaWithUnderscore(strBlueTeamName);
	ReplaceAlphaWithUnderscore(strRedTeamName);

	char szBlueTeamName[10];
	char szRedTeamName[10];

	strncpy(szBlueTeamName, strBlueTeamName.c_str(), sizeof(szBlueTeamName));
	strncpy(szRedTeamName, strRedTeamName.c_str(), sizeof(szBlueTeamName));

	g_SourceTV.OnTournamentStarted(szBlueTeamName, szRedTeamName);

	char szMsg[100];
	V_snprintf(szMsg, sizeof(szMsg), "Tournament mode started\nBlue Team: %s\nRed Team: %s\n", szBlueTeamName, szRedTeamName);
	engine->LogPrint(szMsg);

	static ConVarRef sv_cheats("sv_cheats");
	if (sv_cheats.GetBool())
	{
		AllMessage("\003[TFTruer] WARNING: Cheats are enabled !\n");
	}

	if (*g_sv_pure_mode != 2)
	{
		AllMessage("\003[TFTruer] WARNING: The server is not correctly set up: sv_pure is not enabled!\n");
	}

	if (g_Tournament.m_iConfigDownloadFailed)
	{
		AllMessage("\003[TFTruer] WARNING: The download of %d tournament config files failed! "
				   "The server might not be setup correctly.\n", g_Tournament.m_iConfigDownloadFailed);
	}

	g_Logs.OnTournamentStarted();

	g_Tournament.m_bTournamentStarted = true;
}

void CTournament::FireGameEvent(IGameEvent *pEvent)
{
	if(!strcmp(pEvent->GetName(),"teamplay_game_over") || !strcmp(pEvent->GetName(), "tf_game_over"))
	{
		if(m_bTournamentStarted)
		{
			m_bTournamentStarted = false;

			g_SourceTV.OnGameOver();
			g_Logs.OnGameOver();
		}
	}
	else if(!strcmp(pEvent->GetName(), "teamplay_round_win") || !strcmp(pEvent->GetName(), "teamplay_round_stalemate"))
	{
		if(m_bTournamentStarted)
			g_Logs.OnRoundWin();
	}
}

void CTournament::FindMapType()
{
	static ConVarRef tf_gamemode_arena("tf_gamemode_arena");
	static ConVarRef tf_gamemode_cp("tf_gamemode_cp");
	static ConVarRef tf_gamemode_ctf("tf_gamemode_ctf");
	static ConVarRef tf_gamemode_payload("tf_gamemode_payload");
	static ConVarRef tf_gamemode_mvm("tf_gamemode_mvm");

	eMapType = MAPTYPE_UNKNOWN;

	if(tf_gamemode_arena.GetBool())
		eMapType = MAPTYPE_ARENA;

	if(tf_gamemode_cp.GetBool())
	{
		CBaseEntity *pEntity = nullptr;

		while((pEntity = g_pServerTools->FindEntityByClassname(pEntity, "team_control_point")) != nullptr)
		{
			eMapType = MAPTYPE_ATTACKDEFENSE;

			if(*g_EntityProps.GetSendProp<int>(pEntity, "m_iTeamNum") != 2)
			{
				eMapType = MAPTYPE_CP;
				break;
			}
		}
	}

	if(tf_gamemode_ctf.GetBool())
		eMapType = MAPTYPE_CTF;

	if(tf_gamemode_payload.GetBool())
		eMapType = MAPTYPE_ATTACKDEFENSE;

	if(tf_gamemode_mvm.GetBool())
		eMapType = MAPTYPE_MVM;
}

void CTournament::SetTournamentMapVars()
{
	switch(tftrue_tournament_config.GetInt())
	{
		case CONFIG_ETF2L6v6:
		{
			switch(eMapType)
			{
				case MAPTYPE_ATTACKDEFENSE:
					engine->InsertServerCommand("exec etf2l_6v6_stopwatch\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_CP:
					engine->InsertServerCommand("exec etf2l_6v6_5cp\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_CTF:
					engine->InsertServerCommand("exec etf2l_6v6_ctf\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_ARENA:
					engine->InsertServerCommand("exec etf2l_6v6_koth\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_MVM:
				case MAPTYPE_UNKNOWN:
					break;
			}
			break;
		}
		case CONFIG_ETF2L9v9:
		{
			switch(eMapType)
			{
				case MAPTYPE_ATTACKDEFENSE:
					engine->InsertServerCommand("exec etf2l_9v9_stopwatch\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_CP:
					engine->InsertServerCommand("exec etf2l_9v9_5cp\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_CTF:
					engine->InsertServerCommand("exec etf2l_9v9_ctf\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_ARENA:
					engine->InsertServerCommand("exec etf2l_9v9_koth\n");
					engine->ServerExecute();
					break;
				case MAPTYPE_MVM:
				case MAPTYPE_UNKNOWN:
					break;
			}
			break;
		}
	}
}

void CTournament::GameMode_Callback( IConVar *var, const char *pOldValue, float flOldValue )
{
	static ConVarRef mp_tournament("mp_tournament");
	g_Tournament.FindMapType();

	ConVar* v = (ConVar*)var;
	if(v->GetBool())
	{
		if(mp_tournament.GetBool())
			g_Tournament.SetTournamentMapVars();
	}
}

void CTournament::Tournament_Callback( IConVar *var, const char *pOldValue, float flOldValue )
{
	static ConVarRef tf_gamemode_mvm("tf_gamemode_mvm");

	ConVar* v = (ConVar*)var;
	if (v->GetBool() && !flOldValue)
	{
		if(*g_sv_pure_mode == 2 && !tf_gamemode_mvm.GetBool())
		{
			sv_pure->m_nFlags |= FCVAR_DEVELOPMENTONLY;
		}

		g_Tournament.SetTournamentMapVars();
	}
	else if(!v->GetBool() && flOldValue)
	{
		sv_pure->m_nFlags &= ~FCVAR_DEVELOPMENTONLY;
	}
}

void CTournament::Tournament_Restart_Callback(ConCommand *pCmd, EDX const CCommand &args)
{
	g_Plugin.ForwardCommand(pCmd, args);

	if(g_Plugin.GetCommandIndex() == -1)
	{
		g_Tournament.m_bTournamentStarted = false;
		g_SourceTV.StopTVRecord();
		g_Logs.ResetLastLogID();

		// TODO: create a new log if we're auto uploading?
	}
}

void CTournament::Tournament_Config_Callback( IConVar *var, const char *pOldValue, float flOldValue )
{
	static ConVarRef mp_tournament("mp_tournament");

	ConVar *v = (ConVar*)var;
	if(v->GetInt() == CONFIG_NONE)
	{
		return;
	}

	switch(v->GetInt())
	{
		case CONFIG_ETF2L6v6:
		{
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_custom.cfg", false);
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_6v6.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_6v6_5cp.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_6v6_ctf.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_6v6_koth.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_6v6_stopwatch.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_whitelist_6v6.txt");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_golden_cap.cfg");

			break;
		}
		case CONFIG_ETF2L9v9:
		{
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_custom.cfg", false);
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_9v9.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_9v9_5cp.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_9v9_ctf.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_9v9_koth.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_9v9_stopwatch.cfg");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_whitelist_9v9.txt");
			g_Tournament.DownloadConfig("https://etf2l.org/configs/etf2l_golden_cap.cfg");
			break;
		}
	}

	if(v->GetInt() != flOldValue && mp_tournament.GetBool())

		g_Tournament.SetTournamentMapVars();
}


void CTournament::Pure_Callback(ConCommand *pCmd, EDX const CCommand &args)
{
	static ConVarRef mp_tournament("mp_tournament");
	static ConVarRef tf_gamemode_mvm("tf_gamemode_mvm");

	int iOldPureValue = *g_sv_pure_mode;

	g_Plugin.ForwardCommand(pCmd, args);

	int iNewPureValue = *g_sv_pure_mode;

	if(iNewPureValue == 2 && iOldPureValue != 2)
	{
		if(mp_tournament.GetBool() && !tf_gamemode_mvm.GetBool())
			sv_pure->m_nFlags |= FCVAR_DEVELOPMENTONLY;

		AllMessage("\003[TFTruer] sv_pure changed to 2. Changing map...\n");
		g_Plugin.ForceReloadMap(gpGlobals->curtime+3.0f);
	}
	else if(iNewPureValue == 1 && iOldPureValue != 1)
	{
		AllMessage("\003[TFTruer] sv_pure changed to 1. Changing map...\n");
		g_Plugin.ForceReloadMap(gpGlobals->curtime+3.0f);
	}
	else if(iNewPureValue == 0 && iOldPureValue != 0)
	{
		AllMessage("\003[TFTruer] sv_pure changed to 0. Changing map...\n");
		g_Plugin.ForceReloadMap(gpGlobals->curtime+3.0f);
	}
	else if(iNewPureValue == -1 && iOldPureValue != -1)
	{
		AllMessage("\003[TFTruer] sv_pure changed to -1. Changing map...\n");
		g_Plugin.ForceReloadMap(gpGlobals->curtime+3.0f);
	}
}

void CTournament::Status_Callback(ConCommand *pCmd, EDX const CCommand &args)
{
	g_Plugin.ForwardCommand(pCmd, args);

	static ConVarRef mp_tournament("mp_tournament");
	static ConVarRef tf_gamemode_mvm("tf_gamemode_mvm");

	if (cmd_source && *cmd_source == 1)
	{
		static ConCommand* plugin_print = g_pCVar->FindCommand("plugin_print");
		plugin_print->Dispatch(CCommand());
	}
	else if(cmd_clientslot && *cmd_clientslot != -1 && mp_tournament.GetBool() && !tf_gamemode_mvm.GetBool())
	{
		edict_t* pEntity = EdictFromIndex(*cmd_clientslot+1);
		if(helpers)
		{
			engine->ClientPrintf(pEntity, "Loaded plugins:\n");
			engine->ClientPrintf(pEntity, "---------------------\n");
			CUtlVector<CPlugin *> *m_Plugins = (CUtlVector<CPlugin *>*)((char*)helpers+4);
			for ( int i = 0; i < m_Plugins->Count(); i++ )
			{
				char szPluginName[255];
				V_snprintf(szPluginName, sizeof(szPluginName), "%s\n", m_Plugins->Element(i)->m_szName);
				engine->ClientPrintf(pEntity, szPluginName);
			}
			engine->ClientPrintf(pEntity, "---------------------\n");
		}

		PrintTFTrueInfos(pEntity);
	}
}

void CTournament::Pause_Callback(ConCommand *pCmd, EDX const CCommand &args)
{
	static ConVarRef sv_pausable("sv_pausable");

	// *cmd_source = 0 when it's a client calling it
	if (cmd_source && cmd_clientslot && sv_pausable.GetBool() && *cmd_source == 0)
	{
		// client index
		int icl                 = *cmd_clientslot + 1;

		IClient* pClient        = g_pServer->GetClient(*cmd_clientslot);

		// null or not active client
		if (!pClient || !pClient->IsActive())
		{
			return;
		}

		edict_t* pEntity = EdictFromIndex(icl);
		if (!pEntity)
		{
			return;
		}

		IPlayerInfo* pInfo = playerinfomanager->GetPlayerInfo(pEntity);
		if (!pInfo)
		{
			return;
		}

		int iTeamNum = pInfo->GetTeamIndex();

		char szTeamName[5];

		if(iTeamNum == 2)
		{
			V_strncpy(szTeamName, "Red", sizeof(szTeamName));
		}
		else if(iTeamNum == 3)
		{
			V_strncpy(szTeamName, "Blue", sizeof(szTeamName));
		}

		// unpausing
		if(g_pServer->IsPaused())
		{
			if(time(NULL) >= g_Tournament.m_tNextUnpauseAllowed)
			{
				AllMessage(icl, "\x05[TFTruer] The game was unpaused by \x03%s\x05.\n", pClient->GetClientName());

				// https://github.com/AnAkkk/TFTrue/issues/17#issuecomment-674427577
				char msg[128];
				V_snprintf(msg, sizeof(msg), "%s", "World triggered \"Game_Unpaused\"\n");
				engine->LogPrint(msg);
				// verbose mode
				if (tftrue_log_verbose_pauses.GetBool())
				{
					V_snprintf(msg, sizeof(msg),
					  "\"%s<%d><%s><%s>\" triggered \"matchunpause\"\n",
					  pInfo->GetName(),
					  pInfo->GetUserID(),
					  pInfo->GetNetworkIDString(),
					  szTeamName);
					engine->LogPrint(msg);

					auto PauseEndTime          = std::chrono::high_resolution_clock::now();
					double elapsed_time_ms     = std::chrono::duration<double, std::milli>(PauseEndTime-g_Tournament.m_tPauseStartTime).count();
					double elapsed_time_sec    = elapsed_time_ms / 1000.0;

					// just for you, wiet
					// https://github.com/AnAkkk/TFTrue/issues/17#issue-678751185
					V_snprintf(msg, sizeof(msg),
					  "World triggered \"Pause_Length\" (seconds \"%.2f\")\n", elapsed_time_sec);
					engine->LogPrint(msg);
				}

				g_Plugin.ForwardCommand(pCmd, args);
			}
			else
			{
				Message(icl, "\x07%sPlease wait %ld seconds before unpausing!\n", "FF0000", g_Tournament.m_tNextUnpauseAllowed - time(NULL));
			}
		}
		// pausing
		else
		{
			g_Tournament.m_tNextUnpauseAllowed   = time(NULL) + tftrue_unpause_delay.GetInt();
			g_Tournament.m_tPauseStartTime       = std::chrono::high_resolution_clock::now();
			AllMessage(icl, "\x05[TFTruer] The game was paused by \x03%s\x05.\n", pClient->GetClientName());

			// https://github.com/AnAkkk/TFTrue/issues/17#issuecomment-674427577
			char msg[128];
			V_snprintf(msg, sizeof(msg), "%s", "World triggered \"Game_Paused\"\n");
			engine->LogPrint(msg);
			// verbose mode
			if (tftrue_log_verbose_pauses.GetBool())
			{
				V_snprintf(msg, sizeof(msg),
				  "\"%s<%d><%s><%s>\" triggered \"matchpause\"\n",
				  pInfo->GetName(),
				  pInfo->GetUserID(),
				  pInfo->GetNetworkIDString(),
				  szTeamName);
				engine->LogPrint(msg);
			}
			g_Plugin.ForwardCommand(pCmd, args);
		}
	}
}

void CTournament::DownloadConfig(const char *szURL, bool bOverwrite)
{
	const char *pFileName = strrchr(szURL, '/');

	char szGameDir[MAX_PATH];
	engine->GetGameDir(szGameDir, sizeof(szGameDir));

	char szFullPath[MAX_PATH];
	char szBakFile[MAX_PATH];
	V_snprintf(szFullPath, sizeof(szFullPath), "%s/cfg%s", szGameDir, pFileName);
	V_snprintf(szBakFile, sizeof(szBakFile), "%s/cfg%s.bak", szGameDir, pFileName);

	if(!bOverwrite && filesystem->FileExists(szFullPath))
		return;

	std::unique_ptr<ConfigDownloadRequest> configDownloadRequest(new ConfigDownloadRequest());
	configDownloadRequest->url = szURL;
	SteamAPICall_t hCallServer;
	configDownloadRequest->handle = steam.SteamHTTP()->CreateHTTPRequest(k_EHTTPMethodGET, szURL);
	steam.SteamHTTP()->SetHTTPRequestHeaderValue(configDownloadRequest->handle, "Cache-Control", "no-cache");
	steam.SteamHTTP()->SendHTTPRequest(configDownloadRequest->handle, &hCallServer);

	configDownloadRequest->callResult.SetGameserverFlag();
	configDownloadRequest->callResult.Set(hCallServer, this, &CTournament::DownloadConfigCallback);

	m_ConfigDownloadRequests.emplace_back(std::move(configDownloadRequest));
}

void CTournament::DownloadConfigCallback(HTTPRequestCompleted_t *arg, bool bFailed)
{
	std::vector<std::unique_ptr<ConfigDownloadRequest>>::iterator currConfigDownloadRequest;
	for ( auto it = m_ConfigDownloadRequests.begin(); it != m_ConfigDownloadRequests.end(); it++ )
	{
		if(it->get()->handle == arg->m_hRequest)
		{
			currConfigDownloadRequest = it;
			break;
		}
	}

	if(bFailed || arg->m_eStatusCode < 200 || arg->m_eStatusCode > 299)
	{
		uint32 size;
		steam.SteamHTTP()->GetHTTPResponseBodySize(arg->m_hRequest, &size);

		if(size > 0)
		{
			uint8 *pResponse = new uint8[size+1];
			steam.SteamHTTP()->GetHTTPResponseBodyData(arg->m_hRequest, pResponse, size);
			pResponse[size] = '\0';

			Msg("[TFTruer] The config hasn't been received. HTTP error %d. Response: %s\n", arg->m_eStatusCode, pResponse);

			delete[] pResponse;
		}
		else if(!arg->m_bRequestSuccessful)
		{
			Msg("[TFTruer] The config hasn't been received. No response from the server.\n");
		}
		else
		{
			Msg("[TFTruer] The config hasn't been received. HTTP error %d\n", arg->m_eStatusCode);
		}

		m_iConfigDownloadFailed++;
	}
	else
	{
		uint32 size;
		steam.SteamHTTP()->GetHTTPResponseBodySize(arg->m_hRequest, &size);

		if(size > 0)
		{
			uint8 *pResponse = new uint8[size];
			steam.SteamHTTP()->GetHTTPResponseBodyData(arg->m_hRequest, pResponse, size);

			const char *pFileName = strrchr(currConfigDownloadRequest->get()->url.c_str(), '/');

			char szGameDir[MAX_PATH];
			engine->GetGameDir(szGameDir, sizeof(szGameDir));

			char szFullPath[MAX_PATH];
			char szBakFile[MAX_PATH];
			V_snprintf(szFullPath, sizeof(szFullPath), "%s/cfg%s", szGameDir, pFileName);
			V_snprintf(szBakFile, sizeof(szBakFile), "%s/cfg%s.bak", szGameDir, pFileName);

			rename(szFullPath, szBakFile);

			FILE *pConfigFile = fopen(szFullPath, "w");
			if(pConfigFile)
			{
				fwrite(pResponse, 1, size, pConfigFile);
				fclose(pConfigFile);
				Msg("[TFTruer] Successfully downloaded %s\n", currConfigDownloadRequest->get()->url.c_str());
				remove(szBakFile);
			}
			else
			{
				Msg("[TFTruer] Failed to open %s\n", szFullPath);
				rename(szBakFile, szFullPath);
				m_iConfigDownloadFailed++;
			}

			delete[] pResponse;
		}
		else
		{
			Msg("[TFTruer] Received empty response for %s\n", currConfigDownloadRequest->get()->url.c_str());
		}
	}


	m_ConfigDownloadRequests.erase(currConfigDownloadRequest);
	steam.SteamHTTP()->ReleaseHTTPRequest(arg->m_hRequest);
}
