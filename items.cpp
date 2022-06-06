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

#include "items.h"
#include "utils.h"
#include "tournament.h"
#include "editablecommands.h"

CItems g_Items;


ConVar tftrue_no_hats("tftrue_no_hats", "0", FCVAR_NOTIFY,
	"Activate/Deactivate hats.",
	true, 0, true, 1,
	CItems::RebuildWhitelist);

ConVar tftrue_no_misc("tftrue_no_misc", "0", FCVAR_NOTIFY,
	"Activate/Deactivate misc items.",
	true, 0, true, 1,
	CItems::RebuildWhitelist);

ConVar tftrue_no_action("tftrue_no_action", "0", FCVAR_NOTIFY,
	"Activate/Deactivate action items.",
	true, 0, true, 1,
	CItems::RebuildWhitelist);

ConVar tftrue_whitelist_id("tftrue_whitelist_id", "-1", FCVAR_NOTIFY,
	"ID of the whitelist to use from whitelist.tf.\n"
	"If set to anything but -1, overrides the value of mp_tournament_whitelist",
	true, -1, false, 0,
	CItems::RebuildWhitelist);

CItems::CItems()
{
	V_strncpy(szWhiteListName, "error", sizeof(szWhiteListName));
	memset(szWhiteListChosen, 0, sizeof(szWhiteListChosen));

	item_whitelist = new KeyValues("item_whitelist");
	item_schema    = new KeyValues("");
}

CItems::~CItems()
{
	item_whitelist->deleteThis();
	item_schema->deleteThis();
}

bool CItems::Init(const CModuleScanner& ServerModule)
{
	item_schema->LoadFromFile(filesystem, "scripts/items/items_game.txt", "MOD");

	char* os;

#ifdef _LINUX

	os = (char*)"Linux";

	void *GetLoadoutItem                                    = ServerModule.FindSymbol(
	"_ZN9CTFPlayer14GetLoadoutItemEiib");
	ReloadWhitelist                                         = ServerModule.FindSymbol(
	"_ZN15CEconItemSystem15ReloadWhitelistEv");
	ItemSystem                                              = ServerModule.FindSymbol(
	"_Z10ItemSystemv");
	GiveDefaultItems                                        = ServerModule.FindSymbol(
	"_ZN9CTFPlayer16GiveDefaultItemsEv");
	GetItemDefinition                                       = ServerModule.FindSymbol(
	"_ZN15CEconItemSchema17GetItemDefinitionEi");
	// why in god's name does this need this awful type
	RemoveWearable                                          = (void (*)(void *, void*))ServerModule.FindSymbol(
	"_ZN11CBasePlayer14RemoveWearableEP13CEconWearable");

#else

	os = (char*)"Windows";

	void *GetLoadoutItem                                    = ServerModule.FindSignature((unsigned char*)
	"\x55\x8B\xEC\x51\x53\x56\x8B\xF1\x8B\x0D\x00\x00\x00\x00\x57\x89\x75\xFC", "xxxxxxxxxx????xxxx");
	ReloadWhitelist                                         = ServerModule.FindSignature((unsigned char*)
	"\x55\x8B\xEC\x83\xEC\x0C\x53\x56\x57\x8B\xD9\xC6\x45\xFF\x01", "xxxxxxxxxxxxxxx");
	ItemSystem                                              = ServerModule.FindSignature((unsigned char*)
	"\xA1\x00\x00\x00\x00\x85\xC0\x75\x4F", "x????xxxx");
	RemoveWearable                                          = (void (__thiscall*)(void *, void*))ServerModule.FindSignature((unsigned char *)
	"\x55\x8B\xEC\x53\x8B\xD9\x57\x8B\xBB\x00\x00\x00\x00\x83\xEF\x01", "xxxxxxxxx????xxx");
	GiveDefaultItems                                        = ServerModule.FindSignature((unsigned char *)
	"\x56\x57\x8B\xF9\xFF\xB7\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x83\xC4\x04", "xxxxxx????x????xxx");
	GetItemDefinition                                       = ServerModule.FindSignature((unsigned char*)
	"\x55\x8B\xEC\x56\x8B\xF1\x8D\x45\x08\x57\x50\x8D\x8E\x00\x00\x00\x00\xE8", "xxxxxxxxxxxxx????x");

#endif

	if(!GetLoadoutItem)
	{
		Warning("Can't get sig for GetLoadoutItem! OS: %s\n", os);
	}
	else
	{
#ifndef _LINUX
		PatchAddress((void*)GetLoadoutItem, 0xA7, 2, (unsigned char*)"\x90\x90");
#else
		PatchAddress((void*)GetLoadoutItem, 0xF0, 2, (unsigned char*)"\x90\x90");
#endif
	}

	if(!ItemSystem)
	{
		Warning("Can't get sig for ItemSystem! OS: %s\n", os);
	}
	if(!ReloadWhitelist)
	{
		Warning("Can't get sig for ReloadWhitelist! OS: %s\n", os);
	}
	else
	{
#ifndef _LINUX
		PatchAddress((void*)ReloadWhitelist, 0x45, 2, (unsigned char*)"\x90\x90");
#else
		PatchAddress((void*)ReloadWhitelist, 0x34, 2, (unsigned char*)"\x90\x90");
#endif
	}

	if(!GetItemDefinition)
	{
		Warning("Can't get sig for GetItemDefinition! OS: %s\n", os);
	}
	if(!RemoveWearable)
	{
		Warning("Can't get sig for RemoveWearable! OS: %s\n", os);
	}
	if(!GiveDefaultItems)
	{
		Warning("Can't get sig for GiveDefaultItems! OS: %s\n", os);
	}


	ConVarRef mp_tournament_whitelist("mp_tournament_whitelist");
	((ConVar*)mp_tournament_whitelist.GetLinkedConVar())->InstallChangeCallback(CItems::TournamentWhitelistCallback);

	g_OldSpewOutputFunc = GetSpewOutputFunc();
	SpewOutputFunc(TFTrueSpew);

	return (GetLoadoutItem && ItemSystem && ReloadWhitelist && GetItemDefinition &&
			RemoveWearable && GiveDefaultItems);
}

