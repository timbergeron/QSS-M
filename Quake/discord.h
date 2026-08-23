/*
Copyright (C) 2026 QSS-M contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef DISCORD_H
#define DISCORD_H

#define DISCORD_COMMUNITY_URL "https://discord.quakeone.com/"

typedef enum
{
	DISCORD_PRESENCE_MODE_OFF,
	DISCORD_PRESENCE_MODE_ALL,
	DISCORD_PRESENCE_MODE_CONNECTED,
	DISCORD_PRESENCE_MODE_COUNT
} discord_presence_mode_t;

typedef enum
{
	DISCORD_TEST_IDLE,
	DISCORD_TEST_SENDING,
	DISCORD_TEST_DELIVERED,
	DISCORD_TEST_HTTP_ERROR,
	DISCORD_TEST_TIMEOUT,
	DISCORD_TEST_TRANSPORT_ERROR,
	DISCORD_TEST_NOT_CONFIGURED
} discord_test_status_t;

typedef enum
{
	DISCORD_COMMUNITY_IDLE,
	DISCORD_COMMUNITY_LOADING,
	DISCORD_COMMUNITY_READY,
	DISCORD_COMMUNITY_ERROR
} discord_community_status_t;

extern cvar_t cl_discord_presence;
extern cvar_t con_notifydiscord;

void Discord_Init (void);
void Discord_Shutdown (void);
void Discord_Frame (void);

void Discord_NotifyMention (const char *raw_message, const char *chat_body);
void Discord_NotifyDirectMessage (const char *raw_message);
discord_test_status_t Discord_NotifyTest (void);
discord_test_status_t Discord_NotifyTestStatus (int *http_code);

void Discord_CommunityRefresh (void);
discord_community_status_t Discord_CommunityStatus (int *online_count);

#endif /* DISCORD_H */
