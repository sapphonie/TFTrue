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

#pragma once

#include "SDK.h"
#include "ModuleScanner.h"
#include <curl/curl.h>
class CItems
{
public:
	CItems();
	~CItems();
	bool Init(const CModuleScanner& ServerModule);
	void OnUnload();

	char *GetAttributeValue(KeyValues *pKItem, const char *szAttribute);

	static void RebuildWhitelist(IConVar *var, const char *pOldValue, float flOldValue);
	static void TournamentWhitelistCallback(IConVar *var, const char *pOldValue, float flOldValue);

	KeyValues *item_whitelist = nullptr;
	KeyValues *item_schema = nullptr;
	char szWhiteListChosen[MAX_PATH];

	char *GetWhitelistName() { return szWhiteListName; }

	const char* GetItemLogName(int iDefIndex);

private:
	char szWhiteListName[50];

	void *ReloadWhitelist = nullptr;
	void *ItemSystem = nullptr;
	void *GiveDefaultItems = nullptr;
	void (__thiscall* RemoveWearable)(void *pPlayer, void *pWearable) = nullptr;
	void *GetItemDefinition = nullptr;

	typedef void (__thiscall* ReloadWhitelistFn)( void *pEconItemSystem);
	typedef void *(*ItemSystemFn)( void );
	typedef void (__thiscall* GiveDefaultItemsFn)( void* pEnt );
	typedef void* (__thiscall* GetItemDefinitionFn)(void *thisPtr, int iDefIndex);
};

extern CItems g_Items;

extern ConVar tftrue_no_hats;
extern ConVar tftrue_no_misc;
extern ConVar tftrue_no_action;
extern ConVar tftrue_whitelist_id;

int wltf_mtime;

void Curl_LastUpdateTime_GetResponse(void* ptr, size_t size, size_t nmemb, void* stream)
{
    if (ptr)
    {
        // this is evil and ugly
        wltf_mtime = std::stoi(static_cast<char*>(ptr));
        return;
    }

    wltf_mtime = 0;
    return;
}

bool GetLastUpdateTime()
{
    CURL *curl;
    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, "https://whitelist.tf/last_update_time");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Curl_LastUpdateTime_GetResponse);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        return true;
    }

    return false;
}