void CItems::OnUnload()
{
	ConVarRef mp_tournament_whitelist("mp_tournament_whitelist");
	((ConVar*)mp_tournament_whitelist.GetLinkedConVar())->InstallChangeCallback(nullptr);

	SpewOutputFunc(g_OldSpewOutputFunc);
}

// pKItem can be an item or a prefab if doing recursion
char *CItems::GetAttributeValue(KeyValues *pKItem, const char *szAttribute)
{
	char *szValue = (char*)pKItem->GetString(szAttribute, NULL);

	if(!szValue)
	{
		KeyValues *pKPrefab = NULL;

		// We have a prefab
		if(pKItem->GetString("prefab", NULL))
		{
			char szPrefabName[65] = {};
			V_strncpy(szPrefabName, pKItem->GetString("prefab", NULL), sizeof(szPrefabName));

			char *pPrefab = strtok((char*)szPrefabName, " ");
			while(pPrefab != NULL)
			{
				pKPrefab = item_schema->FindKey("prefabs", true)->FindKey(pPrefab, false);
				if (pKPrefab)
				{
					szValue = (char*)pKPrefab->GetString(szAttribute, NULL);

					// If our prefab has a prefab, then call GetAttributeValue on the prefab
					const char *szPrefabPrefabName = pKPrefab->GetString("prefab", NULL);
					if(szPrefabPrefabName)
					{
						szValue = GetAttributeValue(pKPrefab, szAttribute);
					}

					// If we have found the attribute, get out of the loop
					if(szValue)
					{
						break;
					}
				}

				pPrefab = strtok(NULL, " ");
			}
		}
	}
	return szValue;
}


void CItems::TournamentWhitelistCallback(IConVar *var, const char *pOldValue, float flOldValue)
{
	// we're using tftrue_whitelist_id
	if (strcmp(tftrue_whitelist_id.GetString(), "-1") != 0)
	{
		Msg("[TFTruer] tftrue_whitelist_id = %s! mp_tournament_whitelist updated, ignoring!\n", tftrue_whitelist_id.GetString());
	}
	// we're not using tftrue whitelist id
	else
	{
		Msg("[TFTruer] tftrue_whitelist_id = %s! mp_tournament_whitelist updated, reloading whitelist!\n", tftrue_whitelist_id.GetString());

		static ConVarRef mp_tournament_whitelist("mp_tournament_whitelist");
		V_strncpy(g_Items.szWhiteListChosen, mp_tournament_whitelist.GetString(), sizeof(g_Items.szWhiteListChosen));
		RebuildWhitelist(NULL, NULL, 0.0);
	}
}

void CItems::RebuildWhitelist(IConVar *var, const char *pOldValue, float flOldValue)
{
	static ConVarRef mp_tournament_whitelist("mp_tournament_whitelist");
	EditableConVar* mptw = (EditableConVar*)(ConVar*)mp_tournament_whitelist.GetLinkedConVar();

	bool using_id = false;
	char szConfigPath[50];

	// we're using tftrue_whitelist_id
	if (strcmp(tftrue_whitelist_id.GetString(), "-1") != 0)
	{
		Msg("[TFTruer] -> Using tftrue_whitelist_id as tftrue_whitelist_id != -1...\n");

		using_id = true;
		ConVar* v = (ConVar*)var;

		if (!(mptw->IsFlagSet( FCVAR_DEVELOPMENTONLY )))
		{
			Msg("[TFTruer] -> Setting mp_tournament_whitelist to FCVAR_DEVELOPMENTONLY...\n");
			mptw->m_nFlags |= FCVAR_DEVELOPMENTONLY;
		}

		// if our var that changed is tftrue_whitelist_id
		if (v == &tftrue_whitelist_id)
		{
			// get the current and previous values to compare
			const char* oldval = pOldValue;
			const char* newval = v->GetString();

			// we didn't change, don't do extra work
			if (strcmp(oldval, newval) == 0)
			{
				Msg("[TFTruer] Not redownloading whitelist, tftrue_whitelist_id didn't change and isn't -1!\n");
				return;
			}
		}

		// something DID change, let's yeet our current whitelist and redownload it.
		// TODO: Check file modification time and only allow updating every hour
		g_Items.item_whitelist->Clear();

		int iWhiteListID = 0;
		char szConfigURL[50];

		// Handle int vs string whitelist ids
		if (sscanf(tftrue_whitelist_id.GetString(), "%d", &iWhiteListID) == 1)
		{
			V_snprintf(szConfigURL, sizeof(szConfigURL),   "https://whitelist.tf/custom_whitelist_%d.txt", tftrue_whitelist_id.GetInt());
			V_snprintf(szConfigPath, sizeof(szConfigPath), "cfg/custom_whitelist_%d.txt",          tftrue_whitelist_id.GetInt());
		}
		else
		{
			V_snprintf(szConfigURL, sizeof(szConfigURL),   "https://whitelist.tf/%s.txt", tftrue_whitelist_id.GetString());
			V_snprintf(szConfigPath, sizeof(szConfigPath), "cfg/%s.txt",          tftrue_whitelist_id.GetString());
		}

		// Download our whitelist
		g_Tournament.DownloadConfig(szConfigURL);

		// Read the whitelist display name for the tftrue commands
		FileHandle_t fh = filesystem->Open(szConfigPath, "r", "MOD");
		if (fh)
		{
			g_Items.item_whitelist->LoadFromFile(filesystem, szConfigPath, "MOD");
			char szLine[255] = "";

			filesystem->ReadLine(szLine, sizeof(szLine), fh);
			filesystem->ReadLine(szLine, sizeof(szLine), fh);

			if(!strncmp("// Whitelist: ", szLine, 14))
			{
				V_strncpy(g_Items.szWhiteListName, szLine+13, sizeof(g_Items.szWhiteListName));
				g_Items.szWhiteListName[strlen(g_Items.szWhiteListName)-2] = '\0';
			}
			else
			{
				V_strncpy(g_Items.szWhiteListName, "custom", sizeof(g_Items.szWhiteListName));
			}

			filesystem->Close(fh);
		}
		else
		{
			V_strncpy(g_Items.szWhiteListName, "error", sizeof(g_Items.szWhiteListName));
		}
	}
	// we're not using tftrue whitelist id
	else
	{
		Msg("[TFTruer] -> Using mp_tournament_whitelist as tftrue_whitelist_id is unset...\n");

		// unignore mp_tournament_whitelist
		if (mptw->IsFlagSet( FCVAR_DEVELOPMENTONLY ))
		{
			Msg("[TFTruer] -> Reverting mp_tournament_whitelist...\n");
			mptw->m_nFlags &= ~FCVAR_DEVELOPMENTONLY;
		}

		if (strcmp(g_Items.szWhiteListChosen, ""))
		{
			g_Items.item_whitelist->LoadFromFile(filesystem, g_Items.szWhiteListChosen, "MOD");
		}
	}

	if (!g_Items.item_whitelist->FindKey("unlisted_items_default_to", false))
	{
		g_Items.item_whitelist->SetInt("unlisted_items_default_to", 1);
	}

	// Loop through all items
	for ( KeyValues *pKItem = g_Items.item_schema->FindKey("items", true)->GetFirstTrueSubKey(); pKItem; pKItem = pKItem->GetNextTrueSubKey() )
	{
		char *szItemSlot    = g_Items.GetAttributeValue(pKItem, "item_slot");
		char *szItemName    = g_Items.GetAttributeValue(pKItem, "name");
		char *szCraftClass  = g_Items.GetAttributeValue(pKItem, "craft_class");
		char *szBaseItem    = g_Items.GetAttributeValue(pKItem, "baseitem");

		// Make sure we have an item name and slot
		if(!szItemName || !szItemSlot)
		{
			continue;
		}

		// Do not try to add the item called "default"
		if(!strcmp(szItemName, "default"))
		{
			continue;
		}

		// Do not add base items
		if (szBaseItem && !strcmp(szBaseItem, "1"))
		{
			continue;
		}

		// Do not add craft tokens
		if(szCraftClass && !strcmp(szCraftClass, "craft_token"))
		{
			continue;
		}

		// Add item to the whitelist
		if  (
				   (tftrue_no_hats.GetInt()   && !strcmpi(szItemSlot, "head"))
				|| (tftrue_no_misc.GetInt()   && !strcmpi(szItemSlot, "misc"))
				|| (tftrue_no_action.GetInt() && !strcmpi(szItemSlot, "action"))
			)
		{
			g_Items.item_whitelist->SetInt(szItemName, 0);
		}
	}

	if (using_id)
	{
		// Save the whitelist
		g_Items.item_whitelist->SaveToFile(filesystem, szConfigPath, "MOD");
		mp_tournament_whitelist.SetValue(szConfigPath);
	}

	// Reload the whitelist
	void *pEconItemSystem = reinterpret_cast<ItemSystemFn>(g_Items.ItemSystem)();
	reinterpret_cast<ReloadWhitelistFn>(g_Items.ReloadWhitelist)(pEconItemSystem);

	// Reload items of all players
	for ( int i = 1; i <= server->GetClientCount(); i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer*)CBaseEntity::Instance(i);
		if (!pPlayer)
		{
			continue;
		}

		// Not a bot
		if(engine->GetPlayerNetInfo(pPlayer->entindex()))
		{
			// Remove taunt
			*g_EntityProps.GetSendProp<int>(pPlayer, "m_Shared.m_nPlayerCond") &= ~TF2_PLAYERCOND_TAUNT;

			// Remove all wearables, GiveDefaultItems doesn't remove them for some reason
			CBaseEntity *pChild = *g_EntityProps.GetDataMapProp<EHANDLE>(pPlayer, "m_hMoveChild");
			while(pChild)
			{
				if(!strcmp(pChild->GetClassname(), "tf_wearable"))
				{
					g_Items.RemoveWearable(pPlayer, pChild);

					// Go back to the first child
					pChild = *g_EntityProps.GetDataMapProp<EHANDLE>(pPlayer, "m_hMoveChild");
				}
				else
				{
					pChild = *g_EntityProps.GetDataMapProp<EHANDLE>(pChild, "m_hMovePeer");
				}
			}

			// m_bRegenerating, set to true to keep ubercharge and other weapon data
			*(bool *)(g_EntityProps.GetSendProp<char>(pPlayer, "m_hItem")-1) = true;
			reinterpret_cast<GiveDefaultItemsFn>(g_Items.GiveDefaultItems)(pPlayer);
			*(bool *)(g_EntityProps.GetSendProp<char>(pPlayer, "m_hItem")-1) = false;
		}
	}
}

const char* CItems::GetItemLogName(int iDefIndex)
{
	void *pEconItemSystem = reinterpret_cast<ItemSystemFn>(ItemSystem)();
	void *pItemDefinition = reinterpret_cast<GetItemDefinitionFn>(GetItemDefinition)((void*)((unsigned long)pEconItemSystem+4), iDefIndex);
	if(!pItemDefinition)
	{
		return nullptr;
	}

	// TODO: find where this offset comes from
	return *(char**)((char*)pItemDefinition + 212);
}
